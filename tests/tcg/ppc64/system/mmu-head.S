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

#include "lib/boot.S"

    /* Read a location with translation on */
FUNCTION(test_read)
    mfmsr   %r9
    ori     %r8,%r9,0x10    /* set MSR_DR */
    mtmsrd  %r8,0
    mr      %r6,%r3
    li      %r3,0
    ld      %r5,0(%r6)
    li      %r3,1
    /* land here if DSI occurred */
    mtmsrd  %r9,0
    std     %r5,0(%r4)
    blr

    /* Write a location with translation on */
FUNCTION(test_write)
    mfmsr   %r9
    ori     %r8,%r9,0x10    /* set MSR_DR */
    mtmsrd  %r8,0
    mr      %r6,%r3
    li      %r3,0
    std     %r4,0(%r6)
    li      %r3,1
    /* land here if DSI occurred */
    mtmsrd  %r9,0
    blr

    /* Do a dcbz with translation on */
FUNCTION(test_dcbz)
    mfmsr   %r9
    ori     %r8,%r9,0x10    /* set MSR_DR */
    mtmsrd  %r8,0
    mr      %r6,%r3
    li      %r3,0
    dcbz    0,%r6
    li      %r3,1
    /* land here if DSI occurred */
    mtmsrd  %r9,0
    blr

FUNCTION(test_exec)
    mtsrr0  %r4
    mtsrr1  %r5
    rfid

#define EXCEPTION(nr)        \
    .= nr                   ;\
    li      %r3, (nr >> 4)  ;\
    attn

    /* DSI vector - skip the failing instruction + the next one */
    . = 0x300
    mtsprg0 %r10
    mfsrr0  %r10
    addi    %r10,%r10,8
    mtsrr0  %r10
    rfid

    EXCEPTION(0x380)

    /*
     * ISI vector - jump to LR to return from the test,
     * with r3 cleared
     */
    . = 0x400
    li      %r3,0
    blr

    /* More exception stubs */
    EXCEPTION(0x480)
    EXCEPTION(0x500)
    EXCEPTION(0x600)
    EXCEPTION(0x700)
    EXCEPTION(0x800)
    EXCEPTION(0x900)
    EXCEPTION(0x980)
    EXCEPTION(0xa00)
    EXCEPTION(0xb00)

    /*
     * System call - used to exit from tests where MSR[PR]
     * may have been set.
     */
    . = 0xc00
    blr

    EXCEPTION(0xd00)
    EXCEPTION(0xe00)
    EXCEPTION(0xe20)
    EXCEPTION(0xe40)
    EXCEPTION(0xe60)
    EXCEPTION(0xe80)
    EXCEPTION(0xf00)
    EXCEPTION(0xf20)
    EXCEPTION(0xf40)
    EXCEPTION(0xf60)
    EXCEPTION(0xf80)

    . = 0x1000
    /*
     * This page gets mapped at various locations and
     * the tests try to execute from it.
     * r3 contains the test number.
     */
FUNCTION(test_start)
    nop
    nop
    cmpdi   %r3,1
    beq     test_1
    cmpdi   %r3,2
    beq     test_2
test_return:
    li      %r3,1
    sc

    . = 0x1ff8
    /* test a branch near the end of a page */
test_1:     b   test_return

    /* test flowing from one page to the next */
test_2:     nop
    b       test_return
