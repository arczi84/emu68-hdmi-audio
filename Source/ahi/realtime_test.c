#include <assert.h>
#include <stdio.h>
#define STREAM_BUFFER_FRAMES 192u
#define STREAM_COPY_GUARD_BYTES 384u
#define STREAM_PUBLISH_LIMIT_US 1000u
#include "../src/stream.h"
#include "pcm.h"
static uint32_t le32(const uint8_t *p) {
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
int main(void) {
    static uint8_t ring[STREAM_PLAN_BYTES];
    unsigned i;
    assert(STREAM_PERIOD_US==4000);
    assert(STREAM_PLAN_BYTES==3136);
    assert(stream_build_ring(ring,0x10000000));
    assert(le32(ring+12)==1536 && le32(ring+44)==1536);
    assert(le32(ring+20)==0xd0000020 && le32(ring+52)==0xd0000000);
    assert(stream_can_publish(0xd0000000,0xd0000000,1,384));
    assert(!stream_can_publish(0xd0000000,0xd0000000,1,383));
    assert(!stream_can_publish(0xd0000000,0xd0000000,0,1536));
    assert(stream_observation_safe(0xfffffff0u,0x00000f8fu));
    assert(!stream_observation_safe(0xfffffff0u,0x00000f90u));
    for(i=0;i<192;++i) {
        uint32_t packed=iec958(pcm_scale(32767,0),i);
        assert((packed&0x0ffffff0u)==0);
        dma_put_le32(ring+64+i*8,packed);
        assert(le32(ring+64+i*8)==iec958(0,i));
    }
    assert(le32(ring+64)!=0); /* IEC958 silence is not a bytewise zero buffer. */
    puts("PASS: 4 ms DMA ring, 1 ms copy guard, timer wrap/deadline, valid HDMI silence.");
    return 0;
}
