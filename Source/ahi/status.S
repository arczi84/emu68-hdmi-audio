/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.globl _hdmi_status
_hdmi_status:
 movem.l a2/a6,-(sp)
 move.l 12(sp),a6
 move.l 16(sp),d0
 moveq #0,d1
 sub.l a2,a2
 jsr -114(a6)
 movem.l (sp)+,a2/a6
 rts
