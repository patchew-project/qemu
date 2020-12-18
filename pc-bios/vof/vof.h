#include <stdarg.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
typedef unsigned long long uint64_t;
#define NULL (0)
#define PROM_ERROR (-1u)
typedef unsigned char bool;
typedef unsigned long ihandle;
typedef unsigned long phandle;
#define false ((bool)0)
#define true ((bool)1)
typedef int size_t;
typedef void client(void);

/* globals */
extern void _prom_entry(void); /* OF CI entry point (i.e. this firmware) */

void do_boot(unsigned long addr, unsigned long r3, unsigned long r4);

/* libc */
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
void *memcpy(void *dest, const void *src, size_t n);
int memcmp(const void *ptr1, const void *ptr2, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t size);

/* Prom */
typedef unsigned long prom_arg_t;
int call_prom(const char *service, int nargs, int nret, ...);

/* CI wrappers */
void ci_panic(const char *str);
phandle ci_finddevice(const char *path);
uint32_t ci_getprop(phandle ph, const char *propname, void *prop, int len);
ihandle ci_open(const char *path);
void ci_close(ihandle ih);
void *ci_claim(void *virt, uint32_t size, uint32_t align);
uint32_t ci_release(void *virt, uint32_t size);

/* booting from -kernel */
void boot_from_memory(uint64_t initrd, uint64_t initrdsize);
