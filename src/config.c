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

#include "config.h"
#include "log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void config_defaults(config_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    snprintf(cfg->address, sizeof cfg->address, "%s", "0.0.0.0");
    snprintf(cfg->port, sizeof cfg->port, "%s", "20010");
    snprintf(cfg->callsign, sizeof cfg->callsign, "%s", "DSRP");
    snprintf(cfg->status_text, sizeof cfg->status_text, "%s", "DSRP Reflector");
    cfg->status_reply      = true;
    cfg->status_interval_s = 0;     /* on connect only — avoids 60s log spam */
    cfg->client_timeout_s  = 180;
    cfg->talker_timeout_ms = 2000;
    cfg->debug             = false;
}

/* Trim leading/trailing ASCII whitespace in place; returns s. */
static char *trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

/* Parse a boolean: 1/0, true/false, yes/no, on/off (case-insensitive). */
static bool parse_bool(const char *v, bool fallback)
{
    if (strcasecmp(v, "1") == 0 || strcasecmp(v, "true") == 0 ||
        strcasecmp(v, "yes") == 0 || strcasecmp(v, "on") == 0)
        return true;
    if (strcasecmp(v, "0") == 0 || strcasecmp(v, "false") == 0 ||
        strcasecmp(v, "no") == 0 || strcasecmp(v, "off") == 0)
        return false;
    return fallback;
}

static void copy_str(char *dst, size_t dstsz, const char *src)
{
    snprintf(dst, dstsz, "%s", src);
}

int config_load(config_t *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        log_err("cannot open config file %s", path);
        return -1;
    }

    char line[256];
    char section[32] = "";
    unsigned lineno = 0;

    while (fgets(line, sizeof line, f) != NULL) {
        lineno++;

        /* Strip comments (# or ;) and surrounding whitespace. */
        char *hash = strpbrk(line, "#;");
        if (hash != NULL)
            *hash = '\0';
        char *s = trim(line);
        if (*s == '\0')
            continue;

        if (*s == '[') {
            char *close = strchr(s, ']');
            if (close == NULL) {
                log_err("%s:%u: malformed section header", path, lineno);
                continue;
            }
            *close = '\0';
            copy_str(section, sizeof section, trim(s + 1));
            continue;
        }

        char *eq = strchr(s, '=');
        if (eq == NULL) {
            log_err("%s:%u: expected key=value", path, lineno);
            continue;
        }
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        bool known = false;

        if (strcasecmp(section, "Network") == 0) {
            if (strcasecmp(key, "Address") == 0) { copy_str(cfg->address, sizeof cfg->address, val); known = true; }
            else if (strcasecmp(key, "Port") == 0) { copy_str(cfg->port, sizeof cfg->port, val); known = true; }
        } else if (strcasecmp(section, "Reflector") == 0) {
            if (strcasecmp(key, "Callsign") == 0) { copy_str(cfg->callsign, sizeof cfg->callsign, val); known = true; }
            else if (strcasecmp(key, "StatusText") == 0) { copy_str(cfg->status_text, sizeof cfg->status_text, val); known = true; }
            else if (strcasecmp(key, "StatusReply") == 0) { cfg->status_reply = parse_bool(val, cfg->status_reply); known = true; }
            else if (strcasecmp(key, "StatusInterval") == 0) { cfg->status_interval_s = atoi(val); known = true; }
        } else if (strcasecmp(section, "Timing") == 0) {
            if (strcasecmp(key, "ClientTimeout") == 0) { cfg->client_timeout_s = atoi(val); known = true; }
            else if (strcasecmp(key, "TalkerTimeout") == 0) { cfg->talker_timeout_ms = atoi(val); known = true; }
        } else if (strcasecmp(section, "Log") == 0) {
            if (strcasecmp(key, "Debug") == 0) { cfg->debug = parse_bool(val, cfg->debug); known = true; }
        }

        if (!known)
            log_err("%s:%u: unknown key [%s] %s", path, lineno, section, key);
    }

    fclose(f);
    return 0;
}
