    .org 0x1D0                 /* Program new PSW */
    .quad 0x180000000, _pgm    /* BA, EA */
    .org 0x200                 /* lowcore padding */

    .globl _start
_start:
    lctlg %c0,%c0,_c0
    vst %v0,_unaligned
    mviy _exit,0

_pgm:
    mviy _exit,1

    .align 8
_c0:
    .quad 0x10060000           /* lowcore protection, AFP, VX */

    .byte 0
_unaligned:
    .octa 0

    .org 0x2000                /* mmio-debug-exit base */
_exit:
    .byte 0
