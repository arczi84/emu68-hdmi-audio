/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.globl _hdmi_process_exit
_hdmi_process_exit:
 movem.l d2-d7/a2-a6,-(sp)
 move.l d1,-(sp)
 move.l d0,-(sp)
 jsr _hdmi_process_exited
 addq.l #8,sp
 movem.l (sp)+,d2-d7/a2-a6
 rts
