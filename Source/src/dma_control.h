/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMITEST_DMA_CONTROL_H
#define HDMITEST_DMA_CONTROL_H
#include <stdint.h>

#define DMA_CS_ACTIVE 1u
#define DMA_CS_END 2u
#define DMA_CS_INT 4u
#define DMA_CS_OUTSTANDING (1u << 6)
#define DMA_CS_ERROR (1u << 8)
#define DMA_CS_RESET (1u << 31)
#define DMA_CS_START ((1u << 28) | (15u << 20) | (1u << 16) | DMA_CS_ACTIVE)
#define DMA_CS_BUSY (DMA_CS_ACTIVE | DMA_CS_OUTSTANDING | DMA_CS_RESET)

/* Only unconfigured legacy channels 0..7, never DMA4 11..14. */
static inline int dma_choose_channel(uint32_t allowed, const uint32_t cs[8],
                              const uint32_t cb[8], const uint32_t debug[8])
{
    int i;
    for (i = 7; i >= 0; --i)
        if ((allowed & (1u << i)) && !(cs[i] & (DMA_CS_BUSY | DMA_CS_INT | DMA_CS_ERROR)) &&
            cb[i] == 0 && !(debug[i] & 7u)) return i;
    return -1;
}

/* END can latch at a control-block boundary: ACTIVE must also be clear. */
static inline int dma_completed(uint32_t cs, uint32_t debug)
{
    return !(cs & (DMA_CS_BUSY | DMA_CS_ERROR)) && (cs & DMA_CS_END) && !(debug & 7u);
}

struct dma_tail_watch { uint32_t since; int seen; };

/* Stop only after the hardware has entered the terminal silence descriptor
 * and has stayed there for 200 ms. Unsigned subtraction handles timer wrap. */
static inline int dma_tail_ready(struct dma_tail_watch *watch, uint32_t current,
                          uint32_t silence, uint32_t now)
{
    if (current != silence) { watch->seen = 0; return 0; }
    if (!watch->seen) { watch->seen = 1; watch->since = now; }
    return (uint32_t)(now-watch->since) >= 200000u;
}
#endif
