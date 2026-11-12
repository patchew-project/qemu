/*
 * Non-libc syscall hello world for Aarch64 BE
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define __NR_write 64
#define __NR_exit 93

int write(int fd, const char *buf, int len)
{
    register int x0 __asm__("x0") = fd;
    register const char *x1 __asm__("x1") = buf;
    register int x2 __asm__("x2") = len;
    register int x8 __asm__("x8") = __NR_write;

    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8)
                 : "memory");

    return x0;
}

void exit(int ret)
{
    register int x0 __asm__("x0") = ret;
    register int x8 __asm__("x8") = __NR_exit;

    asm volatile("svc #0" : : "r"(x0), "r"(x8));
    __builtin_unreachable();
}

void _start(void)
{
    /* Force a data load to check CPU data byte order. */
    static const char hello[] = "Hello World\n";
    static volatile int len = sizeof(hello) - 1;

    exit(write(1, hello, len) != len);
}
