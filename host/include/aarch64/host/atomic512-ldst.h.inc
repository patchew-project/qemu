/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Load/store for 512-bit atomic operations, AArch64 version.
 */

#ifndef HOST_ATOMIC512_LDST_H
#define HOST_ATOMIC512_LDST_H

#ifdef CONFIG_ARM_LS64_BUILTIN
#include "host/cpuinfo.h"
#include <arm_acle.h>

#ifdef __ARM_FEATURE_LS64
# define HAVE_ATOMIC512_LD  true
# define ATTR_LS64_ACCEL
#else
# define HAVE_ATOMIC512_LD  likely(cpuinfo & CPUINFO_LS64)
# define ATTR_LS64_ACCEL  __attribute__((target("+ls64")))
#endif
#define HAVE_ATOMIC512_ST   HAVE_ATOMIC512_LD

static inline Value64 ATTR_LS64_ACCEL atomic64_read(const void *s)
{
    return (Value64){ .arm = __arm_ld64b(s) };
}

static inline void ATTR_LS64_ACCEL atomic64_set(void *d, const Value64 *v)
{
    __arm_st64b(d, v->arm);
}

#else
#include "host/include/generic/host/atomic512-ldst.h.inc"
#endif

#endif /* HOST_ATOMIC512_LDST_H */
