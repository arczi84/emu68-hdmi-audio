/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMI_AHI_PCM_H
#define HDMI_AHI_PCM_H
#include "../src/audio.h"
#include "../src/dma_plan.h"
struct pcm_cursor {
    const void *samples;
    unsigned position, count, frame, hifi;
};
typedef int (*pcm_provider)(void *,const void **,unsigned *);

/* AHI S32S is signed, full-scale 32-bit PCM in native byte order. Preserve
 * its upper 24 bits for HDMI, including the eight bits below 16-bit PCM.
 * Shift unsigned: the IEC958 packer consumes the low 24 two's-complement
 * bits, so no implementation-defined signed shift or rounding overflow. */
static inline uint32_t pcm_sample24(const struct pcm_cursor *c,unsigned index)
{
    if(c->hifi) return (uint32_t)((const int32_t *)c->samples)[index] >> 8;
    return (uint32_t)((int32_t)((const int16_t *)c->samples)[index]*256);
}

/* Fast IEC958 packing (same words as iec958() in audio.h, which stays the
 * reference and is what the tests compare against).
 *
 * A subframe is (sample24 << 4) | C | P<<31 | preamble, where P makes the
 * parity of bits 4..30 even. Only frames 0..39 of each 192-frame block carry
 * channel-status bits and only frame 0 a preamble; from frame 40 on the word
 * is just the shifted sample plus its parity, which is a 3-lookup byte parity.
 * The word is stored little endian with one byte-swapped 32-bit write. */
#define PCM_P2(n) n,n^1,n^1,n
#define PCM_P4(n) PCM_P2(n),PCM_P2(n^1),PCM_P2(n^1),PCM_P2(n)
#define PCM_P6(n) PCM_P4(n),PCM_P4(n^1),PCM_P4(n^1),PCM_P4(n)
static const uint8_t pcm_par8[256]={PCM_P6(0),PCM_P6(1),PCM_P6(1),PCM_P6(0)};
/* Channel-status bit C of frames 0..39: bytes {4,0,0,2,0xdb} of IEC958. */
static const uint8_t pcm_status[40]={
    0,0,1,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,
    1,1,0,1,1,0,1,1};
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
#define PCM_PUT32(p,v) (*(uint32_t *)(p)=__builtin_bswap32(v))
#else
#define PCM_PUT32(p,v) dma_put_le32((p),(v))
#endif
static inline uint32_t pcm_parity24(uint32_t s)
{
    return pcm_par8[s&0xffu]^pcm_par8[(s>>8)&0xffu]^pcm_par8[(s>>16)&0xffu];
}
static inline uint32_t pcm_in24(const void *samples,unsigned index,unsigned hifi)
{
    if(hifi) return (uint32_t)((const int32_t *)samples)[index]>>8;
    return ((uint32_t)(int32_t)((const int16_t *)samples)[index]<<8)&0xffffffu;
}
/* Pack n frames of the cursor's current block starting at c->position. */
static inline void pcm_pack(struct pcm_cursor *c,uint8_t *out,unsigned n)
{
    const void *in=c->samples;
    unsigned hifi=c->hifi,pos=c->position,frame=c->frame,run;
    c->position=pos+n;
    while(n) {
        if(frame<40) {
            run=40-frame; if(run>n) run=n;
            for(n-=run;run;--run,++frame,++pos,out+=8) {
                uint32_t l=pcm_in24(in,pos*2,hifi),r=pcm_in24(in,pos*2+1,hifi);
                uint32_t cb=pcm_status[frame],pre=frame?0u:15u;
                PCM_PUT32(out,(l<<4)|(cb<<30)|pre|((pcm_parity24(l)^cb)<<31));
                PCM_PUT32(out+4,(r<<4)|(cb<<30)|pre|((pcm_parity24(r)^cb)<<31));
            }
        } else {
            run=192-frame; if(run>n) run=n;
            for(n-=run,frame+=run;run;--run,++pos,out+=8) {
                uint32_t l=pcm_in24(in,pos*2,hifi),r=pcm_in24(in,pos*2+1,hifi);
                PCM_PUT32(out,(l<<4)|(pcm_parity24(l)<<31));
                PCM_PUT32(out+4,(r<<4)|(pcm_parity24(r)<<31));
            }
        }
        if(frame==192) frame=0;
    }
    c->frame=frame;
}
static inline int pcm_fill(struct pcm_cursor *c,uint8_t *out,unsigned frames,
                           pcm_provider provider,void *context)
{
    while(frames) {
        unsigned n;
        if(c->position==c->count) {
            if(!provider(context,&c->samples,&c->count) || !c->samples || !c->count) return 0;
            c->position=0;
        }
        n=c->count-c->position;
        if(n>frames) n=frames;
        pcm_pack(c,out,n);
        out+=n*8u; frames-=n;
    }
    return 1;
}
#endif
