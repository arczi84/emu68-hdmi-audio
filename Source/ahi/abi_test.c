#include <exec/resident.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/ahi_sub.h>
#include <stdio.h>
#include <assert.h>
#include "status.h"
extern LONG hdmi_status(struct Library *,ULONG);
extern struct Library *test_open(struct Library *);
extern BPTR test_close(struct Library *);
extern LONG test_attr(struct Library *,ULONG);
extern LONG test_attr_ctrl(struct Library *,ULONG,struct AHIAudioCtrlDrv *);
extern ULONG test_alloc(struct Library *,struct AHIAudioCtrlDrv *);
extern void test_stop(struct Library *,ULONG,struct AHIAudioCtrlDrv *);
extern void test_free(struct Library *,struct AHIAudioCtrlDrv *);
int main(void) {
    BPTR seg=LoadSeg((CONST_STRPTR)"dist/emu68hdmi.audio");
    struct Resident *r=NULL;
    struct Library *lib;
    struct AHIAudioCtrlDrv ctrl={0};
    BPTR next=seg;
    assert(seg);
    while(next && !r) {
        ULONG *p=(ULONG *)BADDR(next);
        ULONG size=p[-1];
        UWORD *w=(UWORD *)(p+1);
        UBYTE *end=(UBYTE *)p+size-4;
        for(; (UBYTE *)w+sizeof(*r)<=end; ++w) {
            struct Resident *candidate=(struct Resident *)w;
            if(*w==RTC_MATCHWORD && candidate->rt_MatchTag==candidate) {r=candidate;break;}
        }
        next=*p;
    }
    assert(r);
    lib=(struct Library *)InitResident(r,seg);
    printf("Resident: %p version=%u negsize=%u\n",(void *)lib,lib ? lib->lib_Version:0,lib?lib->lib_NegSize:0);
    assert(lib && lib->lib_Version==4 && lib->lib_NegSize>=114);
    assert(lib->lib_Revision==16);
    assert(test_open(lib)==lib);
    assert(lib->lib_OpenCnt==1);
    test_close(lib);
    assert(lib->lib_OpenCnt==0);
    assert(hdmi_status(lib,HDMI_QUERY_STAGE)==0);
    assert(hdmi_status(lib,HDMI_QUERY_STATE)==0);
    assert(hdmi_status(lib,HDMI_QUERY_FAULT)==0);
    assert(hdmi_status(lib,0x12345678)==0);
    assert(test_attr(lib,AHIDB_Bits)==16);
    assert(test_attr(lib,AHIDB_Frequency)==48000);
    assert(test_attr(lib,AHIDB_Realtime)==TRUE);
    assert(test_attr(lib,0x12345678)==12345);
    ctrl.ahiac_Flags=AHIACF_STEREO|AHIACF_HIFI;
    assert(test_attr_ctrl(lib,AHIDB_Bits,&ctrl)==24);
    ctrl.ahiac_Flags=AHIACF_STEREO;
    assert(test_attr_ctrl(lib,AHIDB_Bits,&ctrl)==16);
    /* No hardware: allocation must fail without touching MMIO; FreeAudio
     * must remove ownership even after partially failed allocation. */
    ctrl.ahiac_Flags=AHIACF_STEREO;
    assert(test_alloc(lib,&ctrl)&AHISF_ERROR);
    assert((hdmi_status(lib,HDMI_QUERY_STATE)&16)==0);
    test_stop(lib,0,&ctrl);
    test_stop(lib,0x01000000UL,&ctrl);
    test_stop(lib,AHISF_RECORD,&ctrl);
    assert((hdmi_status(lib,HDMI_QUERY_STATE)&16)==0);
    test_stop(lib,AHISF_PLAY,&ctrl);
    assert((hdmi_status(lib,HDMI_QUERY_STATE)&16)==16);
    test_free(lib,&ctrl);
    assert(!ctrl.ahiac_DriverData);
    assert(hdmi_status(lib,HDMI_QUERY_STATE)==0);
    assert(hdmi_status(lib,HDMI_QUERY_STAGE)==63);
    assert(!FindSemaphore((CONST_STRPTR)"HDMITest.hardware"));
    ctrl.ahiac_Flags=AHIACF_STEREO|AHIACF_HIFI;
    assert(test_alloc(lib,&ctrl)&AHISF_ERROR);
    /* HiFi must reach hardware discovery, not be rejected by the format gate. */
    assert(ctrl.ahiac_DriverData);
    assert((hdmi_status(lib,HDMI_QUERY_STATE)&16)==0);
    test_stop(lib,0,&ctrl);
    test_stop(lib,0x01000000UL,&ctrl);
    test_stop(lib,AHISF_RECORD,&ctrl);
    assert((hdmi_status(lib,HDMI_QUERY_STATE)&16)==0);
    test_stop(lib,AHISF_PLAY,&ctrl);
    assert((hdmi_status(lib,HDMI_QUERY_STATE)&16)==16);
    test_free(lib,&ctrl);
    RemLibrary(lib);
    puts("PASS: resident, AHI vectors, STOP PLAY flag regression, diagnostics, cleanup and expunge.");
    return 0;
}
