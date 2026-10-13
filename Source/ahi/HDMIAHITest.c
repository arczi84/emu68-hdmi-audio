/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>
#include <stdio.h>
#include "../src/tone.h"
#include "diag.h"
#define HDMI_MODE 0x001f0010UL
struct Library *AHIBase;
int main(void)
{
    struct Library *driver=NULL;
    struct MsgPort *port=NULL;
    struct AHIRequest *request=NULL;
    struct AHIAudioCtrl *ctrl=NULL;
    WORD *pcm=NULL;
    int opened=0,result=20;
    ULONG i,error,cycle;
    LONG options[6]={0};
    struct RDArgs *args=ReadArgs((CONST_STRPTR)"RESTART/S,STALL/S,HIFI/S,LOW/S,VOLUME/K/N,DEFAULT/S",options,NULL);
    struct TagItem tags[]={ {AHIA_AudioID,HDMI_MODE}, {AHIA_MixFreq,48000},
        {AHIA_Channels,1}, {AHIA_Sounds,1}, {TAG_DONE,0} };
    struct TagItem play[]={ {AHIC_Play,TRUE}, {TAG_DONE,0} };
    struct TagItem stop[]={ {AHIC_Play,FALSE}, {TAG_DONE,0} };
    struct AHISampleInfo sample={AHIST_S16S,NULL,144000};
    if(!args) { PrintFault(IoErr(),(CONST_STRPTR)"HDMIAHITest"); return 20; }
    if(options[2]) tags[0].ti_Data=0x001f0011UL;
    if(options[3]) tags[0].ti_Data+=2;  /* low latency twins: 001f0012 / 001f0013 */
    if(options[5]) tags[0].ti_Data=AHI_DEFAULT_ID;  /* the mode the user chose for the Music unit in AHI Prefs */
    printf("HDMIAHITest 0.10 - direct AHI, mode %08x%s; RESTART = 3 START/STOP cycles\n",
        tags[0].ti_Data,options[2]?" HiFi":"");
    port=CreateMsgPort();
    if(!port) goto done;
    request=(struct AHIRequest *)CreateIORequest(port,sizeof(*request));
    if(!request) goto done;
    request->ahir_Version=4;
    if(OpenDevice((CONST_STRPTR)"ahi.device",AHI_NO_UNIT,(struct IORequest *)request,0)) {
        puts("Cannot open ahi.device V4+. Install AHI first."); goto done;
    }
    opened=1; AHIBase=(struct Library *)request->ahir_Std.io_Device;
    printf("AHI version: %u.%u\n",AHIBase->lib_Version,AHIBase->lib_Revision);
    ctrl=AHI_AllocAudioA(tags);
    if(!ctrl) { puts("AHI_AllocAudio failed. Check installed driver/mode, HDMI and mailbox.resource."); goto done; }
    driver=OpenLibrary((CONST_STRPTR)"emu68hdmi.audio",4);
    {   /* The standard master volume: what AHI left after AllocAudio (the unit setting of AHI Prefs), then VOLUME = percent of unity */
        LONG volume=-1;
        struct TagItem query[]={ {AHIC_OutputVolume_Query,0}, {TAG_DONE,0} };
        query[0].ti_Data=(ULONG)&volume;
        AHI_ControlAudioA(ctrl,query);
        printf("AHIC_OutputVolume after AllocAudio: %d.%03d (1.000 = unity)\n",(int)(volume>>16),(int)(((volume&0xffffL)*1000L)>>16));
        if(options[4]) {
            struct TagItem set[]={ {AHIC_OutputVolume,0}, {TAG_DONE,0} };
            set[0].ti_Data=(ULONG)((*(ULONG *)options[4]*0x10000UL)/100UL);
            AHI_ControlAudioA(ctrl,set);
            AHI_ControlAudioA(ctrl,query);
            printf("AHIC_OutputVolume set to %u%%, now %d.%03d\n",(ULONG)*(ULONG *)options[4],(int)(volume>>16),(int)(((volume&0xffffL)*1000L)>>16));
        }
    }
    if(driver) printf("Resident HDMI driver: %u.%u\n",driver->lib_Version,driver->lib_Revision);
    if(options[1] && (!driver || driver->lib_Version!=4 || driver->lib_Revision!=10)) {
        puts("STALL is only supported by driver 4.10; use RESTART without STALL"); goto done;
    }
    pcm=AllocVec(144000*4,MEMF_PUBLIC);
    if(!pcm) goto done;
    for(i=0;i<144000;++i) {
        WORD s=sine48[i%48];
        pcm[i*2]=(i>=48000 && i<96000)?0:s;
        pcm[i*2+1]=i<48000?0:s;
    }
    sample.ahisi_Address=pcm;
    error=AHI_LoadSound(0,AHIST_SAMPLE,&sample,ctrl);
    if(error) { printf("AHI_LoadSound error: %u\n",error); goto done; }
    for(cycle=0;cycle<(options[0]?3u:1u);++cycle) {
    printf("CYCLE %u START BEGIN\n",cycle+1); fflush(stdout);
    AHI_SetFreq(0,48000,ctrl,AHISF_IMM);
    AHI_SetVol(0,0x10000,0x8000,ctrl,AHISF_IMM);
    AHI_SetSound(0,0,0,144000,ctrl,AHISF_IMM);
    AHI_SetSound(0,AHI_NOSOUND,0,0,ctrl,0); /* Stop after one pass. */
    puts("Playing through AHI: LEFT 1s, RIGHT 1s, BOTH 1s (-30 dBFS). Ctrl-C stops.");
    fflush(stdout);
    error=AHI_ControlAudioA(ctrl,play);
    if(error) { printf("AHI start error: %u\n",error); hdmi_diag(driver); goto done; }
    for(i=0;i<180;++i) {
        if(options[1] && cycle==0 && i==50) {
            puts("STALL: request 6 DOS ticks pause in DMA observation; expect brief interruption and recovery");
            fflush(stdout);
            if(hdmi_status(driver,HDMI_REQUEST_STALL)!=1) {
                puts("STALL request rejected"); goto done;
            }
        }
        if(SetSignal(0,0)&SIGBREAKF_CTRL_C) break;
        Delay(1);
    }
    puts("STOP BEGIN"); fflush(stdout);
    error=AHI_ControlAudioA(ctrl,stop);
    printf("STOP returned %u\n",error); fflush(stdout);
    hdmi_diag(driver);
    if(error) goto done;
    if(options[1] && cycle==0 &&
       (hdmi_status(driver,HDMI_QUERY_RECOVERIES)<1 ||
        hdmi_status(driver,HDMI_QUERY_FAULT)!=0)) {
        puts("STALL FAIL: no recovery or terminal driver fault"); goto done;
    }
    if(options[1] && cycle==0) puts("STALL PASS: recovered after missed deadline and STOP returned");
    if(SetSignal(0,0)&SIGBREAKF_CTRL_C) break;
    Delay(50);
    }
    puts("AHI playback test ended. Confirm sound and channel order by listening.");
    result=0;
done:
    if(ctrl) {
        puts("FREE BEGIN"); fflush(stdout);
        AHI_FreeAudio(ctrl);
        puts("FREE returned"); fflush(stdout);
    }
    if(driver) CloseLibrary(driver);
    if(pcm) FreeVec(pcm);
    if(opened) CloseDevice((struct IORequest *)request);
    if(request) DeleteIORequest((struct IORequest *)request);
    if(port) DeleteMsgPort(port);
    FreeArgs(args);
    return result;
}
