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

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static int s_verbose = 0;

void log_init(int verbose)
{
    s_verbose = verbose;
}

static void emit(FILE *out, const char *level, const char *fmt, va_list ap)
{
    char ts[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(out, "%s %s ", ts, level);
    vfprintf(out, fmt, ap);
    fputc('\n', out);
    fflush(out);
}

void log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(stdout, "I:", fmt, ap);
    va_end(ap);
}

void log_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(stderr, "E:", fmt, ap);
    va_end(ap);
}

void log_dbg(const char *fmt, ...)
{
    if (!s_verbose)
        return;
    va_list ap;
    va_start(ap, fmt);
    emit(stdout, "D:", fmt, ap);
    va_end(ap);
}
