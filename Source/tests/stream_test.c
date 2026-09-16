#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/stream.h"

static uint32_t le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(void)
{
    static uint8_t ring[STREAM_PLAN_BYTES], data[STREAM_BUFFER_BYTES];
    struct stream_source source = {0, 0};
    unsigned chunk, i, crossings[12] = {0};
    int last_sign = 0;
    const uint32_t physical = 0x09000000u, bus = 0xc9000000u;
    assert(stream_build_ring(ring, physical));
    for (i = 0; i < 2; ++i) {
        const uint8_t *cb = ring+i*32u;
        assert(le(cb) == 0x000a0148u); /* no IRQ, DREQ 10, source increment */
        assert(le(cb+4) == bus+64u+i*36864u);
        assert(le(cb+8) == 0x7ef2001cu);
        assert(le(cb+12) == 36864 && le(cb+12) < 65536);
        assert(le(cb+16) == 0 && le(cb+24) == 0 && le(cb+28) == 0);
        assert(le(cb+20) == bus+(i^1u)*32u);
        assert((le(cb+4) & 63u) == 0);
    }
    assert(STREAM_PLAN_BYTES == 73792);
    assert(!stream_build_ring(ring, physical+1));
    assert(!stream_build_ring(ring, 0x07000000));
    assert(!stream_build_ring(ring, 0x40000000));
    assert(stream_build_ring(ring, 0x40000000-STREAM_PLAN_BYTES));
    assert(!stream_build_ring(ring, 0x40000000-STREAM_PLAN_BYTES+64));
    assert(stream_active_buffer(bus, bus) == 0);
    assert(stream_active_buffer(bus+32, bus) == 1);
    assert(stream_active_buffer(0, bus) == -1);
    assert(stream_can_publish(bus, bus, 1, 36864));
    assert(stream_can_publish(bus+32, bus, 0, 7680));
    assert(!stream_can_publish(bus, bus, 0, 36864));
    assert(!stream_can_publish(bus, bus, 1, 7679));
    assert(!stream_can_publish(bus, bus, 1, 40000));
    assert(!stream_can_publish(bus, bus, 2, 36864));
    assert(!stream_can_publish(0, bus, 1, 36864));
    assert(stream_observation_safe(100, 96099));
    assert(!stream_observation_safe(100, 96100));
    assert(stream_observation_safe(0xfffffff0u, 0x100u));
    assert(!stream_observation_safe(100, 192100)); /* an entire hidden ring lap */

    for (chunk = 0; chunk < 128; ++chunk) {
        stream_fill(data, &source);
        assert(source.frame == (chunk+1u)*4608u);
        assert(source.phase < 48u*65536u);
        for (i = 0; i < STREAM_BUFFER_FRAMES; ++i) {
            uint32_t frame = chunk*4608u+i, packed = le(data+i*8u);
            uint32_t raw = (packed >> 4) & 0xffffffu;
            int32_t sample = raw & 0x800000u ? (int32_t)raw-0x1000000 : (int32_t)raw;
            assert(le(data+i*8u+4) == packed); /* stereo samples stay paired */
            assert(sample >= -265216 && sample <= 265216);
            assert((packed & 15u) == (frame % 192u == 0 ? 15u : 0u));
            if (frame < STREAM_TOTAL_FRAMES) {
                if (sample > 0 && last_sign <= 0) ++crossings[frame/48000u];
                if (sample) last_sign = sample > 0 ? 1 : -1;
            } else assert(sample == 0);
        }
    }
    for (i = 0; i < 12; ++i) {
        const unsigned expected[4] = {500, 750, 1000, 1500};
        assert(crossings[i] == expected[i%4]);
    }
    puts("PASS: two-buffer ring, ownership/deadline guards, continuous IEC958 sequence, 12 tone changes and trailing silence.");
    return 0;
}
