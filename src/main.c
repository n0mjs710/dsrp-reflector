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
#include "reflector.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_PORT      "20010"
#define DEFAULT_BIND      "0.0.0.0"
#define DEFAULT_CALLSIGN  "DSRP"
#define RECV_BUFLEN       100
#define TICK_INTERVAL_MS  250

static volatile sig_atomic_t s_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    s_stop = 1;
}

/* Bind a UDP socket to host:port (IPv4 or IPv6). Returns fd or -1. */
static int open_socket(const char *host, const char *port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags    = AI_PASSIVE | AI_NUMERICSERV;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        log_err("getaddrinfo(%s:%s): %s", host, port, gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
        log_err("unable to bind %s:%s: %s", host, port, strerror(errno));
    return fd;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -b ADDR   bind address (default %s)\n"
        "  -p PORT   listen port (default %s)\n"
        "  -c CALL   reflector callsign in status replies (<=8 chars, default %s)\n"
        "  -v        verbose (debug logging)\n"
        "  -h        this help\n",
        argv0, DEFAULT_BIND, DEFAULT_PORT, DEFAULT_CALLSIGN);
}

int main(int argc, char **argv)
{
    const char *bind_addr = DEFAULT_BIND;
    const char *port      = DEFAULT_PORT;
    const char *callsign  = DEFAULT_CALLSIGN;
    int verbose = 0;

    int opt;
    while ((opt = getopt(argc, argv, "b:p:c:vh")) != -1) {
        switch (opt) {
        case 'b': bind_addr = optarg; break;
        case 'p': port      = optarg; break;
        case 'c': callsign  = optarg; break;
        case 'v': verbose   = 1;      break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 2;
        }
    }

    log_init(verbose);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    int sock = open_socket(bind_addr, port);
    if (sock < 0)
        return 1;

    reflector_t refl;
    reflector_init(&refl, sock, callsign);

    log_msg("dsrp-reflector listening on %s:%s as \"%.8s\"", bind_addr, port, callsign);

    struct pollfd pfd = { .fd = sock, .events = POLLIN };

    while (!s_stop) {
        int pr = poll(&pfd, 1, TICK_INTERVAL_MS);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            log_err("poll: %s", strerror(errno));
            break;
        }

        if (pr > 0 && (pfd.revents & POLLIN)) {
            unsigned char buf[RECV_BUFLEN];
            struct sockaddr_storage src;
            socklen_t srclen = sizeof src;
            ssize_t n = recvfrom(sock, buf, sizeof buf, 0,
                                 (struct sockaddr *)&src, &srclen);
            if (n > 0)
                reflector_handle(&refl, buf, (size_t)n, &src, srclen);
        }

        reflector_tick(&refl);
    }

    log_msg("shutting down");
    close(sock);
    return 0;
}
