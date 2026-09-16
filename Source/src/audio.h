/* SPDX-License-Identifier: GPL-3.0-or-later
 * IEC958 packing adapted from Circle, Copyright (C) 2021-2024 R. Stange.
 */
#ifndef HDMITEST_AUDIO_H
#define HDMITEST_AUDIO_H
#include <stdint.h>

static uint32_t iec958(int32_t sample, unsigned frame)
{
    static const uint8_t status[5] = {4, 0, 0, 2, 0xdb};
    uint32_t v = ((uint32_t)sample & 0xffffffu) << 4;
    uint32_t parity;
    if (frame < 40 && (status[frame / 8] & (1u << (frame % 8))))
        v |= 0x40000000u;
    parity = v;
    parity ^= parity >> 16;
    parity ^= parity >> 8;
    parity ^= parity >> 4;
    parity ^= parity >> 2;
    parity ^= parity >> 1;
    v |= (parity & 1u) << 31;
    if (frame == 0) v |= 15;
    return v;
}
#endif
