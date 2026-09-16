/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMITEST_CLOCK_REPLY_H
#define HDMITEST_CLOCK_REPLY_H
#include <stdint.h>

/* Input is a complete 8-word property response in CPU byte order. */
static uint32_t clock_reply(const uint32_t *p, uint32_t tag)
{
    if (p[0] != 32 || p[1] != 0x80000000u || p[2] != tag ||
        p[3] != 8 || !(p[4] & 0x80000000u) ||
        (p[4] & 0x7fffffffu) < 8 || p[5] != 14 || p[7] != 0 ||
        p[6] < 1000000 || p[6] > 340000000)
        return 0;
    return p[6];
}
#endif
