/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "buffering.h"
#include "../src/stream.h"
#include "../src/dma_control.h"
typedef uintptr_t ULONG;
#define DMA_ReadFromRAM 1
#define TIMER 0x10u
#define BASE 0x1000u
#define BUS 0xc8001000u
static ULONG clock_us,cb,remaining,cs,elapsed;
static unsigned copies,forbidden,disabled,cache_bad,cross_cb,reads,change_read;
static void Forbid(void) { ++forbidden; }
static void Permit(void) { assert(forbidden); --forbidden; }
static void Disable(void) { ++disabled; }
static void Enable(void) { assert(disabled); --disabled; }
static ULONG rd(ULONG address)
{
    assert(forbidden && disabled);
    if(address==TIMER) return clock_us;
    if(address==BASE) return cs;
    if(address==BASE+0x14) return remaining;
    assert(address==BASE+4);
    if(++reads==change_read) cb=BUS+32;
    return cb;
}
static void CachePostDMA(void *p,ULONG *n,unsigned flags)
{ (void)p; assert(*n==STREAM_BUFFER_BYTES && flags==DMA_ReadFromRAM); }
static ULONG CachePreDMA(void *p,ULONG *n,unsigned flags)
{ (void)n; (void)flags; return (ULONG)p+cache_bad; }
static void *copy_pcm(void *dst,const void *src,size_t n)
{
    assert(disabled && forbidden); ++copies;
    clock_us+=elapsed;
    if(cross_cb) cb=BUS+32;
    return memcpy(dst,src,n);
}
#define memcpy copy_pcm
/* Generated directly from the production backend by the Makefile rule. */
#include "../build/ahi_publish_impl.h"
#undef memcpy
static void reset(void)
{
    clock_us=0; cb=BUS; remaining=STREAM_BUFFER_BYTES; cs=DMA_CS_ACTIVE;
    elapsed=1000; copies=forbidden=disabled=cache_bad=cross_cb=reads=change_read=0;
}
int main(void)
{
    static uint8_t ring[STREAM_PLAN_BYTES],staging[STREAM_BUFFER_BYTES];
    ULONG duration;
    memset(staging,0x5a,sizeof(staging));
    reset();
    assert(stream_publish(BASE,BUS,ring,0,staging,&duration)==2 && copies==0);
    reset(); remaining=STREAM_COPY_GUARD_BYTES-1;
    assert(stream_publish(BASE,BUS,ring,1,staging,&duration)==2 && copies==0);
    reset(); cs=0;
    assert(stream_publish(BASE,BUS,ring,1,staging,&duration)==2 && copies==0);
    reset(); change_read=2;
    assert(stream_publish(BASE,BUS,ring,1,staging,&duration)==2 && copies==0);
    reset();
    assert(stream_publish(BASE,BUS,ring,1,staging,&duration)==1 && copies==1);
    assert(!memcmp(ring+STREAM_DATA_OFFSET+STREAM_BUFFER_BYTES,staging,sizeof(staging)));
    assert(ring[STREAM_DATA_OFFSET]==0);
    reset(); cache_bad=1;
    assert(stream_publish(BASE,BUS,ring,1,staging,&duration)==-1 && copies==1);
    reset(); elapsed=STREAM_PUBLISH_LIMIT_US;
    assert(stream_publish(BASE,BUS,ring,1,staging,&duration)==0 && copies==1);
    reset(); cross_cb=1;
    assert(stream_publish(BASE,BUS,ring,1,staging,&duration)==0 && copies==1);
    assert(!disabled && !forbidden);
    puts("PASS: production publisher distinguishes no-write deferral, success, cache failure and post-copy deadline/ownership failure.");
    return 0;
}
