 /*
  * .bss region exists but unused
  * SPDX-License-Identifier: GPL-2.0-or-later
  */
int x;
void _start(void)
{
    /*
     * Exit immediately & never touched x
     */
    __asm__ volatile(
        "mov $60, %%rax\n"
        "xor %%rdi, %%rdi\n"
        "syscall\n"
        : /* no out */
        : /* no in */
        : "rax", "rdi"
    );
    __builtin_unreachable();
}
