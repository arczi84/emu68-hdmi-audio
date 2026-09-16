/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "buffering.h"
#include "watchdog.h"
#include "refill.h"
struct source {
    unsigned frames,calls,hifi;
    int16_t small[962];
    int32_t wide[962];
};
static int16_t value(unsigned frame) { return (int16_t)((int)(frame%30000)-15000); }
static int supply(void *context,const void **samples,unsigned *count)
{
    struct source *s=context;
    unsigned i;
    ++s->calls; *count=(s->calls&1u)?479u:481u;
    for(i=0;i<*count;++i,++s->frames) {
        int16_t v=value(s->frames);
        s->small[i*2]=v; s->small[i*2+1]=-v;
        s->wide[i*2]=(int32_t)v*65536;
        s->wide[i*2+1]=-(int32_t)v*65536;
    }
    *samples=s->hifi?(const void *)s->wide:(const void *)s->small;
    return 1;
}
static uint32_t le(const uint8_t *p)
{
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void run(unsigned pause_at,unsigned gap,uint32_t origin,unsigned hifi)
{
    static struct source source;
    static uint8_t staging[STREAM_BUFFER_BYTES];
    struct pcm_cursor cursor={0};
    struct pcm_pacer pacer;
    struct ahi_refill refill={0};
    unsigned t=0,previous=0,injected=0,published=0,resyncs=0,deferred=0,frames=0;
    const uint32_t bus=0xc8001000u;
    memset(&source,0,sizeof(source));
    source.hifi=cursor.hifi=hifi;
    pcm_pacer_start(&pacer,origin);
    while(t<1400000) {
        unsigned calls=source.calls,fault,remaining;
        int current;
        if(!injected && t>=pause_at) { t=previous+gap; injected=1; }
        current=(t/STREAM_PERIOD_US)&1;
        remaining=STREAM_BUFFER_BYTES-((t%STREAM_PERIOD_US)*48u/125u);
        fault=hdmi_observation_fault(origin+previous,origin+t,DMA_CS_ACTIVE,0);
        assert(!fault || fault==HDMI_FAULT_GAP);
        ahi_refill_observe(&refill,&pacer,&cursor,current,fault==HDMI_FAULT_GAP,origin+t);
        if(refill.pending && pacer.filled!=STREAM_BUFFER_FRAMES && !refill.resync)
            ahi_refill_defer(&refill,&pacer,&cursor,origin+t);
        assert(pcm_pacer_pump(&pacer,&cursor,staging,STREAM_BUFFER_FRAMES,
                              origin+t,supply,&source));
        /* A late poll must not consume hundreds of ms of client audio at once. */
        assert(source.calls-calls<=1);
        if(refill.pending && pacer.filled==STREAM_BUFFER_FRAMES) {
            if(stream_can_publish(bus+32u*current,bus,(unsigned)current^1u,remaining)) {
                unsigned i;
                for(i=0;i<STREAM_BUFFER_FRAMES;++i,++frames) {
                    assert(le(staging+i*8)==iec958((int32_t)value(frames)*256,frames%192));
                    assert(le(staging+i*8+4)==iec958(-(int32_t)value(frames)*256,frames%192));
                }
                ++published; resyncs+=refill.resync;
                ahi_refill_published(&refill,&pacer,&cursor,origin+t);
            } else {
                ++deferred;
                ahi_refill_defer(&refill,&pacer,&cursor,origin+t);
            }
        }
        previous=t; t+=1000;
    }
    assert(injected && resyncs>=1 && published>=8);
    assert(source.frames-cursor.count+cursor.position==frames+pacer.filled);
    (void)deferred;
}
int main(void)
{
    unsigned hifi;
    for(hifi=0;hifi<2;++hifi) {
        run(95000,183656,0,hifi); /* screenshot gap, less than one full lap */
        run(500000,183656,0xffff0000u,hifi);
        run(95000,384000,0,hifi); /* same CB after two complete missed laps */
        run(20000,200000,0,hifi); /* partially prepared buffer at the pause */
    }
    /* A hardware fault still overrides a simultaneous long poll gap. */
    assert(hdmi_observation_fault(0,183656,DMA_CS_ACTIVE|DMA_CS_ERROR,0)==HDMI_FAULT_DMA);
    assert(hdmi_observation_fault(0,183656,DMA_CS_ACTIVE,1)==HDMI_FAULT_DMA);
    assert(hdmi_observation_fault(0,183656,0,0)==HDMI_FAULT_DMA);
    puts("PASS: 183656 us gap, missed complete laps, partial staging, S16/S32, timer wrap, resumed safe refills without catch-up bursts or lost PCM; DMA errors remain fatal.");
    return 0;
}
