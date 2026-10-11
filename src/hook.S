/* i686 Windows COFF uses a leading underscore for C symbols. */
.text
.p2align 4
.globl _FrameHook
_FrameHook:
    pushl %eax
    movl _g_targetBits, %eax
    movl %eax, 0x20(%ecx)
    popl %eax
    movl $1, _g_hookSeen
    jmp *_g_original
