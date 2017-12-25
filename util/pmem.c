/*
 * Helper functions to operate on persistent memory.
 *
 * Copyright (c) 2017 Intel Corporation.
 *
 * Author: Haozhong Zhang <haozhong.zhang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/pmem.h"

static size_t cache_line_size;

typedef void (*cache_flush_func_t)(void *p);
typedef void (*store_fence_func_t)(void);

static cache_flush_func_t cache_flush_func;
static store_fence_func_t store_fence_func;

#if defined(__x86_64__) || defined(__i386__)

#define CPUID_1_0_EBX_CLSIZE_MASK   0x0000ff00
#define CPUID_1_0_EBX_CLSIZE_SHIFT  8
#define CPUID_1_0_EDX_CLFLUSH       (1U << 19)
#define CPUID_7_0_EBX_CLFLUSHOPT    (1U << 23)
#define CPUID_7_0_EBX_CLWB          (1U << 24)

static inline void cpuid(uint32_t function, uint32_t count,
                         uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx)
{
    uint32_t vec[4];

#ifdef __x86_64__
    asm volatile("cpuid"
                 : "=a"(vec[0]), "=b"(vec[1]),
                   "=c"(vec[2]), "=d"(vec[3])
                 : "0"(function), "c"(count) : "cc");
#else
    asm volatile("pusha\n\t"
                 "cpuid\n\t"
                 "mov %%eax, 0(%2)\n\t"
                 "mov %%ebx, 4(%2)\n\t"
                 "mov %%ecx, 8(%2)\n\t"
                 "mov %%edx, 12(%2)\n\t"
                 "popa"
                 : : "a"(function), "c"(count), "S"(vec)
                 : "memory", "cc");
#endif

    if (eax) {
        *eax = vec[0];
    }
    if (ebx) {
        *ebx = vec[1];
    }
    if (ecx) {
        *ecx = vec[2];
    }
    if (edx) {
        *edx = vec[3];
    }
}

static void clflush(void *p)
{
    asm volatile("clflush %0" : "+m" (*(volatile char *)p));
}

static void clflushopt(void *p)
{
    asm volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)p));
}

static void clwb(void *p)
{
    asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)p));
}

static void sfence(void)
{
    asm volatile("sfence" : : : "memory");
}

static void __attribute__((constructor)) init_funcs(void)
{
    uint32_t ebx, edx;

    cpuid(0x1, 0x0, NULL, &ebx, NULL, &edx);

    cache_line_size = ((ebx & CPUID_1_0_EBX_CLSIZE_MASK) >>
                       CPUID_1_0_EBX_CLSIZE_SHIFT) * 8;
    assert(cache_line_size && !(cache_line_size & (cache_line_size - 1)));

    cpuid(0x7, 0x0, NULL, &ebx, NULL, NULL);
    if (ebx & CPUID_7_0_EBX_CLWB) {
        cache_flush_func = clwb;
    } else if (ebx & CPUID_7_0_EBX_CLFLUSHOPT) {
        cache_flush_func = clflushopt;
    } else {
        if (edx & CPUID_1_0_EDX_CLFLUSH) {
            cache_flush_func = clflush;
        }
    }

    store_fence_func = sfence;
}

#endif /* __x86_64__ || __i386__ */

void pmem_persistent(void *p, unsigned long len)
{
    uintptr_t s, e;

    if (!cache_flush_func || !store_fence_func) {
        return;
    }

    s = (uintptr_t)p & ~(cache_line_size - 1);
    e = (uintptr_t)p + len;

    while (s < e) {
        cache_flush_func((void *)s);
        s +=  cache_line_size;
    }

    store_fence_func();
}
