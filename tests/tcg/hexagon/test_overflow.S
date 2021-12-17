// Purpose: test example, verify the soundness of the overflow bit
//
// a right shift with negative amount should make r0 saturate, setting the
// overflow bit

    .text
    .globl _start

_start:
    {
        call init
    }
    {
        r0=#0x10000000
        r1=#-2
    }
    {
        r2=asr(r0, r1):sat
    }
    {
        r4=USR
    }
    {
        r5=and(r4,#1)
    }
    {
        p0 = cmp.eq(r5, #0); if (p0.new) jump:t check_ovfl
        jump fail
    }
check_ovfl:
    {
        r1=#-3
    }
    {
        r2=asr(r0, r1):sat
    }
    {
        r4=USR
    }
    {
        r5=and(r4,#1)
    }
    {
        p0 = cmp.eq(r5, #1); if (p0.new) jump:t check_sticky
        jump fail
    }
check_sticky:
    {
        r1=#-2
    }
    {
        r2=asr(r0, r1):sat
    }
    {
        r4=USR
    }
    {
        r5=and(r4,#1)
    }
    {
        p0 = cmp.eq(r5, #1); if (p0.new) jump:t pass
        jump fail
    }
