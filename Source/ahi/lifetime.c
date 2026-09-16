/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lifetime.h"
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>
int hdmi_spawn(struct hdmi_process *life,LONG (*entry)(void),APTR context)
{
    struct TagItem tags[]={
        {NP_Entry,(ULONG)entry}, {NP_Name,(ULONG)"Emu68 HDMI AHI"},
        {NP_Priority,5}, {NP_StackSize,65536},
        {NP_ExitCode,(ULONG)hdmi_process_exit}, {NP_ExitData,(ULONG)life},
        {TAG_DONE,0}
    };
    int started;
    Forbid();
    life->task=CreateNewProc(tags);
    if(life->task) life->task->pr_Task.tc_UserData=context;
    started=life->task!=NULL;
    Permit();
    return started;
}
LONG hdmi_process_exited(LONG result,struct hdmi_process *life)
{
    /* Called AFTER DOS has released the process's files/locks. The DOS exit
     * path goes directly to RemTask after this callback. Keep scheduling
     * forbidden through the callback return, so the waiter cannot free this
     * library/context while callback instructions are still running.
     * See Amiga RKM DOS: pr_ExitCode, and CreateNewProc NP_ExitCode/ExitData. */
    Forbid();
    life->result=result;
    life->task=NULL;
    if(life->waiter) Signal(life->waiter,life->wake);
    return result;
}
