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

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

/* Runtime configuration. Populated with defaults, then optionally overlaid
 * from an INI file, then overlaid again by command-line flags. */
typedef struct {
    char address[64];        /* bind address                          */
    char port[16];           /* listen port                           */
    char callsign[16];       /* reflector callsign (<=8 chars used)   */
    char status_text[32];    /* status-reply display text (<=20 used) */
    bool status_reply;       /* reply with a link-status pkt          */
    int  status_interval_s;  /* resend status every N s (0 = on connect only) */
    int  client_timeout_s;   /* drop a repeater after this idle period */
    int  talker_timeout_ms;  /* release channel if frames stop        */
    int  roster_interval_s;  /* log connected repeaters every N s (0 = off) */
    bool debug;              /* verbose logging                       */
} config_t;

/* Fill cfg with built-in defaults. */
void config_defaults(config_t *cfg);

/* Overlay cfg from an INI file. Returns 0 on success, -1 if the file cannot
 * be opened. Unknown keys are logged and skipped. */
int config_load(config_t *cfg, const char *path);

#endif /* CONFIG_H */
