/* SPDX-License-Identifier: GPL-3.0-or-later
 * BCM2711 legacy DMA layout based on Circle dmacommon.h/dmachannel.cpp,
 * Copyright (C) 2014-2025 R. Stange. Test buffer layout is specific to HDMITest.
 */
#ifndef HDMITEST_DMA_PLAN_H
#define HDMITEST_DMA_PLAN_H
#include <stdint.h>

#define DMA_AUDIO_BLOCK_BYTES 1536u /* 192 IEC958 stereo frames, 4 ms */
#define DMA_TONE_BLOCKS 750u        /* 250 left, 250 right, 250 both */
#define DMA_TAIL_BLOCKS 1u          /* terminal silence block repeats until stop */
#define DMA_FADE_BLOCKS 5u          /* last 20 ms of the tone fade to zero */
#define DMA_SAMPLE_BLOCKS (4u + DMA_FADE_BLOCKS)
#define DMA_CB_COUNT (DMA_TONE_BLOCKS + DMA_TAIL_BLOCKS)
#define DMA_CB_BYTES 32u
#define DMA_SAMPLE_OFFSET ((DMA_CB_COUNT * DMA_CB_BYTES + 63u) & ~63u)
#define DMA_PLAN_BYTES (DMA_SAMPLE_OFFSET + DMA_SAMPLE_BLOCKS * DMA_AUDIO_BLOCK_BYTES)
#define DMA_HDMI0_FIFO_BUS 0x7ef2001cu
/* 32-bit source/destination, source increment, destination DREQ 10,
 * wait for write responses. No 2D mode, destination increment or interrupt. */
#define DMA_AUDIO_TI ((10u << 16) | (1u << 8) | (1u << 6) | (1u << 3))

static inline void dma_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Called only for the final five tone blocks. Keep the first sample at full
 * gain and make the final sample exactly zero, without a signed overflow. */
static inline int32_t dma_fade_sample(int32_t sample, unsigned frame)
{
    const unsigned last = DMA_FADE_BLOCKS * 192u - 1u;
    if (frame >= last) return 0;
    return sample * (int32_t)(last-frame) / (int32_t)last;
}

/* Physical addresses must be validated before applying the VideoCore alias. */
static inline int dma_build_chain(uint8_t *p, uint32_t physical)
{
    unsigned i, word;
    if ((physical & 63u) || physical < 0x08000000u ||
        physical > 0x40000000u - DMA_PLAN_BYTES) return 0;
    for (i = 0; i < DMA_CB_COUNT; ++i) {
        unsigned stage = i < DMA_TONE_BLOCKS ? i / 250u : 3u;
        if (i >= DMA_TONE_BLOCKS-DMA_FADE_BLOCKS && i < DMA_TONE_BLOCKS)
            stage = 4u + i - (DMA_TONE_BLOCKS-DMA_FADE_BLOCKS);
        uint8_t *cb = p + i * DMA_CB_BYTES;
        for (word = 0; word < 8; ++word) dma_put_le32(cb + 4u*word, 0);
        dma_put_le32(cb, DMA_AUDIO_TI);
        dma_put_le32(cb+4, (physical + DMA_SAMPLE_OFFSET +
                           stage * DMA_AUDIO_BLOCK_BYTES) | 0xc0000000u);
        dma_put_le32(cb+8, DMA_HDMI0_FIFO_BUS);
        dma_put_le32(cb+12, DMA_AUDIO_BLOCK_BYTES);
        if (i + 1 < DMA_CB_COUNT)
            dma_put_le32(cb+20, (physical + (i+1)*DMA_CB_BYTES) | 0xc0000000u);
        else
            dma_put_le32(cb+20, (physical + i*DMA_CB_BYTES) | 0xc0000000u);
    }
    return 1;
}
#endif
