/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../ahi/ring.h"

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void check(const struct ring_geom *g)
{
    static uint8_t plan[4096 + 16 * 8192];
    uint32_t phys = 0x08000000u, written = 0, completed;
    unsigned i;

    assert(ring_plan_bytes(g) < sizeof(plan));
    assert(ring_build(plan, phys, g));
    assert(!ring_build(plan, phys + 4, g));
    assert(!ring_build(plan, 0x40000000u, g));
    for (i = 0; i < g->segs; ++i) {
        const uint8_t *cb = plan + i * DMA_CB_BYTES;
        assert(le32(cb) & 1u);                       /* INTEN */
        assert((le32(cb) & ~1u) == DMA_AUDIO_TI);
        assert(le32(cb + 4) == ((phys + ring_data_offset(g) + i * ring_seg_bytes(g)) | 0xc0000000u));
        assert(le32(cb + 8) == DMA_HDMI0_FIFO_BUS);
        assert(le32(cb + 12) == ring_seg_bytes(g));
        assert(le32(cb + 20) == ((phys + ((i + 1) % g->segs) * DMA_CB_BYTES) | 0xc0000000u));
    }
    assert(ring_data_offset(g) >= g->segs * DMA_CB_BYTES && !(ring_data_offset(g) & 63u));

    /* Steady state: one segment freed per interrupt, refilled in time. */
    for (completed = 0; completed < 1000; ++completed) {
        int s = ring_slack(completed, written, g->segs);
        assert(s == (completed > written ? 1 : 0));
        if (s) ++written;
    }
    /* Late service: the DMA wrapped onto the due segment. */
    written = 10;
    assert(ring_slack(10 + g->segs - 1, written, g->segs) == (int)g->segs - 1);
    assert(ring_slack(10 + g->segs, written, g->segs) == RING_LAPPED);
    completed = 10 + g->segs + 5;
    assert(ring_resync(completed, &written, g->segs) == 6);
    assert(ring_slack(completed, written, g->segs) == (int)g->segs - 1);
    /* 32-bit wrap of both counters. */
    written = 0xfffffffeu;
    assert(ring_slack(0xffffffffu, written, g->segs) == 1);
    assert(ring_slack(1u, written, g->segs) == (g->segs > 3 ? 3 : RING_LAPPED));
}

int main(void)
{
    const struct ring_geom geoms[] = {
        { RING_LONG_SEGS, RING_LONG_SEG_FRAMES }, { RING_LOW_SEGS, RING_LOW_SEG_FRAMES },
        { 8, 1152 }, { 16, 192 }, { 2, 192 }
    };
    struct ring_geom bad = { 1, 192 };
    unsigned i;

    for (i = 0; i < sizeof(geoms) / sizeof(geoms[0]); ++i) check(&geoms[i]);
    assert(!ring_build((uint8_t *)geoms, 0x08000000u, &bad));  /* needs 2+ segments */
    bad.segs = 2; bad.seg_frames = 100;
    assert(!ring_build((uint8_t *)geoms, 0x08000000u, &bad));  /* whole blocks only */
    puts("ring_test ok");
    return 0;
}
