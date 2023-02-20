#define N_ITERATIONS 10

    .org 0x8d
ilc:
    .org 0x8e
program_interruption_code:
    .org 0x96
per_code:
    .org 0x150
program_old_psw:
    .org 0x1d0                         /* program new PSW */
    .quad 0,pgm_handler
    .org 0x200                         /* lowcore padding */

    .globl _start
_start:
    lpswe per_on_psw
start_per:
    lghi %r0,N_ITERATIONS
    xgr %r1,%r1
    lctlg %c9,%c11,per_on_regs
loop:
    brct %r0,loop
    lctlg %c9,%c11,per_off_regs
    cgijne %r1,N_ITERATIONS-1,fail     /* expected number of events? */
    lpswe success_psw
fail:
    lpswe failure_psw

pgm_handler:
    chhsi program_interruption_code,0x80         /* PER event? */
    jne fail
    cli per_code,0x80                  /* successful-branching event? */
    jne fail
    la %r1,1(%r1)                      /* increment event counter */
    mvc return_psw(8),program_old_psw
    lg %r3,program_old_psw+8
    llgc %r2,ilc
    sgr %r3,%r2                        /* rewind PSW */
    stg %r3,return_psw+8
    lpswe return_psw

    .align 8
per_on_psw:
    .quad 0x4000000000000000,start_per
per_on_regs:
    .quad 0x80000000,0,-1              /* successful-branching everywhere */
per_off_regs:
    .quad 0,0,0
return_psw:
    .quad 0,0
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
