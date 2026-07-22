/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Load/store for 512-bit atomic operations, generic version.
 */

#ifndef HOST_ATOMIC512_LDST_H
#define HOST_ATOMIC512_LDST_H

#include "host/cpuinfo.h"

#define HAVE_ATOMIC512_LD 0
#define HAVE_ATOMIC512_ST likely(cpuinfo & CPUINFO_MOVDIR64B)

Value64 QEMU_ERROR("unsupported atomic") atomic64_read(const void *s);

static inline void atomic64_set(void *d, const Value64 *v)
{
    asm volatile("movdir64b (%1),%0" : : "r"(d), "r"(v) : "memory");
}

#endif /* HOST_ATOMIC512_LDST_H */
