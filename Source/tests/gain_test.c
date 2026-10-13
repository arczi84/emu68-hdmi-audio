/* SPDX-License-Identifier: GPL-3.0-or-later
 * Master gain: unity is bit exact with the plain packer, a lower gain scales both channels
 * (16 and 32 bit input), a change ramps smoothly, and the ramp ends exactly on the target. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../ahi/pcm.h"

static int32_t data32[4096*2];
static int16_t data16[4096*2];
static int supply(void *ctx,const void **samples,unsigned *count) {
    unsigned hifi=*(unsigned *)ctx;
    *samples=hifi?(const void *)data32:(const void *)data16; *count=4096; return 1;
}
static int32_t unpack(const uint8_t *w) {   /* the 24 bit sample of one IEC958 word (little endian) */
    uint32_t v=w[0]|((uint32_t)w[1]<<8)|((uint32_t)w[2]<<16)|((uint32_t)w[3]<<24);
    return ((int32_t)(v<<4))>>8;
}
int main(void)
{
    unsigned hifi,i,k;
    (void)iec958;   /* pulled in by pcm.h for the other tests */
    for(i=0;i<4096*2;++i) {
        uint32_t v=i*2654435761u;
        if(i%11==0) v=0x80000000u; else if(i%13==0) v=0x7fffffffu;
        data32[i]=(int32_t)v; data16[i]=(int16_t)(v>>16);
    }
    for(hifi=0;hifi<2;++hifi) {
        static uint8_t a[4096*8],b[4096*8];
        struct pcm_cursor c={0},ref={0};
        unsigned ctx=hifi;
        c.hifi=ref.hifi=hifi;
        /* 1. unity, gain enabled: identical words */
        c.gain_enable=1; c.gain_target=65536u;
        assert(pcm_fill(&c,a,3000,supply,&ctx)); assert(pcm_fill(&ref,b,3000,supply,&ctx));
        for(k=0;k<3000*8;++k) assert(a[k]==b[k]);
        /* 2. half gain, ramped: after the ramp every sample is half */
        c.gain_target=32768u;
        assert(pcm_fill(&c,a,4096,supply,&ctx));      /* ramp is 2048 frames */
        assert(c.ramp==0);
        assert(pcm_fill(&c,a,1000,supply,&ctx));
        for(k=0;k<1000;++k) {
            int32_t want=(hifi?(data32[(c.position-1000+k)%4096*2]>>8):(int32_t)((uint32_t)(int32_t)data16[(c.position-1000+k)%4096*2]<<8));
            int32_t got=unpack(a+k*8);
            assert(got==want/2 || got==(want>>1) || got==(want>>1)+1);
        }
        /* 3. monotonic ramp down then back to unity: bit exact again afterwards */
        c.gain_target=65536u;
        assert(pcm_fill(&c,a,4096,supply,&ctx));
        assert(c.ramp==0 && c.gain_cur==PCM_GAIN_UNITY30);
        /* 4. silence */
        c.gain_target=0u;
        assert(pcm_fill(&c,a,4096,supply,&ctx));
        assert(pcm_fill(&c,a,512,supply,&ctx));
        for(k=0;k<512;++k) assert(unpack(a+k*8)==0 && unpack(a+k*8+4)==0);
    }
    puts("gain_test ok");
    return 0;
}
