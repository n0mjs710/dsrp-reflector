/*
 *   Copyright (C) 2026 by Cort Buffington N0MJS
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include "reflector.h"
#include "dsrp.h"
#include "log.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

/* ----- small helpers ----------------------------------------------------- */

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Render an address as "ip:port" into out (for logging). */
static void addr_str(const struct sockaddr_storage *ss, char *out, size_t outlen)
{
    char host[INET6_ADDRSTRLEN] = "?";
    unsigned port = 0;

    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *s = (const struct sockaddr_in *)ss;
        inet_ntop(AF_INET, &s->sin_addr, host, sizeof host);
        port = ntohs(s->sin_port);
    } else if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6 *s = (const struct sockaddr_in6 *)ss;
        inet_ntop(AF_INET6, &s->sin6_addr, host, sizeof host);
        port = ntohs(s->sin6_port);
    }
    snprintf(out, outlen, "%s:%u", host, port);
}

/* True if two stored addresses refer to the same host and port. */
static bool addr_eq(const struct sockaddr_storage *a, const struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family)
        return false;

    if (a->ss_family == AF_INET) {
        const struct sockaddr_in *x = (const struct sockaddr_in *)a;
        const struct sockaddr_in *y = (const struct sockaddr_in *)b;
        return x->sin_port == y->sin_port &&
               x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *y = (const struct sockaddr_in6 *)b;
        return x->sin6_port == y->sin6_port &&
               memcmp(&x->sin6_addr, &y->sin6_addr, sizeof x->sin6_addr) == 0;
    }
    return false;
}

/* ----- client table ------------------------------------------------------ */

static int find_client(const reflector_t *r, const struct sockaddr_storage *src)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (r->clients[i].used && addr_eq(&r->clients[i].addr, src))
            return i;
    return -1;
}

/* Return index of the client at src, adding it if new. -1 if the table is full. */
static int find_or_add_client(reflector_t *r, const struct sockaddr_storage *src,
                              socklen_t srclen)
{
    int idx = find_client(r, src);
    if (idx >= 0)
        return idx;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!r->clients[i].used) {
            r->clients[i].used    = true;
            r->clients[i].addr    = *src;
            r->clients[i].addrlen = srclen;
            r->clients[i].last_poll = time(NULL);

            int n = 0;
            for (int j = 0; j < MAX_CLIENTS; j++)
                n += r->clients[j].used ? 1 : 0;

            char who[64];
            addr_str(src, who, sizeof who);
            log_msg("repeater connected: %s (%d total)", who, n);
            return i;
        }
    }

    char who[64];
    addr_str(src, who, sizeof who);
    log_err("client table full (%d); rejecting %s", MAX_CLIENTS, who);
    return -1;
}

/* ----- core relay -------------------------------------------------------- */

/* Send a status (0x00) reply so the repeater's display shows it is linked. */
static void send_status(reflector_t *r, int idx)
{
    unsigned char p[DSRP_STATUS_LEN];
    memset(p, 0, sizeof p);
    memcpy(p, DSRP_TAG, DSRP_TAG_LEN);
    p[DSRP_OFF_TYPE] = DSRP_TYPE_STATUS;

    /* 20-char status text, already space-padded in r->status_text. */
    memcpy(p + DSRP_OFF_STATUS_TEXT, r->status_text, DSRP_STATUS_TEXT_LEN);

    p[DSRP_OFF_STATUS_CODE] = DSTAR_LINKED_LOOPBACK;
    memcpy(p + DSRP_OFF_STATUS_REFL, r->refl_call, DSRP_CALLSIGN_LEN);

    sendto(r->sock, p, sizeof p, 0,
           (const struct sockaddr *)&r->clients[idx].addr, r->clients[idx].addrlen);
}

/*
 * Relay a header or data packet to every connected repeater except the source.
 * The type byte is normalized to the non-busy form (0x20/0x21): MMDVMHost's
 * receive path only accepts those; the busy variants are dropped as unknown.
 */
static void relay(reflector_t *r, unsigned char *buf, size_t len, int from,
                  unsigned char normalized_type)
{
    buf[DSRP_OFF_TYPE] = normalized_type;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!r->clients[i].used || i == from)
            continue;
        sendto(r->sock, buf, len, 0,
               (const struct sockaddr *)&r->clients[i].addr, r->clients[i].addrlen);
    }
}

static void release_talker(reflector_t *r, const char *reason)
{
    if (!r->talking)
        return;
    log_msg("channel released (%s)", reason);
    r->talking    = false;
    r->talker     = -1;
    r->session_id = 0;
}

/* ----- public API -------------------------------------------------------- */

/* Copy src into a fixed, space-padded field (no NUL terminator). */
static void pad_field(char *dst, size_t width, const char *src)
{
    memset(dst, ' ', width);
    if (src != NULL) {
        size_t n = strlen(src);
        if (n > width)
            n = width;
        memcpy(dst, src, n);
    }
}

void reflector_init(reflector_t *r, int sock, const config_t *cfg)
{
    memset(r, 0, sizeof *r);
    r->sock              = sock;
    r->talker            = -1;
    r->client_timeout_s  = cfg->client_timeout_s;
    r->talker_timeout_ms = cfg->talker_timeout_ms;
    r->status_reply      = cfg->status_reply;

    pad_field(r->status_text, DSRP_STATUS_TEXT_LEN, cfg->status_text);
    pad_field(r->refl_call, DSRP_CALLSIGN_LEN, cfg->callsign);
}

void reflector_handle(reflector_t *r, unsigned char *buf, size_t len,
                      const struct sockaddr_storage *src, socklen_t srclen)
{
    if (len < 5U || memcmp(buf, DSRP_TAG, DSRP_TAG_LEN) != 0)
        return;

    switch (buf[DSRP_OFF_TYPE]) {
    case DSRP_TYPE_POLL: {
        int idx = find_or_add_client(r, src, srclen);
        if (idx < 0)
            return;
        r->clients[idx].last_poll = time(NULL);
        if (r->status_reply)
            send_status(r, idx);
        return;
    }

    case DSRP_TYPE_HEADER:
    case DSRP_TYPE_HEADER_BUSY: {
        if (len < DSRP_HEADER_LEN)
            return;
        int idx = find_or_add_client(r, src, srclen);
        if (idx < 0)
            return;

        uint16_t id = dsrp_get_id(buf);

        if (r->talking && r->talker != idx) {
            log_dbg("header from idx %d ignored; idx %d holds the channel", idx, r->talker);
            return;
        }

        if (!r->talking) {
            char who[64];
            addr_str(src, who, sizeof who);
            log_msg("transmission started by %s (id %04X)", who, id);
            r->talking = true;
            r->talker  = idx;
        }
        r->session_id    = id;
        r->last_frame_ms = now_ms();

        relay(r, buf, len, idx, DSRP_TYPE_HEADER);
        return;
    }

    case DSRP_TYPE_DATA:
    case DSRP_TYPE_DATA_BUSY: {
        if (len < DSRP_DATA_LEN)
            return;
        int idx = find_client(r, src);
        if (idx < 0)
            return;

        uint16_t id = dsrp_get_id(buf);
        if (!r->talking || r->talker != idx || r->session_id != id)
            return;   /* not the active stream */

        r->last_frame_ms = now_ms();
        bool eot = (buf[DSRP_OFF_SEQ] & DSRP_SEQ_EOT) != 0U;

        relay(r, buf, len, idx, DSRP_TYPE_DATA);

        if (eot)
            release_talker(r, "end of transmission");
        return;
    }

    case DSRP_TYPE_STATUS:
    case DSRP_TYPE_DD_DATA:
        /* Not expected from a repeater; ignore. */
        return;

    default:
        log_dbg("unknown DSRP type 0x%02X (%zu bytes)", buf[DSRP_OFF_TYPE], len);
        return;
    }
}

void reflector_tick(reflector_t *r)
{
    time_t now = time(NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!r->clients[i].used)
            continue;
        if (now - r->clients[i].last_poll > r->client_timeout_s) {
            char who[64];
            addr_str(&r->clients[i].addr, who, sizeof who);
            log_msg("repeater timed out: %s", who);
            if (r->talking && r->talker == i)
                release_talker(r, "talker timed out");
            r->clients[i].used = false;
        }
    }

    if (r->talking && now_ms() - r->last_frame_ms > r->talker_timeout_ms)
        release_talker(r, "frame watchdog");
}
