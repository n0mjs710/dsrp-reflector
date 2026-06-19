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

static int count_clients(const reflector_t *r)
{
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        n += r->clients[i].used ? 1 : 0;
    return n;
}

static int find_client(const reflector_t *r, const struct sockaddr_storage *src)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (r->clients[i].used && addr_eq(&r->clients[i].addr, src))
            return i;
    return -1;
}

/* Return index of the client at src, adding it if new. Sets *added when a new
 * entry was created. Returns -1 if the table is full. */
static int find_or_add_client(reflector_t *r, const struct sockaddr_storage *src,
                              socklen_t srclen, bool *added)
{
    *added = false;

    int idx = find_client(r, src);
    if (idx >= 0)
        return idx;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!r->clients[i].used) {
            memset(&r->clients[i], 0, sizeof r->clients[i]);
            r->clients[i].used      = true;
            r->clients[i].addr      = *src;
            r->clients[i].addrlen   = srclen;
            r->clients[i].last_poll = time(NULL);
            *added = true;
            return i;
        }
    }

    char who[64];
    addr_str(src, who, sizeof who);
    log_err("client table full (%d); rejecting %s", MAX_CLIENTS, who);
    return -1;
}

/* Copy a fixed-width callsign field into a trimmed, sanitized C string. */
static void callsign_str(char *out, size_t outsz, const unsigned char *src, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < outsz; i++) {
        unsigned char c = src[i];
        if (c == 0)
            break;   /* never valid in a callsign; treat as terminator */
        out[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
}

/* Extract the NUL-terminated version text from a poll packet (text at offset 5). */
static void extract_poll_text(const unsigned char *buf, size_t len,
                              char *out, size_t outsz)
{
    size_t avail = (len > 6U) ? len - 6U : 0U;   /* total = 6 + textlen */
    size_t n = 0;
    for (size_t i = 0; i < avail && n + 1 < outsz; i++) {
        unsigned char c = buf[5U + i];
        if (c == 0)
            break;
        out[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    out[n] = '\0';
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

    double secs = (double)r->talk_frames * (double)DSTAR_FRAME_TIME_MS / 1000.0;
    log_msg("TX end: %s -> %s | %.1fs, %u frames (%s)",
            r->talk_mycall, r->talk_urcall, secs, r->talk_frames, reason);

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
    r->status_interval_s = cfg->status_interval_s;

    pad_field(r->status_text, DSRP_STATUS_TEXT_LEN, cfg->status_text);
    pad_field(r->refl_call, DSRP_CALLSIGN_LEN, cfg->callsign);
}

void reflector_handle(reflector_t *r, unsigned char *buf, size_t len,
                      const struct sockaddr_storage *src, socklen_t srclen)
{
    if (len < 5U || memcmp(buf, DSRP_TAG, DSRP_TAG_LEN) != 0)
        return;

    char who[64];
    addr_str(src, who, sizeof who);

    switch (buf[DSRP_OFF_TYPE]) {
    case DSRP_TYPE_POLL: {
        bool added = false;
        int idx = find_or_add_client(r, src, srclen, &added);
        if (idx < 0)
            return;
        r->clients[idx].last_poll = time(NULL);
        extract_poll_text(buf, len, r->clients[idx].info, sizeof r->clients[idx].info);
        if (added)
            log_msg("repeater connected: %s [%s] (%d connected)",
                    who, r->clients[idx].info, count_clients(r));

        /* Reply on connect, then only every status_interval_s (0 = never again).
         * MMDVMHost logs every status packet, so per-poll replies spam its log. */
        if (r->status_reply) {
            int64_t now = now_ms();
            bool due = added ||
                       (r->status_interval_s > 0 &&
                        now - r->clients[idx].status_sent_ms >= (int64_t)r->status_interval_s * 1000);
            if (due) {
                send_status(r, idx);
                r->clients[idx].status_sent_ms = now;
            }
        }
        return;
    }

    case DSRP_TYPE_HEADER:
    case DSRP_TYPE_HEADER_BUSY: {
        if (len < DSRP_HEADER_LEN)
            return;
        bool added = false;
        int idx = find_or_add_client(r, src, srclen, &added);
        if (idx < 0)
            return;
        if (added)
            log_msg("repeater connected: %s (%d connected)", who, count_clients(r));

        uint16_t id = dsrp_get_id(buf);

        if (r->talking && r->talker != idx) {
            log_dbg("header from %s (id %04X) ignored; %s holds the channel",
                    who, id, r->clients[r->talker].call);
            return;
        }

        /* Parse callsigns from the embedded 41-byte D-Star header. */
        const unsigned char *h = buf + DSRP_OFF_HEADER;
        char my1[12], my2[8], ur[12], r1[12], r2[12];
        callsign_str(my1, sizeof my1, h + DSTAR_HDR_MYCALL1, DSTAR_LONG_CALLSIGN_LEN);
        callsign_str(my2, sizeof my2, h + DSTAR_HDR_MYCALL2, DSTAR_SHORT_CALLSIGN_LEN);
        callsign_str(ur,  sizeof ur,  h + DSTAR_HDR_URCALL,  DSTAR_LONG_CALLSIGN_LEN);
        callsign_str(r1,  sizeof r1,  h + DSTAR_HDR_RPT1,    DSTAR_LONG_CALLSIGN_LEN);
        callsign_str(r2,  sizeof r2,  h + DSTAR_HDR_RPT2,    DSTAR_LONG_CALLSIGN_LEN);

        if (!r->talking) {
            r->talking       = true;
            r->talker        = idx;
            r->talk_start_ms = now_ms();
            r->talk_frames   = 0U;
            snprintf(r->talk_mycall, sizeof r->talk_mycall, "%s", my1);
            snprintf(r->talk_urcall, sizeof r->talk_urcall, "%s", ur);

            snprintf(r->clients[idx].call, sizeof r->clients[idx].call, "%s", my1);
            r->clients[idx].have_call = true;

            log_msg("TX start: %s/%s -> %s via %s/%s | %s id=%04X -> %d listener(s)",
                    my1, my2, ur, r1, r2, who, id, count_clients(r) - 1);
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
        r->talk_frames++;
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
            if (r->talking && r->talker == i)
                release_talker(r, "talker timed out");
            r->clients[i].used = false;

            char who[64];
            addr_str(&r->clients[i].addr, who, sizeof who);
            if (r->clients[i].have_call)
                log_msg("repeater disconnected (timeout): %s [%s] (%d connected)",
                        who, r->clients[i].call, count_clients(r));
            else
                log_msg("repeater disconnected (timeout): %s (%d connected)",
                        who, count_clients(r));
        }
    }

    if (r->talking && now_ms() - r->last_frame_ms > r->talker_timeout_ms)
        release_talker(r, "frame watchdog");
}
