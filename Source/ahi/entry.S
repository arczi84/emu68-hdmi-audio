/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.globl _start
_start:
 moveq #-1,d0
 rts

/* Every ABI gateway preserves d2-d7/a2-a6, including Disable/Enable. */
.macro SAVE
 movem.l d2-d7/a2-a6,-(sp)
.endm
.macro RETURN bytes=0
 .if \bytes
 lea \bytes(sp),sp
 .endif
 movem.l (sp)+,d2-d7/a2-a6
 rts
.endm
.globl _gw_init
_gw_init:
 SAVE
 move.l a6,-(sp)
 move.l a0,-(sp)
 move.l d0,-(sp)
 jsr _driver_init
 RETURN 12
.macro BASE name
.globl _gw_\name
_gw_\name:
 SAVE
 move.l a6,-(sp)
 jsr _driver_\name
 RETURN 4
.endm
BASE open
BASE close
BASE expunge
.macro CTRL name
.globl _gw_\name
_gw_\name:
 SAVE
 move.l a2,-(sp)
 jsr _driver_\name
 RETURN 4
.endm
| AHIsub_AllocAudio(tags=A1, ctrl=A2): both are needed, the tags carry the mode ID.
.globl _gw_alloc
_gw_alloc:
 SAVE
 move.l a2,-(sp)
 move.l a1,-(sp)
 jsr _driver_alloc
 RETURN 8
CTRL free
.macro NONE name
.globl _gw_\name
_gw_\name:
 movem.l d0-d7/a0-a6,-(sp)
 jsr _driver_\name
 movem.l (sp)+,d0-d7/a0-a6
 rts
.endm
NONE disable
NONE enable
.macro FLAGS name
.globl _gw_\name
_gw_\name:
 SAVE
 move.l a2,-(sp)
 move.l d0,-(sp)
 jsr _driver_\name
 RETURN 8
.endm
FLAGS start
FLAGS stop
| AHIsub_GetAttr(attr=D0, arg=D1, def=D2, tags=A1, ctrl=A2)
.globl _gw_attr
_gw_attr:
 SAVE
 move.l a2,-(sp)
 move.l a1,-(sp)
 move.l d2,-(sp)
 move.l d1,-(sp)
 move.l d0,-(sp)
 jsr _driver_attr
 RETURN 20
.globl _gw_null
_gw_null:
 moveq #0,d0
 rts
.globl _gw_unknown
_gw_unknown:
 moveq #-1,d0
 rts

.globl _gw_control
_gw_control:
 SAVE
 move.l a2,-(sp)
 move.l d1,-(sp)
 move.l d0,-(sp)
 jsr _driver_control
 RETURN 12

| ULONG hdmi_call_timer(APTR fn, APTR ctrl): ahiac_PreTimer / ahiac_PostTimer want the
| AHIAudioCtrlDrv in A2 (ahi_sub.doc, AHIsub_Start steps 2 and 5) and preserve every
| register except D0, so only A2 is saved. D0 is the result of PreTimer.
.globl _hdmi_call_timer
_hdmi_call_timer:
 move.l a2,-(sp)
 move.l 12(sp),a2
 move.l 8(sp),a0
 jsr (a0)
 move.l (sp)+,a2
 rts
