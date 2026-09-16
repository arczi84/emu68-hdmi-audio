/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMI_AHI_GUARD_H
#define HDMI_AHI_GUARD_H
#include <stdint.h>
#define PCM_GUARD_BYTES 64u
static inline void pcm_guard_init(uint8_t *storage,unsigned bytes) {
    unsigned i;
    for(i=0;i<PCM_GUARD_BYTES;++i) storage[i]=storage[PCM_GUARD_BYTES+bytes+i]=0xa7;
}
static inline int pcm_guard_ok(const uint8_t *storage,unsigned bytes) {
    unsigned i;
    for(i=0;i<PCM_GUARD_BYTES;++i)
        if(storage[i]!=0xa7 || storage[PCM_GUARD_BYTES+bytes+i]!=0xa7) return 0;
    return 1;
}
#endif
