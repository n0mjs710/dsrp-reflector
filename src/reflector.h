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

#ifndef REFLECTOR_H
#define REFLECTOR_H

#include "config.h"
#include "dsrp.h"

#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>

#define MAX_CLIENTS 64

/* A connected repeater, identified by its source address. */
typedef struct {
    bool                    used;
    struct sockaddr_storage addr;
    socklen_t               addrlen;
    time_t                  last_poll;   /* wall-clock seconds of last poll */
    char                    info[40];    /* poll version text, e.g. linux_mmdvm-... */
    char                    call[12];    /* last-heard MYCALL1, trimmed */
    bool                    have_call;
    int64_t                 status_sent_ms; /* monotonic ms of last status reply */
} client_t;

typedef struct {
    int      sock;
    client_t clients[MAX_CLIENTS];

    /* Talker lock (party line — one repeater is heard at a time). */
    bool     talking;
    int      talker;          /* index into clients[], valid when talking */
    uint16_t session_id;      /* DSRP session id of the active over */
    int64_t  last_frame_ms;   /* monotonic ms of last relayed frame */
    int64_t  talk_start_ms;   /* monotonic ms the over started */
    uint32_t talk_frames;     /* voice/data frames relayed this over */
    char     talk_mycall[12]; /* MYCALL1 of the active over (for end log) */
    char     talk_urcall[12]; /* URCALL of the active over (for end log)  */

    /* Tunables. */
    int      client_timeout_s;   /* drop a repeater after this long with no poll */
    int      talker_timeout_ms;  /* release the lock if frames stop (lost EOT) */

    /* Status reply. */
    bool     status_reply;       /* whether to answer polls with a 0x00 packet */
    int      status_interval_s;  /* resend cadence; 0 = on connect only */
    char     status_text[DSRP_STATUS_TEXT_LEN];      /* display text, space-padded */
    char     refl_call[DSRP_CALLSIGN_LEN];           /* reflector callsign, space-padded */
} reflector_t;

/* Initialize from cfg. Strings are space-padded/truncated to the wire widths. */
void reflector_init(reflector_t *r, int sock, const config_t *cfg);

/* Handle one received UDP datagram from src. */
void reflector_handle(reflector_t *r, unsigned char *buf, size_t len,
                      const struct sockaddr_storage *src, socklen_t srclen);

/* Service timeouts (client expiry, talker watchdog). Call periodically. */
void reflector_tick(reflector_t *r);

#endif /* REFLECTOR_H */
