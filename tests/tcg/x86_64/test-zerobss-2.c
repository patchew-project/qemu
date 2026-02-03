/*
 * Test case for zero_bss() with anonymous BSS in RX PT_LOAD
 *
 * This binary has .bss in the same PT_LOAD as .text (R_X permissions),
 * but the BSS is anonymous (beyond p_filesz), not file-backed.
 * Actual behavior:
 * old code: Fails with "PT_LOAD with non-writable bss"
 * new code: Succeeds, zeros BSS, exits with code 0
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* These will be included in .bss (uninitialized anonymous memory) */
int bss_value;
int bss_array[64];

void _start(void)
{
    int sum = bss_value;
    int i;

    for (i = 0; i < 64; i++) {
        sum += bss_array[i];
    }
    /* If BSS was properly zeroed, sum should be 0 */
    /* Exit with sum as exit code */
    __asm__ volatile (
        "movl %0, %%edi\n\t"
        "movl $60, %%eax\n\t"
        "syscall\n\t"
        : /* no out*/
        : "r" (sum)
        : "rdi", "rax"
    );

    __builtin_unreachable();
}
