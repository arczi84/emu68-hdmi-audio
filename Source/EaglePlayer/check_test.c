/* Test the actual binary CheckAmplifier tail, with AHI capability queries
 * supplied by a small fixture. No EaglePlayer GUI or hardware is emulated. */
#include <assert.h>
#include <stdio.h>
#include <proto/dos.h>
#include <devices/ahi.h>
extern LONG run_eagle_check(void *);
static ULONG min_hz,max_hz,calls;
ULONG eagle_best(const struct TagItem *tag)
{
    unsigned count=0,ok=1;
    ++calls;
    for(;tag->ti_Tag;++tag,++count) {
        switch(tag->ti_Tag) {
        case AHIDB_Panning: case AHIDB_Stereo: ok &= tag->ti_Data==1; break;
        case AHIDB_MaxChannels: ok &= tag->ti_Data<=128; break;
        case AHIDB_Bits: ok &= tag->ti_Data<=16; break;
        /* Same comparison direction as AHI 6.0 DizzyTestAudioID. */
        case AHIDB_MinMixFreq: ok &= tag->ti_Data>=min_hz; break;
        case AHIDB_MaxMixFreq: ok &= tag->ti_Data<=max_hz; break;
        default: assert(0);
        }
    }
    assert(count==6);
    return ok?0x001f0010UL:AHI_INVALID_ID;
}
static void check(const char *path,int patched)
{
    BPTR seg=LoadSeg((CONST_STRPTR)path);
    void *code;
    LONG result;
    if(!seg) { printf("LoadSeg failed: %s\n",path); PrintFault(IoErr(),(CONST_STRPTR)"LoadSeg"); }
    assert(seg);
    code=(ULONG *)BADDR(seg)+1;
    assert(*(ULONG *)code==0x600000e8UL);
    assert(*(ULONG *)((UBYTE *)code+0x79fc)==0x203a03c6UL);
    assert(*(UWORD *)((UBYTE *)code+0x4e)==(patched?20:19));
    calls=0;
    min_hz=max_hz=48000;
    result=run_eagle_check(code);
    printf("%s fixed 48000: result=%d expected=%d calls=%u\n",path,result,patched?0:-1,calls);
    assert(result==(patched?0:-1));
    min_hz=4410; max_hz=96000;
    assert(run_eagle_check(code)==0); /* UAE-like range remains accepted */
    min_hz=44100; max_hz=44100;
    assert(run_eagle_check(code)==(patched?0:-1));
    min_hz=8000; max_hz=28000;
    assert(run_eagle_check(code)==-1); /* Existing upper-rate check retained */
    assert(calls==4);
    UnLoadSeg(seg);
}
int main(void)
{
    check("build/eagle-ahi-original",0);
    check("build/eagle-ahi-patched",1);
    puts("PASS: actual EaglePlayer check rejects fixed 48 kHz before patch, accepts it after patch; UAE-like range still accepted, insufficient maximum still rejected.");
    return 0;
}
