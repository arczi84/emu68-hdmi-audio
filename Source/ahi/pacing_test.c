/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "buffering.h"
#include "../src/stream.h"
#include "pacing.h"
struct client {
    unsigned available,empty,calls,hifi;
    int16_t samples[960];
    int32_t wide[960];
};
static int supply(void *context,const void **samples,unsigned *count)
{
    struct client *s=context;
    unsigned i;
    ++s->calls;
    *count=480;
    for(i=0;i<480;++i) {
        int16_t value=0;
        if(s->available) { --s->available; value=12345; }
        else ++s->empty;
        s->samples[i*2]=value; s->samples[i*2+1]=-value;
        s->wide[i*2]=(int32_t)value*65536;
        s->wide[i*2+1]=-(int32_t)value*65536;
    }
    *samples=s->hifi?(const void *)s->wide:(const void *)s->samples;
    return 1;
}
static void paced(uint32_t base,unsigned hifi)
{
    static uint8_t staging[STREAM_BUFFER_BYTES];
    static struct client client;
    struct pcm_cursor cursor={0};
    struct pcm_pacer pacer;
    unsigned time,published=0;
    memset(&client,0,sizeof(client));
    client.hifi=hifi; cursor.hifi=hifi;
    pcm_pacer_start(&pacer,base);
    /* A client with ~93 ms of queued data, serviced at 50 FPS. Actual
     * pcm_pacer_pump drives the provider; a 96 ms DMA half is published
     * only once fully prepared. Client frames and DMA boundaries differ. */
    for(time=0;time<=1920000;time+=1000) {
        if(time%20000==0) client.available=4458;
        assert(pcm_pacer_pump(&pacer,&cursor,staging,STREAM_BUFFER_FRAMES,
                              base+time,supply,&client));
        if(time && time%STREAM_PERIOD_US==0) {
            assert(pacer.filled==STREAM_BUFFER_FRAMES);
            ++published; pacer.filled=0;
        }
        assert(!client.empty);
        assert(client.calls<=time/10000+1);
    }
    assert(published==20);
    assert(client.calls>=192);
}
int main(void)
{
    static uint8_t staging[STREAM_BUFFER_BYTES];
    static struct client legacy;
    struct pcm_cursor cursor={0};
    unsigned i;
    legacy.available=4458;
    for(i=0;i<3;++i)
        assert(pcm_fill(&cursor,staging,STREAM_BUFFER_FRAMES,supply,&legacy));
    assert(legacy.empty>0);
    paced(0,0);
    paced(0xffff0000u,0);
    paced(0,1);
    paced(0xffff0000u,1);
    pcm_silence(staging,STREAM_BUFFER_FRAMES);
    for(i=0;i<STREAM_BUFFER_FRAMES;++i) {
        uint8_t sample[4];
        dma_put_le32(sample,iec958(0,i%192));
        assert(!memcmp(staging+i*8,sample,4));
        assert(!memcmp(staging+i*8+4,sample,4));
    }
    puts("PASS: legacy prefill empties a 93 ms client queue; paced mixer keeps it fed across 96 ms DMA halves and timer wrap; valid startup silence.");
    return 0;
}
