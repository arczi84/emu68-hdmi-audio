/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMI_AHI_PACING_H
#define HDMI_AHI_PACING_H
#include "pcm.h"
/* AHI supplies BuffSamples after each Player hook. Pace those blocks by
 * their actual sample count, independently of the hardware DMA block. */
struct pcm_pacer {
    uint32_t next_us, fraction;
    unsigned filled;
};
static inline void pcm_pacer_start(struct pcm_pacer *p,uint32_t now)
{
    p->next_us=now; p->fraction=0; p->filled=0;
}
/* Resume after a scheduling gap without bursting through all missed mixer
 * deadlines. Keep queued PCM and its IEC958 phase; account for the part of
 * an AHI block that has not yet been copied into the staging buffer. */
static inline void pcm_pacer_rebase(struct pcm_pacer *p,
                                    const struct pcm_cursor *c,uint32_t now)
{
    uint32_t interval=(c->count-c->position)*125u;
    p->next_us=now+interval/6u;
    p->fraction=interval%6u;
}
static inline int pcm_pacer_pump(struct pcm_pacer *p,struct pcm_cursor *c,
                                uint8_t *out,unsigned capacity,uint32_t now,
                                pcm_provider provider,void *context)
{
    while(p->filled<capacity) {
        unsigned n;
        if(c->position==c->count) {
            uint32_t interval;
            if((int32_t)(now-p->next_us)<0) break;
            if(!provider(context,&c->samples,&c->count) || !c->samples || !c->count)
                return 0;
            c->position=0;
            /* 1 frame at 48 kHz is 125/6 us. Keep fractional time instead
             * of accumulating rounding error for varying AHI block sizes. */
            interval=c->count*125u+p->fraction;
            p->next_us+=interval/6u;
            p->fraction=interval%6u;
        }
        n=c->count-c->position;
        if(n>capacity-p->filled) n=capacity-p->filled;
        if(!pcm_fill(c,out+p->filled*8u,n,provider,context)) return 0;
        p->filled+=n;
    }
    return 1;
}
static inline void pcm_silence(uint8_t *out,unsigned frames)
{
    unsigned i;
    for(i=0;i<frames;++i) {
        uint32_t sample=iec958(0,i%192u);
        dma_put_le32(out+i*8u,sample);
        dma_put_le32(out+i*8u+4,sample);
    }
}
#endif
