.text
.globl _invoke_exit
_invoke_exit:
 move.l 4(sp),d0
 move.l 8(sp),d1
 jmp _hdmi_process_exit
