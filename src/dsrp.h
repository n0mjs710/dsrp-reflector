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

/*
 * DSRP — the private UDP protocol MMDVMHost speaks to its D-Star "gateway"
 * helper. The reflector replaces the gateway and relays these packets between
 * repeaters. Constants below are derived from MMDVMHost's DStarNetwork.cpp and
 * DStarGateway's HBRepeaterProtocolHandler.cpp. See DESIGN.md.
 */

#ifndef DSRP_H
#define DSRP_H

#include <stdint.h>

#define DSRP_TAG            "DSRP"
#define DSRP_TAG_LEN        4U

/* Packet type byte, at offset 4. */
#define DSRP_TYPE_STATUS        0x00U   /* refl -> rptr: link status text       */
#define DSRP_TYPE_POLL          0x0AU   /* rptr -> refl: keepalive poll w/ text */
#define DSRP_TYPE_HEADER        0x20U   /* header, sender idle                  */
#define DSRP_TYPE_DATA          0x21U   /* voice/data frame, sender idle        */
#define DSRP_TYPE_HEADER_BUSY   0x22U   /* header, sender busy                  */
#define DSRP_TYPE_DATA_BUSY     0x23U   /* voice/data frame, sender busy        */
#define DSRP_TYPE_DD_DATA       0x24U   /* DD data (ignored)                    */

/* Common offsets for header & data packets. */
#define DSRP_OFF_TYPE       4U
#define DSRP_OFF_ID         5U   /* 2 bytes, big-endian session id */
#define DSRP_OFF_SEQ        7U   /* data packets only              */

/* Wire sizes. */
#define DSRP_HEADER_LEN     49U  /* DSRP + type + id(2) + 0 + 41-byte header     */
#define DSRP_DATA_LEN       21U  /* DSRP + type + id(2) + seq + errors + 12B frame */

/* Sequence byte (data packets). */
#define DSRP_SEQ_EOT        0x40U /* end-of-transmission marker bit */
#define DSRP_SEQ_MASK       0x3FU

/* Link status (0x00) reply layout — total 34 bytes. */
#define DSRP_STATUS_LEN         34U
#define DSRP_OFF_STATUS_TEXT    5U   /* 20 chars, space-padded            */
#define DSRP_STATUS_TEXT_LEN    20U
#define DSRP_OFF_STATUS_CODE    25U  /* 1 byte, LINK_STATUS               */
#define DSRP_OFF_STATUS_REFL    26U  /* 8 chars, reflector callsign       */
#define DSRP_CALLSIGN_LEN       8U

/* LINK_STATUS values used by MMDVMHost (DStarDefines.h). */
#define DSTAR_LINK_NONE         0U
#define DSTAR_LINKED_LOOPBACK   7U

/* Read the big-endian 16-bit session id at offset 5. */
static inline uint16_t dsrp_get_id(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[DSRP_OFF_ID] << 8) | (uint16_t)p[DSRP_OFF_ID + 1U]);
}

#endif /* DSRP_H */
