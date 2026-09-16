.text
.globl _run_eagle_check
_run_eagle_check:
 movem.l d2-d7/a2-a6,-(sp)
 move.l 48(sp),a3
 lea fake_ahi_base(pc),a6
 move.l a6,0x7dc4(a3)
 lea 0x79fc(a3),a0
 jsr (a0)
 movem.l (sp)+,d2-d7/a2-a6
 rts

/* Only BestAudioIDA (-114) is called by this part of CheckAmplifier. */
fake_ahi_vectors:
 jmp best_gateway
 .space 114-(.-fake_ahi_vectors)
fake_ahi_base:
 .long 0
best_gateway:
 movem.l d2-d7/a2-a6,-(sp)
 move.l a1,-(sp)
 jsr _eagle_best
 addq.l #4,sp
 movem.l (sp)+,d2-d7/a2-a6
 rts
