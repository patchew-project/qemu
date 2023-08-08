#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "native/libnative.h"

#define WRAP_NATIVE()                                 \
    do {                                              \
        __asm__ volatile(__CALL_EXPR : : : "memory"); \
    } while (0)

#if defined(i386) || defined(x86_64)
/*
 * An unused instruction is utilized to mark a native call.
 */
#define __CALL_EXPR ".byte 0x0f, 0xff;"
#endif

#if defined(arm) || defined(aarch64)
/*
 * HLT is an invalid instruction for userspace and usefully has 16
 * bits of spare immeadiate data which we can stuff data in.
 */
#define __CALL_EXPR "hlt 0xffff;"
#endif

#if defined(mips) || defined(mips64)
/*
 * The syscall instruction contains 20 unused bits, which are typically
 * set to 0. These bits can be used to store non-zero data,
 * distinguishing them from a regular syscall instruction.
 */
#define __CALL_EXPR "syscall 0xffff;"
#endif

void *memcpy(void *dest, const void *src, size_t n)
{
    WRAP_NATIVE();
}
int memcmp(const void *s1, const void *s2, size_t n)
{
    WRAP_NATIVE();
}
void *memset(void *s, int c, size_t n)
{
    WRAP_NATIVE();
}
char *strncpy(char *dest, const char *src, size_t n)
{
    WRAP_NATIVE();
}
int strncmp(const char *s1, const char *s2, size_t n)
{
    WRAP_NATIVE();
}
char *strcpy(char *dest, const char *src)
{
    WRAP_NATIVE();
}
char *strcat(char *dest, const char *src)
{
    WRAP_NATIVE();
}
int strcmp(const char *s1, const char *s2)
{
    WRAP_NATIVE();
}
