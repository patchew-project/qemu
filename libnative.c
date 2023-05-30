#include <stdio.h>
#include <stdlib.h>

#define NATIVE_MEMCPY 0x1001
#define NATIVE_MEMCMP 0x1002
#define NATIVE_MEMSET 0x1003
#define NATIVE_STRCPY 0x1004
#define NATIVE_STRCMP 0x1005
#define NATIVE_STRCAT 0x1006

void *memcpy(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memset(void *s, int c, size_t n);
char *strcpy(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);
char *strcat(char *dest, const char *src);

#define STR_MACRO(str) #str
#define STR(num) STR_MACRO(num)

#if defined(TARGET_X86_64) || defined(TARGET_I386)

/* unused opcode */
#define __PREFIX_INSTR \
    ".byte 0x0f,0xff;"

#define NATIVE_CALL_EXPR(func) \
    __PREFIX_INSTR             \
    ".word " STR(func) ";" : : :
#endif

#if defined(TARGET_ARM) || defined(TARGET_AARCH64)

/* unused syscall number */
#define __PREFIX_INSTR \
    "svc 0xff;"

#define NATIVE_CALL_EXPR(func) \
    __PREFIX_INSTR             \
    ".word " STR(func) ";" : : :

#endif

#if defined(TARGET_MIPS) || defined(TARGET_MIPS64)

/* unused bytes in syscall instructions */
#define NATIVE_CALL_EXPR(func) \
    ".long " STR((0x1 << 24) + (func << 8) + 0xC) ";" : : :

#endif

void *memcpy(void *dest, const void *src, size_t n)
{
    __asm__ volatile(NATIVE_CALL_EXPR(NATIVE_MEMCPY));
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    __asm__ volatile(NATIVE_CALL_EXPR(NATIVE_MEMCMP));
}
void *memset(void *s, int c, size_t n)
{
    __asm__ volatile(NATIVE_CALL_EXPR(NATIVE_MEMSET));
}
char *strcpy(char *dest, const char *src)
{
    __asm__ volatile(NATIVE_CALL_EXPR(NATIVE_STRCPY));
}
int strcmp(const char *s1, const char *s2)
{
    __asm__ volatile(NATIVE_CALL_EXPR(NATIVE_STRCMP));
}
char *strcat(char *dest, const char *src)
{
    __asm__ volatile(NATIVE_CALL_EXPR(NATIVE_STRCAT));
}
