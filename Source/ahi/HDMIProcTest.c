/* SPDX-License-Identifier: GPL-3.0-or-later
 * Runs on real AmigaOS. No AHI, mailbox or HDMI access. */
#include <stdio.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "lifetime.h"
struct context { struct hdmi_process life; ULONG expected; };
static LONG child(void)
{
    struct context *c=FindTask(NULL)->tc_UserData;
    Delay(1);
    return c->expected;
}
int main(void)
{
    struct context c={0};
    BYTE signal=AllocSignal(-1);
    unsigned i;
    if(signal<0) return 20;
    puts("HDMIProcTest 0.1 - 30 DOS processes, alternating return 0 / 5; no AHI or HDMI.");
    fflush(stdout);
    for(i=0;i<30;++i) {
        c.expected=i&1?5:0;
        c.life.waiter=FindTask(NULL); c.life.wake=1UL<<signal;
        c.life.result=0xffffffffUL;
        Forbid();
        if(!hdmi_spawn(&c.life,child,&c)) {
            Permit(); FreeSignal(signal);
            printf("CreateNewProc failed at cycle %u\n",i+1); return 20;
        }
        while(c.life.task) Wait(c.life.wake);
        c.life.waiter=NULL;
        Permit();
        if(c.life.result!=c.expected) {
            FreeSignal(signal); puts("Wrong NP_ExitCode result."); return 20;
        }
        if(i%5==4) { printf("%u/30 exits acknowledged by DOS\n",i+1); fflush(stdout); }
    }
    FreeSignal(signal);
    puts("PASS: 30 complete process lifetimes, including failed-start return code 5.");
    return 0;
}
