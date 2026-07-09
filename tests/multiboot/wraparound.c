/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Regression test for the translator_ld crash when an instruction
 * straddles the end of the 32-bit address space (i386).
 */

#include "libc.h"
#include "multiboot.h"

int test_main(uint32_t magic, struct mb_info *mbi)
{
    (void) magic;
    (void) mbi;

    printf("cross-boundary insn at end of address space\n");

    /*
     * The top page (0xfffff000) is SeaBIOS ROM and cannot be written.
     * Its byte at 0xffffffff (0x00 = "add r/m8, r8") already crosses the
     * page boundary into page1 at 0x0, which is exactly the case
     * translator_ld must handle without aborting.  Reaching 0xfffffffe
     * runs the ROM's cld, then that add; the add's modrm is fetched from
     * [0x0], which is RAM, so build a short exit stub there:
     *
     *   [0x0] c0                 modrm -> "add al, al" (reg; EIP -> 1)
     *   [1] eb 00                jmp +0          (EIP -> 3)
     *   [3] b8 00 00 00 00       mov eax, 0
     *   [8] e7 f4                out 0xf4, eax   -> isa-debug-exit(0)
     *
     * Without the translator_ld fix QEMU aborts while reading the modrm
     * at the wrapped address 0x0 (translator.c assert).
     *
     * Note: this relies on the SeaBIOS byte at 0xffffffff being 0x00
     * (add r/m8, r8); if that ever changes, the stub below must move.
     */
    volatile uint8_t *p = (uint8_t *)0x00000000;
    p[0] = 0xC0;                        /* modrm: add al, al */
    p[1] = 0xEB; p[2] = 0x00;           /* jmp +0 -> 0x3 */
    /* mov eax, 0 */
    p[3] = 0xB8; p[4] = 0x00; p[5] = 0x00; p[6] = 0x00; p[7] = 0x00;
    p[8] = 0xE7; p[9] = 0xF4;           /* out 0xf4, eax -> exit 0 */

    asm volatile("mov $0xFFFFFFFE, %%eax; jmp *%%eax" : : : "eax");

    return 1; /* unreachable */
}
