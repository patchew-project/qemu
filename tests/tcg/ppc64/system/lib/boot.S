/* Copyright 2013-2014 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "asm.h"

#define SPR_HID0                        0x3f0
#define SPR_HID0_POWER9_HILE            0x0800000000000000

#define ADP_STOPPED_APPLICATIONEXIT     0x20026

    .section ".head","ax"

    /* QEMU enters in BE at 0x10 by default */
    . = 0x10
.global start
start:
    FIXUP_ENDIAN

    /* Setup TOC */
    LOAD_IMM64(%r2, .TOC.)

    /* Configure interrupt endian */
#ifdef __LITTLE_ENDIAN__
    mfspr   %r10, SPR_HID0
    LOAD_IMM64(%r11, SPR_HID0_POWER9_HILE)
    or      %r10, %r10, %r11
    mtspr   SPR_HID0, %r10
#endif

    /* Clear .bss */
    LOAD_IMM64(%r10, __bss_start)
    LOAD_IMM64(%r11, __bss_end)
    subf    %r11, %r10, %r11
    addi    %r11, %r11, 63
    srdi.   %r11, %r11, 6
    beq     2f
    mtctr   %r11
1:  dcbz    0, %r10
    addi    %r10, %r10, 64
    bdnz    1b

    /* Setup stack */
2:  LOAD_IMM64(%r1, __stack_top)
    li      %r0, 0
    stdu    %r0, -32(%r1)

    CALL(main)

    /* Terminate on exit */
    CALL_LOCAL(sys_exit)
    b       .

FUNCTION(sys_exit)
    /*
     * As semihosting operations are executed by non-translated QEMU code,
     * we shouldn't need to save LR.
     */
    LOAD_IMM64(%r4, ADP_STOPPED_APPLICATIONEXIT)
    std     %r4, -16(%r1)
    std     %r3, -8(%r1)
    li      %r3, 0x18
    addi    %r4, %r1, -16
    sc      7
    blr

FUNCTION(__sys_outc)
    addi    %r4, %r1, -1
    stb     %r3, 0(%r4)
    li      %r3, 0x03
    sc      7
    blr
