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
static inline int pcm_fill(struct pcm_cursor *c,uint8_t *out,unsigned frames,
                           pcm_provider provider,void *context)
{
    unsigned i;
    for(i=0;i<frames;++i) {
        if(c->position==c->count) {
            if(!provider(context,&c->samples,&c->count) || !c->samples || !c->count) return 0;
            c->position=0;
        }
        dma_put_le32(out+i*8,iec958(pcm_sample24(c,c->position*2),c->frame));
        dma_put_le32(out+i*8+4,iec958(pcm_sample24(c,c->position*2+1),c->frame));
        ++c->position; c->frame=(c->frame+1)%192;
    }
    return 1;
}
#endif
