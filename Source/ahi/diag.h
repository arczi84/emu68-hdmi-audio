/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMI_DIAG_H
#define HDMI_DIAG_H
#include "status.h"
extern LONG hdmi_status(struct Library *,ULONG);
static const char *hdmi_stage_name(ULONG stage)
{
    switch(stage) {
    case 1:return "START entry"; case 2:return "START wake/spawn";
    case 9:return "open polling timer";
    case 10:return "DMA mask mailbox"; case 11:return "DMA allocation/initial PCM";
    case 12:return "CachePreDMA"; case 13:return "select DMA channel";
    case 14:return "initial DMA reset"; case 15:return "prepare HDMI";
    case 16:return "enable DMA/HDMI";
    case 20:return "Player hook"; case 21:return "Mixer hook"; case 22:return "Mixer returned";
    case 30:return "stream loop"; case 40:return "stop DMA";
    case 41:return "restore/mute HDMI"; case 42:return "free DMA buffers";
    case 70:return "recovering after missed deadline";
    case 50:return "worker idle"; case 60:return "FreeAudio entry";
    case 61:return "FreeAudio waiting for worker exit";
    case 62:return "worker returning to DOS"; case 63:return "FreeAudio worker gone";
    default:return "not started";
    }
}
static void hdmi_diag(struct Library *driver)
{
    ULONG stage;
    if(!driver || driver->lib_Revision<6) { puts("Diagnostics require driver 4.6"); return; }
    stage=(ULONG)hdmi_status(driver,HDMI_QUERY_STAGE);
    printf("HDMI stage=%u (%s) fault=%u state=%08x result=%u\n",stage,hdmi_stage_name(stage),
        (ULONG)hdmi_status(driver,HDMI_QUERY_FAULT),(ULONG)hdmi_status(driver,HDMI_QUERY_STATE),
        (ULONG)hdmi_status(driver,HDMI_QUERY_RESULT));
    printf("After cleanup: MAI=%08x DMA_CS=%08x\n",
        (ULONG)hdmi_status(driver,HDMI_QUERY_CTRL),(ULONG)hdmi_status(driver,HDMI_QUERY_DMA));
    if(driver->lib_Revision>=8)
        printf("DMA observation: max_gap=%u us refills=%u\n",
            (ULONG)hdmi_status(driver,HDMI_QUERY_GAP),(ULONG)hdmi_status(driver,HDMI_QUERY_FILLS));
    else puts("DMA observation: gap/refill counters unavailable on this driver");
    if(driver->lib_Version==4 && driver->lib_Revision>=11 && driver->lib_Revision<=15)
        puts("Automatic recovery: disabled (4.7 playback lifetime)");
    else if(driver->lib_Version==4 && driver->lib_Revision>=16)
        printf("In-place resyncs=%u last_reason=%u (no HDMI reset)\n",
            (ULONG)hdmi_status(driver,HDMI_QUERY_RECOVERIES),
            (ULONG)hdmi_status(driver,HDMI_QUERY_RECOVERY_FAULT));
    else if(driver->lib_Revision>=9)
        printf("Recoveries=%u last_recovery_fault=%u\n",
            (ULONG)hdmi_status(driver,HDMI_QUERY_RECOVERIES),
            (ULONG)hdmi_status(driver,HDMI_QUERY_RECOVERY_FAULT));
    if(driver->lib_Revision>=10)
        printf("At fault: DMA_CS=%08x DEBUG=%08x CB=%08x\n",
            (ULONG)hdmi_status(driver,HDMI_QUERY_FAULT_CS),
            (ULONG)hdmi_status(driver,HDMI_QUERY_FAULT_DEBUG),
            (ULONG)hdmi_status(driver,HDMI_QUERY_FAULT_CB));
    if(driver->lib_Version==4 && driver->lib_Revision>=15) {
        ULONG bits=(ULONG)hdmi_status(driver,HDMI_QUERY_MIX_BITS);
        if(bits) printf("Last START mixer: %u bit%s -> HDMI %u bit, 48000 Hz\n",
            bits,bits==32?" HiFi":"",bits==32?24u:16u);
    }
    fflush(stdout);
}
#endif
