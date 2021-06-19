/* Purpose: simple C Program to test hardware loops.
 * It should print numbersfrom 0 to 9.
 */

    .text
    .globl _start

_start:
    {
        call init
    }
    {
        loop0(.LBB0_1,#10)
        r2=#0
    }
.Ltmp0:
.LBB0_1:
    {
        r2=add(r2,#1)
        nop
    }:endloop0
    {
        p0 = cmp.eq(r2, #10); if (p0.new) jump:t pass
        jump fail
    }
