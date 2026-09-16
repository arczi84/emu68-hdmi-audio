#include <assert.h>
#include <stdio.h>
#include "../src/audio.h"
#include "../src/tone.h"

int main(void)
{
    const uint8_t expected_status[] = {4, 0, 0, 2, 0xdb};
    const int32_t samples[] = {0, 1, -1, 8388607, -8388608, 123456, -123456};
    unsigned frame, s, bit;
    long sum = 0;
    for (frame = 0; frame < 192; ++frame) {
        for (s = 0; s < sizeof(samples)/sizeof(samples[0]); ++s) {
            uint32_t v = iec958(samples[s], frame);
            unsigned parity = 0;
            assert(((v >> 4) & 0xffffff) == ((uint32_t)samples[s] & 0xffffff));
            assert((v & 15) == (frame == 0 ? 15u : 0u));
            assert((v & 0x30000000) == 0); /* valid PCM, user bit clear */
            assert(((v >> 30) & 1) == (frame < 40 ?
                ((expected_status[frame/8] >> (frame%8)) & 1u) : 0u));
            for (bit = 4; bit < 32; ++bit) parity += (v >> bit) & 1;
            assert((parity & 1) == 0);
        }
    }
    for (s = 0; s < 48; ++s) {
        sum += sine48[s];
        assert(sine48[s] == -sine48[(s+24)%48]);
        assert(sine48[s] >= -1036 && sine48[s] <= 1036);
    }
    assert(sum == 0 && sine48[12] == 1036);
    puts("PASS: IEC958 payload, parity, status, preambles; 1 kHz tone symmetry and level.");
    return 0;
}
