/* SPDX-License-Identifier: GPL-3.0-or-later
 * Multi-client mixing (4.18): one client through pcm_mix + pcm_mix_finish packs to the same IEC958
 * words as pcm_fill of that client (16 and 32 bit, blocks crossing segment boundaries); two clients
 * add; four full scale clients saturate instead of wrapping; a stopped provider keeps its partial sum. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ahi/pcm.h"

#define BLOCK 1000u
static int32_t data32[BLOCK*2];
static int16_t data16[BLOCK*2];
struct src { unsigned hifi, left; int value; };
static int supply(void *ctx,const void **samples,unsigned *count) {
    unsigned hifi=*(unsigned *)ctx;
    *samples=hifi?(const void *)data32:(const void *)data16; *count=BLOCK; return 1;
}
static int32_t const32[BLOCK*2];
static int16_t const16[BLOCK*2];
static int supply_const(void *ctx,const void **samples,unsigned *count) {
    struct src *s=ctx;
    if(!s->left) return 0;
    --s->left;
    *samples=s->hifi?(const void *)const32:(const void *)const16; *count=BLOCK; return 1;
}
static int32_t unpack(const uint8_t *w) {
    uint32_t v=w[0]|((uint32_t)w[1]<<8)|((uint32_t)w[2]<<16)|((uint32_t)w[3]<<24);
    return ((int32_t)(v<<4))>>8;
}
int main(void)
{
    enum { SEG=768u, SEGS=7u };
    static uint8_t a[SEG*8],b[SEG*8];
    static int32_t acc[SEG*2];
    unsigned hifi,i,s,k;
    (void)iec958;
    for(i=0;i<BLOCK*2;++i) {
        uint32_t v=i*2654435761u;
        if(i%11==0) v=0x80000000u; else if(i%13==0) v=0x7fffffffu; else if(i%17==0) v=0;
        data32[i]=(int32_t)v; data16[i]=(int16_t)(v>>16);
    }
    /* 1. one client: bit exact with the direct packer, gain path included */
    for(hifi=0;hifi<2;++hifi) {
        struct pcm_cursor in={0},out={0},ref={0};
        unsigned ctx=hifi;
        in.hifi=ref.hifi=hifi; out.hifi=1;
        out.gain_enable=ref.gain_enable=1; out.gain_target=ref.gain_target=65536u;
        for(s=0;s<SEGS;++s) {
            if(s==3) out.gain_target=ref.gain_target=40000u;
            memset(acc,0,sizeof(acc));
            assert(pcm_mix(&in,acc,SEG,supply,&ctx));
            pcm_mix_finish(acc,SEG*2);
            out.samples=acc; out.position=0; out.count=SEG;
            pcm_pack(&out,a,SEG);
            assert(pcm_fill(&ref,b,SEG,supply,&ctx));
            for(k=0;k<SEG*8;++k) assert(a[k]==b[k]);
        }
    }
    /* 2. two clients (16 + 32 bit) add; 3. four full scale clients saturate */
    for(i=0;i<BLOCK*2;++i) { const16[i]=(int16_t)((i&1)?-12000:9000); const32[i]=(int32_t)((i&1)?-0x20000000:0x10000000); }
    {
        struct pcm_cursor c16={0},c32={0},out={0};
        struct src s16={0,10,0},s32={1,10,0};
        c32.hifi=1; out.hifi=1;
        memset(acc,0,sizeof(acc));
        assert(pcm_mix(&c16,acc,SEG,supply_const,&s16));
        assert(pcm_mix(&c32,acc,SEG,supply_const,&s32));
        pcm_mix_finish(acc,SEG*2);
        out.samples=acc; out.count=SEG;
        pcm_pack(&out,a,SEG);
        for(k=0;k<SEG;++k) {
            assert(unpack(a+k*8)==9000*256+0x100000);
            assert(unpack(a+k*8+4)==-12000*256-0x200000);
        }
    }
    for(i=0;i<BLOCK*2;++i) { const16[i]=(int16_t)((i&1)?-32768:32767); }
    {
        struct pcm_cursor c[PCM_MIX_MAX_CLIENTS],out={0};
        struct src src[PCM_MIX_MAX_CLIENTS];
        memset(c,0,sizeof(c)); out.hifi=1;
        memset(acc,0,sizeof(acc));
        for(i=0;i<PCM_MIX_MAX_CLIENTS;++i) { src[i].hifi=0; src[i].left=10; assert(pcm_mix(&c[i],acc,SEG,supply_const,&src[i])); }
        pcm_mix_finish(acc,SEG*2);
        out.samples=acc; out.count=SEG;
        pcm_pack(&out,a,SEG);
        for(k=0;k<SEG;++k) { assert(unpack(a+k*8)==0x7fffff); assert(unpack(a+k*8+4)==-0x800000); }
    }
    /* 4. a provider that stops mid segment: returns 0, the frames it gave stay summed */
    {
        struct pcm_cursor c16={0};
        struct src s16={0,1,0};
        for(i=0;i<BLOCK*2;++i) const16[i]=100;
        memset(acc,0,sizeof(acc));
        assert(pcm_mix(&c16,acc,SEG,supply_const,&s16));        /* 768 of the 1000 frames */
        memset(acc,0,sizeof(acc));
        assert(!pcm_mix(&c16,acc,SEG,supply_const,&s16));       /* 232 left, then the provider stops */
        for(k=0;k<(BLOCK-SEG)*2;++k) assert(acc[k]==100*256);
        for(;k<SEG*2;++k) assert(acc[k]==0);
    }
    puts("PASS: one client bit exact with direct packing (S16S/S32S, gain, block carry), two clients add, four saturate, partial stop.");
    return 0;
}
