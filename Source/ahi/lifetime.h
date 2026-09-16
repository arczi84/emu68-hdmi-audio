/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMI_AHI_LIFETIME_H
#define HDMI_AHI_LIFETIME_H
#include <exec/types.h>
#include <dos/dosextens.h>
struct hdmi_process {
    struct Process *volatile task;
    struct Task *waiter;
    ULONG wake;
    volatile ULONG ready, result;
};
int hdmi_spawn(struct hdmi_process *, LONG (*entry)(void), APTR context);
/* NP_ExitCode: D0 return code, D1 pointer to hdmi_process. */
void hdmi_process_exit(void);
LONG hdmi_process_exited(LONG,struct hdmi_process *);
#endif
