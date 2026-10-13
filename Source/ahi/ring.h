/* SPDX-License-Identifier: GPL-3.0-or-later
 * Interrupt-driven DMA ring for the AHI subdriver.
 * BCM2711 legacy DMA layout based on Circle dmacommon.h/dmachannel.cpp,
 * Copyright (C) 2014-2025 R. Stange.
 *
 * The ring has `segs` segments of `seg_frames` IEC958 frames. Every control
 * block raises an interrupt when its segment has been transferred, so the
 * handler only has to count completed segments. Ownership is then pure
 * arithmetic: no control block address is read back and no time stamp is
 * needed to know which segment the CPU may write.
 *
 *   completed  segments finished by the DMA (incremented by the interrupt)
 *   written    segments refilled by the CPU
 *
 * Segment (written % segs) is free once completed > written, and must be
 * finished before the DMA wraps back to it, i.e. before completed reaches
 * written + segs.
 *
 * Two geometries ship: the long ring of 4.16 (2 x 96 ms, for music and
 * background playback) and a low latency ring (3 x 16 ms).
 */
#ifndef HDMI_AHI_RING_H
#define HDMI_AHI_RING_H
#include <stdint.h>
#include "../src/dma_plan.h"
#include "../src/dma_control.h"

struct ring_geom { unsigned segs, seg_frames; };

#ifndef RING_LONG_SEG_FRAMES
#define RING_LONG_SEG_FRAMES 4608u /* 24 IEC958 blocks, 96 ms */
#endif
#ifndef RING_LONG_SEGS
#define RING_LONG_SEGS 2u
#endif
#ifndef RING_LOW_SEG_FRAMES
#define RING_LOW_SEG_FRAMES 768u /* 4 IEC958 blocks, 16 ms */
#endif
#ifndef RING_LOW_SEGS
#define RING_LOW_SEGS 3u
#endif
#if (RING_LONG_SEG_FRAMES % 192u) || (RING_LOW_SEG_FRAMES % 192u) || RING_LONG_SEGS < 2u || RING_LOW_SEGS < 2u
#error "segments must be whole IEC958 blocks and a ring needs 2+ segments"
#endif
#define RING_MAX_SEGS 16u

/* Same transfer as the polled driver, plus INTEN (TI bit 0). */
#define RING_TI (DMA_AUDIO_TI | 1u)

static inline unsigned ring_seg_bytes(const struct ring_geom *g) { return g->seg_frames * 8u; }
static inline unsigned ring_data_offset(const struct ring_geom *g)
{
    return ((g->segs * DMA_CB_BYTES) + 63u) & ~63u;
}
static inline unsigned ring_plan_bytes(const struct ring_geom *g)
{
    return ring_data_offset(g) + g->segs * ring_seg_bytes(g);
}
/* Total audio held by the ring, in frames (the latency is between segs-1 and segs segments). */
static inline unsigned ring_frames(const struct ring_geom *g) { return g->segs * g->seg_frames; }

static inline int ring_build(uint8_t *p, uint32_t physical, const struct ring_geom *g)
{
    unsigned i, word;
    if (g->segs < 2u || g->segs > RING_MAX_SEGS || (g->seg_frames % 192u) ||
        (physical & 63u) || physical < 0x08000000u ||
        physical > 0x40000000u - ring_plan_bytes(g)) return 0;
    for (i = 0; i < g->segs; ++i) {
        uint8_t *cb = p + i * DMA_CB_BYTES;
        for (word = 0; word < 8; ++word) dma_put_le32(cb + 4u * word, 0);
        dma_put_le32(cb, RING_TI);
        dma_put_le32(cb + 4, (physical + ring_data_offset(g) + i * ring_seg_bytes(g)) | 0xc0000000u);
        dma_put_le32(cb + 8, DMA_HDMI0_FIFO_BUS);
        dma_put_le32(cb + 12, ring_seg_bytes(g));
        dma_put_le32(cb + 20, (physical + ((i + 1u) % g->segs) * DMA_CB_BYTES) | 0xc0000000u);
    }
    return 1;
}

/* Segments the CPU may refill, or RING_LAPPED when the DMA has already
 * wrapped onto the segment that was due: the audio of the lapped segments
 * is lost and the CPU must skip ahead. Unsigned subtraction handles wrap. */
#define RING_LAPPED (-1)
static inline int ring_slack(uint32_t completed, uint32_t written, unsigned segs)
{
    uint32_t slack = completed - written;
    if (slack >= segs) return RING_LAPPED;
    return (int)slack;
}

/* After a lap, resume with the segment that plays next, which is at least
 * the remainder of the segment now playing away from the DMA. Returns the
 * number of segments of audio skipped (for the diagnostics). */
static inline uint32_t ring_resync(uint32_t completed, uint32_t *written, unsigned segs)
{
    uint32_t target = completed - (segs - 1u);
    uint32_t skipped = target - *written;
    *written = target;
    return skipped;
}
#endif
