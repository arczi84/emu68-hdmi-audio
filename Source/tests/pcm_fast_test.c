/* SPDX-License-Identifier: GPL-3.0-or-later
 * The fast packer must produce exactly the words of the reference iec958()
 * for every frame phase, both PCM formats, odd block sizes and extreme samples. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../ahi/pcm.h"

static uint32_t read_le(const uint8_t *p) {
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
struct src { int32_t data32[4096*2]; int16_t data16[4096*2]; unsigned call,hifi; };
static uint32_t mix(uint32_t x) { x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; x^=x>>16; return x; }
static int supply(void *ctx,const void **samples,unsigned *count) {
    static const unsigned sizes[]={1,7,40,41,191,192,193,959,960,961,2048};
    struct src *s=ctx; unsigned i,n=sizes[s->call%11];
    for(i=0;i<n*2;++i) {
        uint32_t v=mix(s->call*100003u+i);
        if(i%13==0) v=0x80000000u; else if(i%17==0) v=0x7fffffffu; else if(i%19==0) v=0;
        s->data32[i]=(int32_t)v; s->data16[i]=(int16_t)(v>>16);
    }
    ++s->call;
    *samples=s->hifi?(const void *)s->data32:(const void *)s->data16; *count=n; return 1;
}
int main(void)
{
    unsigned hifi,round;
    for(hifi=0;hifi<2;++hifi) {
        static struct src s0,s; s0.call=0; s0.hifi=hifi; s=s0;
        struct pcm_cursor c={0},ref={0};
        static uint8_t a[9216*8],b[9216*8];
        c.hifi=ref.hifi=hifi;
        for(round=0;round<40;++round) {
            unsigned frames=(round*977u)%4608u+1u,i;
            static struct src s2; s2=s; /* reference: same input stream */
            assert(pcm_fill(&c,a,frames,supply,&s));
            /* reference: per-sample iec958 on the same samples */
            {
                unsigned done=0;
                while(done<frames) {
                    unsigned n;
                    if(ref.position==ref.count) { assert(supply(&s2,&ref.samples,&ref.count)); ref.position=0; }
                    n=ref.count-ref.position; if(n>frames-done) n=frames-done;
                    for(i=0;i<n;++i,++ref.position,++done) {
                        dma_put_le32(b+done*8,iec958((int32_t)pcm_sample24(&ref,ref.position*2),ref.frame));
                        dma_put_le32(b+done*8+4,iec958((int32_t)pcm_sample24(&ref,ref.position*2+1),ref.frame));
                        ref.frame=(ref.frame+1)%192;
                    }
                }
            }
            for(i=0;i<frames;++i) {
                assert(read_le(a+i*8)==read_le(b+i*8));
                assert(read_le(a+i*8+4)==read_le(b+i*8+4));
            }
            assert(c.frame==ref.frame && c.position==ref.position);
        }
    }
    puts("pcm_fast_test ok");
    return 0;
}
