/*
 * Non-libc syscall hello world to exercise big-endian ARM Linux-user.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define __NR_exit 1
#define __NR_write 4

static const char hello[] = "Hello World\n";
/* Volatile forces a data load to check the CPU data byte order. */
static volatile int len = sizeof(hello) - 1;

static inline int write(int fd, const char *buf, int len)
{
    register int r0 __asm__("r0") = fd;
    register const char *r1 __asm__("r1") = buf;
    register int r2 __asm__("r2") = len;
    register int r7 __asm__("r7") = __NR_write;

    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7)
                     : "memory");

    return r0;
}

void _start(void)
{
    register int r0 __asm__("r0") = write(1, hello, len) != len;
    register int r7 __asm__("r7") = __NR_exit;

    __asm__ volatile("svc 0" : : "r"(r0), "r"(r7) : "memory");
    __builtin_unreachable();
}
