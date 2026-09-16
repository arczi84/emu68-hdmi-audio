/* SPDX-License-Identifier: GPL-3.0-or-later
 * AHI device-interface probe: same linked CMD_WRITE path as Amiga SDL.
 * No direct HDMI access and no driver/configuration changes. */
#include <exec/memory.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include "../src/tone.h"
#define FRAMES 2048
#define RATE 44100
static void stage(const char *s) { puts(s); fflush(stdout); }
static int finish(struct AHIRequest *r, int *pending)
{
    unsigned ticks=0;
    if(!*pending) return 1;
    while(!CheckIO((struct IORequest *)r)) {
        if((SetSignal(0,0)&SIGBREAKF_CTRL_C) || ++ticks>=500) {
            stage("WAIT interrupted/timed out; entering AbortIO then WaitIO");
            AbortIO((struct IORequest *)r);
            WaitIO((struct IORequest *)r);
            *pending=0;
            stage("AbortIO/WaitIO returned");
            return 0;
        }
        Delay(1);
    }
    WaitIO((struct IORequest *)r); *pending=0;
    printf("DONE error=%d actual=%u\n",(int)r->ahir_Std.io_Error,r->ahir_Std.io_Actual);
    fflush(stdout);
    return r->ahir_Std.io_Error==0;
}
static int cycle(unsigned number,int beep)
{
    struct MsgPort *port=NULL;
    struct AHIRequest *r[2]={NULL,NULL};
    WORD *pcm[2]={NULL,NULL};
    int pending[2]={0,0},opened=0,ok=0;
    unsigned i,j,count=beep?1:32;
    printf("CYCLE %u: %s\n",number,beep?"single short sound":"32 linked buffers, last 16 silent");
    fflush(stdout);
    port=CreateMsgPort(); if(!port) goto done;
    for(i=0;i<2;++i) {
        r[i]=(struct AHIRequest *)CreateIORequest(port,sizeof(*r[i]));
        pcm[i]=AllocVec(FRAMES*4,MEMF_PUBLIC|MEMF_CLEAR);
        if(!r[i] || !pcm[i]) goto done;
    }
    r[0]->ahir_Version=4;
    stage("OPEN Unit 0 BEGIN (uses Unit 0 preferences)");
    if(OpenDevice((CONST_STRPTR)"ahi.device",0,(struct IORequest *)r[0],0)) {
        stage("OPEN FAILED"); goto done;
    }
    opened=1;
    printf("OPEN OK: ahi.device %u.%u\n",r[0]->ahir_Std.io_Device->dd_Library.lib_Version,
           r[0]->ahir_Std.io_Device->dd_Library.lib_Revision);
    fflush(stdout);
    r[1]->ahir_Std.io_Device=r[0]->ahir_Std.io_Device;
    r[1]->ahir_Std.io_Unit=r[0]->ahir_Std.io_Unit;
    r[1]->ahir_Version=4;
    for(i=0;i<count;++i) {
        unsigned cur=i&1,prev=cur^1;
        struct AHIRequest *q=r[cur];
        /* cur was acknowledged before its storage is reused. */
        for(j=0;j<FRAMES;++j) {
            WORD s=(beep || i<16)?sine48[(i*FRAMES+j)%48]:0;
            pcm[cur][j*2]=s; pcm[cur][j*2+1]=s;
        }
        q->ahir_Std.io_Command=CMD_WRITE;
        q->ahir_Std.io_Flags=0;
        q->ahir_Std.io_Error=0;
        q->ahir_Std.io_Offset=0;
        q->ahir_Std.io_Data=pcm[cur];
        q->ahir_Std.io_Length=FRAMES*4;
        q->ahir_Std.io_Message.mn_Node.ln_Pri=60;
        q->ahir_Frequency=RATE; q->ahir_Type=AHIST_S16S;
        q->ahir_Volume=0x10000; q->ahir_Position=0x8000;
        q->ahir_Link=i?r[prev]:NULL;
        printf("SEND %u/%u %s link=%s\n",i+1,count,(beep || i<16)?"TONE":"SILENCE",i?"yes":"no");
        fflush(stdout);
        SendIO((struct IORequest *)q); pending[cur]=1;
        if(i) {
            stage("WAIT previous BEGIN");
            if(!finish(r[prev],&pending[prev])) goto done;
        }
    }
    stage("WAIT final BEGIN");
    if(!finish(r[(count-1)&1],&pending[(count-1)&1])) goto done;
    stage("ALL writes returned; idle 2 seconds with device open");
    for(i=0;i<100;++i) {
        if(SetSignal(0,0)&SIGBREAKF_CTRL_C) goto done;
        Delay(1);
    }
    ok=1;
done:
    /* Abort all outstanding requests; retain memory until every reply arrives. */
    for(i=0;i<2;++i) if(pending[i]) AbortIO((struct IORequest *)r[i]);
    for(i=0;i<2;++i) if(pending[i]) {
        stage("CLEANUP WaitIO BEGIN");
        WaitIO((struct IORequest *)r[i]); pending[i]=0;
        stage("CLEANUP WaitIO returned");
    }
    if(opened) {
        stage("CLOSE Unit 0 BEGIN");
        CloseDevice((struct IORequest *)r[0]);
        stage("CLOSE Unit 0 returned");
    }
    for(i=0;i<2;++i) {
        if(r[i]) DeleteIORequest((struct IORequest *)r[i]);
        if(pcm[i]) FreeVec(pcm[i]);
    }
    if(port) DeleteMsgPort(port);
    stage(ok?"CYCLE complete":"CYCLE failed");
    return ok;
}
int main(void)
{
    LONG options[1]={0};
    struct RDArgs *args=ReadArgs((CONST_STRPTR)"BEEP/S",options,NULL);
    unsigned i;
    int result=20;
    if(!args) { PrintFault(IoErr(),(CONST_STRPTR)"HDMIUnitTest"); return 20; }
    stage("HDMIUnitTest 0.1 - AHI Unit 0, stereo 16-bit 44100 Hz, 2048 frames");
    stage("Keep driver 4.1. Output follows Unit 0 prefs; tone about -30 dBFS.");
    for(i=1;i<=3;++i) {
        if(!cycle(i,options[0]!=0)) goto done;
        stage("Device closed; pause 2 seconds before next cycle");
        Delay(100);
        if(SetSignal(0,0)&SIGBREAKF_CTRL_C) goto done;
    }
    stage("PASS: three Unit 0 open/write/idle/close cycles returned.");
    result=0;
done:
    FreeArgs(args);
    return result;
}
