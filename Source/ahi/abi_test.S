.text
.globl _test_attr
_test_attr:
 movem.l d2/a2/a6,-(sp)
 move.l 16(sp),a6
 move.l 20(sp),d0
 moveq #0,d1
 move.l #12345,d2
 sub.l a2,a2
 sub.l a1,a1
 jsr -108(a6)
 movem.l (sp)+,d2/a2/a6
 rts
.globl _test_alloc
_test_alloc:
 movem.l a2/a6,-(sp)
 move.l 12(sp),a6
 move.l 16(sp),a2
 sub.l a1,a1
 jsr -30(a6)
 movem.l (sp)+,a2/a6
 rts
.globl _test_attr_ctrl
_test_attr_ctrl:
 movem.l d2/a2/a6,-(sp)
 move.l 16(sp),a6
 move.l 20(sp),d0
 move.l 24(sp),a2
 moveq #0,d1
 move.l #12345,d2
 sub.l a1,a1
 jsr -108(a6)
 movem.l (sp)+,d2/a2/a6
 rts
.globl _test_free
_test_free:
 movem.l a2/a6,-(sp)
 move.l 12(sp),a6
 move.l 16(sp),a2
 jsr -36(a6)
 movem.l (sp)+,a2/a6
 rts
.globl _test_open
_test_open:
 move.l a6,-(sp)
 move.l 8(sp),a6
 moveq #4,d0
 jsr -6(a6)
 move.l (sp)+,a6
 rts
.globl _test_close
_test_close:
 move.l a6,-(sp)
 move.l 8(sp),a6
 jsr -12(a6)
 move.l (sp)+,a6
 rts

.globl _test_stop
_test_stop:
 movem.l a2/a6,-(sp)
 move.l 12(sp),a6
 move.l 16(sp),d0
 move.l 20(sp),a2
 jsr -66(a6)
 movem.l (sp)+,a2/a6
 rts
