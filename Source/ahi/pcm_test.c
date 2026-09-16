#include <assert.h>
#include <stdio.h>
#include "pcm.h"
struct source { unsigned frame,call; int16_t samples[4096]; int fail; };
static int16_t value(unsigned frame,unsigned channel) {
    const int16_t special[]={-32768,32767,-1,0,1,12345,-12345};
    return special[(frame*3+channel)%7];
}
static int supply(void *context,const void **samples,unsigned *count) {
    struct source *s=context;
    const unsigned sizes[]={1,80,959,960,961,2048,192};
    unsigned i;
    if(s->fail) return 0;
    *count=sizes[s->call++%7];
    for(i=0;i<*count;++i,++s->frame) {
        s->samples[i*2]=value(s->frame,0);
        s->samples[i*2+1]=value(s->frame,1);
    }
    *samples=s->samples;
    return 1;
}
static uint32_t read_le(const uint8_t *p) {
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static const int32_t wide_values[]={
    INT32_MIN,INT32_MAX,-1,0,1,255,256,-256,-257,
    0x12345678,-0x12345678,0x00000100,0x00008000
};
/* Independent expected payloads, including sub-16-bit detail and negatives. */
static const uint32_t wide_payloads[]={
    0x800000,0x7fffff,0xffffff,0,0,0,1,0xffffff,0xfffffe,
    0x123456,0xedcba9,1,0x80
};
struct wide_source { unsigned frame,call; int32_t samples[4096]; };
static int supply_wide(void *context,const void **samples,unsigned *count) {
    struct wide_source *s=context;
    const unsigned sizes[]={1,80,959,960,961,2048,192};
    unsigned i;
    *count=sizes[s->call++%7];
    for(i=0;i<*count;++i,++s->frame) {
        s->samples[i*2]=wide_values[(s->frame*3)%13];
        s->samples[i*2+1]=wide_values[(s->frame*3+1)%13];
    }
    *samples=s->samples;
    return 1;
}
static void test_hifi(void) {
    static uint8_t guarded[4608*8+16];
    static struct wide_source src;
    struct pcm_cursor cursor={0};
    unsigned block,i,ch,frame=0;
    cursor.hifi=1;
    for(i=0;i<sizeof(guarded);++i) guarded[i]=0xa7;
    for(block=0;block<17;++block) {
        unsigned frames=block&1u?960u:4608u;
        assert(pcm_fill(&cursor,guarded+8,frames,supply_wide,&src));
        for(i=0;i<frames;++i,++frame) for(ch=0;ch<2;++ch) {
            uint32_t word=read_le(guarded+8+i*8+ch*4), parity=0;
            unsigned bit;
            assert(((word>>4)&0xffffff)==wide_payloads[(frame*3+ch)%13]);
            /* IEC958 parity covers payload and V/U/C/P, not preamble. */
            for(bit=4;bit<32;++bit) parity^=(word>>bit)&1u;
            assert(parity==0);
            assert((word&15u)==(frame%192==0?15u:0u));
        }
        for(i=0;i<8;++i) {
            assert(guarded[i]==0xa7);
            assert(guarded[sizeof(guarded)-8+i]==0xa7);
        }
    }
    assert(src.frame-cursor.count+cursor.position==frame);
}
int main(void) {
    static uint8_t output[4608*8];
    static struct source src;
    struct pcm_cursor cursor={0};
    unsigned block,i,frame=0;
    for(block=0;block<17;++block) {
        unsigned frames=block&1u?960u:4608u;
        assert(pcm_fill(&cursor,output,frames,supply,&src));
        for(i=0;i<frames;++i,++frame) {
            assert(read_le(output+i*8)==iec958((int32_t)value(frame,0)*256,frame%192));
            assert(read_le(output+i*8+4)==iec958((int32_t)value(frame,1)*256,frame%192));
        }
    }
    assert(src.frame-cursor.count+cursor.position==frame);
    cursor.position=cursor.count; src.fail=1;
    assert(!pcm_fill(&cursor,output,1,supply,&src));
    test_hifi();
    puts("PASS: S16S and S32S across DMA boundaries, 24-bit detail/sign/extremes, stereo, IEC958 parity/continuity, guards and provider failure.");
    return 0;
}
