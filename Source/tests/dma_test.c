#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/dma_plan.h"
#include "../src/dma_control.h"

static uint32_t le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(void)
{
    static uint8_t plan[DMA_PLAN_BYTES];
    const uint32_t physical = 0x08010000u;
    uint32_t cb_address = physical | 0xc0000000u;
    unsigned visited = 0, frames = 0, stages[DMA_SAMPLE_BLOCKS] = {0};
    struct dma_tail_watch watch = {0, 0};
    uint32_t cs[8] = {0}, cb[8] = {0}, debug[8] = {0};
    memset(plan, 0xa5, sizeof(plan));
    assert(dma_build_chain(plan, physical));
    while (visited < DMA_CB_COUNT) {
        uint32_t offset = (cb_address & 0x3fffffffu)-physical;
        const uint8_t *block;
        uint32_t source, ti, bytes;
        unsigned stage;
        assert((cb_address & 31) == 0);
        assert(offset == visited * 32u && offset < DMA_CB_COUNT*32u);
        assert((cb_address & 0xc0000000u) == 0xc0000000u);
        block = plan + offset;
        ti = le(block); source = le(block+4); bytes = le(block+12);
        assert(((ti >> 16) & 31) == 10); /* HDMI0 DREQ */
        assert(ti == 0x000a0148u); /* no IRQ, no destination increment */
        assert(le(block+8) == 0x7ef2001cu);
        assert(bytes == 1536 && bytes <= 65535 && bytes % 8 == 0);
        assert(le(block+16) == 0 && le(block+24) == 0 && le(block+28) == 0);
        assert((source & 0xc0000000u) == 0xc0000000u);
        source = (source & 0x3fffffffu)-physical;
        assert(source >= DMA_SAMPLE_OFFSET && source+bytes <= sizeof(plan));
        assert((source-DMA_SAMPLE_OFFSET) % 1536 == 0);
        stage = (source-DMA_SAMPLE_OFFSET)/1536;
        assert(stage == (visited < 745 ? visited/250 : (visited < 750 ? 4u+visited-745u : 3u)));
        ++stages[stage]; frames += bytes/8;
        cb_address = le(block+20);
        assert(++visited <= DMA_CB_COUNT); /* only the final silence CB may cycle */
    }
    assert(visited == 751 && frames == 144192); /* 3s tone plus first silence block */
    assert(cb_address == ((physical + 750u*32u) | 0xc0000000u)); /* silence self-loop */
    assert(stages[0] == 250 && stages[1] == 250 && stages[2] == 245 && stages[3] == 1);
    for (unsigned i = 4; i < DMA_SAMPLE_BLOCKS; ++i) assert(stages[i] == 1);
    assert(DMA_PLAN_BYTES == 37888 && DMA_SAMPLE_OFFSET % 64 == 0);
    assert(!dma_build_chain(plan, physical+1));
    assert(!dma_build_chain(plan, 0x07000000));
    assert(!dma_build_chain(plan, 0x40000000));
    assert(!dma_build_chain(plan, 0x40000000-DMA_PLAN_BYTES+64));
    assert(dma_build_chain(plan, 0x40000000-DMA_PLAN_BYTES));
    assert(!dma_build_chain(plan, 0xfffffff0));

    assert(dma_choose_channel(0x71f5, cs, cb, debug) == 7);
    cs[7] = DMA_CS_ACTIVE;
    assert(dma_choose_channel(0x80, cs, cb, debug) == -1);
    assert(dma_choose_channel(0x71f5, cs, cb, debug) == 6);
    cs[7] = 0; cb[7] = 0xc8010000;
    assert(dma_choose_channel(0x80, cs, cb, debug) == -1);
    cb[7] = 0; cs[7] = DMA_CS_INT;
    assert(dma_choose_channel(0x80, cs, cb, debug) == -1);
    cs[7] = 0; debug[7] = 1;
    assert(dma_choose_channel(0x80, cs, cb, debug) == -1);
    debug[7] = 1u << 28; /* LITE feature bit is not an error */
    assert(dma_choose_channel(0x80, cs, cb, debug) == 7);
    assert(dma_choose_channel(0xff00, cs, cb, debug) == -1);
    assert(!dma_completed(DMA_CS_ACTIVE | DMA_CS_END, 0));
    assert(!dma_completed(DMA_CS_END | DMA_CS_ERROR, 0));
    assert(!dma_completed(DMA_CS_END | DMA_CS_OUTSTANDING, 0));
    assert(!dma_completed(DMA_CS_END, 1));
    assert(!dma_completed(0, 0));
    assert(dma_completed(DMA_CS_END, 1u << 28));
    {
        int32_t previous = 265216;
        for (unsigned i = 0; i < 960; ++i) {
            int32_t current = dma_fade_sample(265216, i);
            assert(current >= 0 && current <= previous);
            assert(dma_fade_sample(-265216, i) == -current);
            previous = current;
        }
        assert(dma_fade_sample(265216, 0) == 265216);
        assert(previous == 0 && dma_fade_sample(265216, 960) == 0);
    }
    assert(!dma_tail_ready(&watch, 100, 200, 100000));
    assert(!dma_tail_ready(&watch, 200, 200, 120000));
    assert(!dma_tail_ready(&watch, 200, 200, 319999));
    assert(dma_tail_ready(&watch, 200, 200, 320000));
    assert(!dma_tail_ready(&watch, 100, 200, 330000));
    assert(!dma_tail_ready(&watch, 200, 200, 0xffff0000u));
    assert(dma_tail_ready(&watch, 200, 200, 0xffff0000u + 200000u));
    puts("PASS: DMA chain, fade to zero, silence-only loop, timed shutdown, addresses and channel flags.");
    return 0;
}
