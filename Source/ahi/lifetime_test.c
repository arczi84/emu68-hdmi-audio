/* Gateway test only; vamos cannot create/schedule real DOS processes. */
#include <assert.h>
#include <stdio.h>
#include <proto/exec.h>
#include "lifetime.h"
extern LONG invoke_exit(LONG,struct hdmi_process *);
int main(void)
{
    struct hdmi_process life={0};
    unsigned i;
    for(i=0;i<30;++i) {
        life.task=(struct Process *)0x12345678;
        life.result=0xffffffffUL;
        assert(invoke_exit(i&1?5:0,&life)==(i&1?5:0));
        Permit(); /* Real DOS instead goes directly to RemTask. */
        assert(!life.task && life.result==(i&1?5u:0u));
    }
    puts("PASS: native NP_ExitCode D0/D1 ABI, exit result and final task acknowledgement.");
    return 0;
}
