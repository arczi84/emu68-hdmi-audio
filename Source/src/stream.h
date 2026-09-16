/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMITEST_STREAM_H
#define HDMITEST_STREAM_H
#include <stdint.h>
#include "audio.h"
#include "tone.h"
#include "dma_plan.h"

#define STREAM_RATE 48000u
#define STREAM_SECONDS 12u
#define STREAM_TOTAL_FRAMES (STREAM_SECONDS * STREAM_RATE)
#ifndef STREAM_BUFFER_FRAMES
#define STREAM_BUFFER_FRAMES 4608u /* 24 complete IEC958 blocks, 96 ms */
#endif
#define STREAM_BUFFER_BYTES (STREAM_BUFFER_FRAMES * 8u)
#define STREAM_PERIOD_US (STREAM_BUFFER_FRAMES * 125u / 6u)
#define STREAM_DATA_OFFSET 64u
#define STREAM_PLAN_BYTES (STREAM_DATA_OFFSET + 2u*STREAM_BUFFER_BYTES)
#ifndef STREAM_COPY_GUARD_BYTES
#define STREAM_COPY_GUARD_BYTES 7680u /* at least 20 ms of the active buffer */
#endif
#ifndef STREAM_PUBLISH_LIMIT_US
#define STREAM_PUBLISH_LIMIT_US 20000u
#endif

struct stream_source { uint32_t frame, phase; };

/* Phase-continuous stereo test source: 500, 750, 1000, 1500 Hz, one second
 * each, three repetitions. Beyond 12 seconds the producer supplies silence. */
static inline void stream_fill(uint8_t *out, struct stream_source *source)
{
    static const uint32_t steps[4] = {32768, 49152, 65536, 98304};
    unsigned i;
    for (i = 0; i < STREAM_BUFFER_FRAMES; ++i) {
        int32_t sample = 0;
        if (source->frame < STREAM_TOTAL_FRAMES) {
            sample = (int32_t)sine48[source->phase >> 16] * 256;
            source->phase += steps[(source->frame / STREAM_RATE) % 4];
            if (source->phase >= 48u*65536u) source->phase -= 48u*65536u;
        }
        {
            uint32_t packed = iec958(sample, source->frame % 192u);
            dma_put_le32(out + i*8u, packed);
            dma_put_le32(out + i*8u+4, packed);
        }
        ++source->frame;
    }
}

static inline int stream_build_ring(uint8_t *p, uint32_t physical)
{
    unsigned i, word;
    if ((physical & 63) || physical < 0x08000000u ||
        physical > 0x40000000u-STREAM_PLAN_BYTES) return 0;
    for (i = 0; i < 2; ++i) {
        uint8_t *cb = p+i*32u;
        for (word = 0; word < 8; ++word) dma_put_le32(cb+word*4u, 0);
        dma_put_le32(cb, DMA_AUDIO_TI);
        dma_put_le32(cb+4, (physical+STREAM_DATA_OFFSET+i*STREAM_BUFFER_BYTES) | 0xc0000000u);
        dma_put_le32(cb+8, DMA_HDMI0_FIFO_BUS);
        dma_put_le32(cb+12, STREAM_BUFFER_BYTES);
        dma_put_le32(cb+20, (physical+(i^1u)*32u) | 0xc0000000u);
    }
    return 1;
}

static inline int stream_active_buffer(uint32_t current_cb, uint32_t base_bus)
{
    if (current_cb == base_bus) return 0;
    if (current_cb == base_bus+32u) return 1;
    return -1;
}

static inline int stream_can_publish(uint32_t current_cb, uint32_t base_bus,
                              unsigned target, uint32_t remaining)
{
    int active = stream_active_buffer(current_cb, base_bus);
    return target < 2 && active >= 0 && (unsigned)active != target &&
           remaining >= STREAM_COPY_GUARD_BYTES && remaining <= STREAM_BUFFER_BYTES;
}

/* With two alternating CBs, a gap of one period can hide a transition.
 * Stop conservatively rather than guessing ownership from a stale snapshot. */
static inline int stream_observation_safe(uint32_t previous, uint32_t now)
{
    return (uint32_t)(now-previous) < STREAM_PERIOD_US;
}
#endif
