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

#ifndef LOG_H
#define LOG_H

/* Enable debug-level output when verbose is non-zero. */
void log_init(int verbose);

void log_msg(const char *fmt, ...);   /* informational */
void log_err(const char *fmt, ...);   /* errors */
void log_dbg(const char *fmt, ...);   /* only emitted when verbose */

#endif /* LOG_H */
