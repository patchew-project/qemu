/*
 * Atomic helper templates
 * Included from tcg-runtime.c and cputlb.c.
 *
 * Copyright (c) 2016 Red Hat, Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/plugin.h"

#if DATA_SIZE == 16
# define SUFFIX     o
# define DATA_TYPE  Int128
# define BSWAP      bswap128
# define SHIFT      4
#elif DATA_SIZE == 8
# define SUFFIX     q
# define DATA_TYPE  aligned_uint64_t
# define SDATA_TYPE aligned_int64_t
# define BSWAP      bswap64
# define SHIFT      3
#elif DATA_SIZE == 4
# define SUFFIX     l
# define DATA_TYPE  uint32_t
# define SDATA_TYPE int32_t
# define BSWAP      bswap32
# define SHIFT      2
#elif DATA_SIZE == 2
# define SUFFIX     w
# define DATA_TYPE  uint16_t
# define SDATA_TYPE int16_t
# define BSWAP      bswap16
# define SHIFT      1
#elif DATA_SIZE == 1
# define SUFFIX     b
# define DATA_TYPE  uint8_t
# define SDATA_TYPE int8_t
# define BSWAP
# define SHIFT      0
#else
# error unsupported data size
#endif

#if DATA_SIZE == 16
# define VALUE_LOW(val) int128_getlo(val)
# define VALUE_HIGH(val) int128_gethi(val)
#else
# define VALUE_LOW(val) val
# define VALUE_HIGH(val) 0
#endif

#if DATA_SIZE >= 4
# define ABI_TYPE  DATA_TYPE
#else
# define ABI_TYPE  uint32_t
#endif

/* Define host-endian atomic operations.  Note that END is used within
   the ATOMIC_NAME macro, and redefined below.  */
# define END

ABI_TYPE ATOMIC_NAME(cmpxchg)(CPUArchState *env, vaddr addr,
                              ABI_TYPE cmpv, ABI_TYPE newv,
                              MemOpIdx oi, uintptr_t retaddr)
{
    bool need_bswap = get_memop(oi) & MO_BSWAP;
    DATA_TYPE *haddr = atomic_mmu_lookup(env_cpu(env), addr, oi,
                                         DATA_SIZE, retaddr, &need_bswap);
    DATA_TYPE ret, ret_e;
    if (need_bswap) {
#if DATA_SIZE == 16
        ret = atomic16_cmpxchg(haddr, BSWAP(cmpv), BSWAP(newv));
#else
        ret = qatomic_cmpxchg__nocheck(haddr, BSWAP(cmpv), BSWAP(newv));
#endif
        ret_e = BSWAP(ret);
    } else {
#if DATA_SIZE == 16
        ret = atomic16_cmpxchg(haddr, cmpv, newv);
#else
        ret = qatomic_cmpxchg__nocheck(haddr, cmpv, newv);
#endif
        ret_e = ret;
    }
    ATOMIC_MMU_CLEANUP;
    atomic_trace_rmw_post(env, addr,
                        VALUE_LOW(ret),
                        VALUE_HIGH(ret),
                        VALUE_LOW(newv),
                        VALUE_HIGH(newv),
                        oi);
    return ret_e;
}

ABI_TYPE ATOMIC_NAME(xchg)(CPUArchState *env, vaddr addr, ABI_TYPE val,
                           MemOpIdx oi, uintptr_t retaddr)
{
    bool need_bswap = get_memop(oi) & MO_BSWAP;

    DATA_TYPE *haddr = atomic_mmu_lookup(env_cpu(env), addr, oi,
                                         DATA_SIZE, retaddr, &need_bswap);
    DATA_TYPE ret, ret_e;

    if (need_bswap) {
        #if DATA_SIZE == 16
            ret = atomic16_xchg(haddr, BSWAP(val));
        #else
            ret = qatomic_xchg__nocheck(haddr, BSWAP(val));
        #endif

        ret_e = BSWAP(ret);
    } else {
        #if DATA_SIZE == 16
            ret = atomic16_xchg(haddr, val);
        #else
            ret = qatomic_xchg__nocheck(haddr, val);
        #endif
        ret_e = ret;
    }
    ATOMIC_MMU_CLEANUP;
    atomic_trace_rmw_post(env, addr,
                        VALUE_LOW(ret),
                        VALUE_HIGH(ret),
                        VALUE_LOW(val),
                        VALUE_HIGH(val),
                        oi);
    return ret_e;
}

#if DATA_SIZE == 16
ABI_TYPE ATOMIC_NAME(fetch_and)(CPUArchState *env, vaddr addr, ABI_TYPE val,
                                MemOpIdx oi, uintptr_t retaddr)
{
    bool need_bswap = get_memop(oi) & MO_BSWAP;

    DATA_TYPE *haddr = atomic_mmu_lookup(env_cpu(env), addr, oi,
                                         DATA_SIZE, retaddr, &need_bswap);
    DATA_TYPE ret, ret_e;
    if (need_bswap) {
        ret = atomic16_fetch_and(haddr, BSWAP(val));
        ret_e = BSWAP(ret);
    } else {
        ret = atomic16_fetch_and(haddr, val);
        ret_e = ret;
    }
    ATOMIC_MMU_CLEANUP;
    atomic_trace_rmw_post(env, addr,
                          VALUE_LOW(ret),
                          VALUE_HIGH(ret),
                          VALUE_LOW(val),
                          VALUE_HIGH(val),
                          oi);
    return ret_e;
}

ABI_TYPE ATOMIC_NAME(fetch_or)(CPUArchState *env, vaddr addr, ABI_TYPE val,
                               MemOpIdx oi, uintptr_t retaddr)
{
    bool need_bswap = get_memop(oi) & MO_BSWAP;

    DATA_TYPE *haddr = atomic_mmu_lookup(env_cpu(env), addr, oi,
                                         DATA_SIZE, retaddr, &need_bswap);
    DATA_TYPE ret, ret_e;
    if (need_bswap) {
        ret = atomic16_fetch_or(haddr, BSWAP(val));
        ret_e = BSWAP(ret);
    } else {
        ret = atomic16_fetch_or(haddr, val);
        ret_e = ret;
    }

    ATOMIC_MMU_CLEANUP;
    atomic_trace_rmw_post(env, addr,
                          VALUE_LOW(ret),
                          VALUE_HIGH(ret),
                          VALUE_LOW(val),
                          VALUE_HIGH(val),
                          oi);
    return ret_e;
}
#else
#define GEN_ATOMIC_HELPER(X)                                        \
ABI_TYPE ATOMIC_NAME(X)(CPUArchState *env, vaddr addr,              \
                        ABI_TYPE val, MemOpIdx oi, uintptr_t retaddr) \
{                                                                   \
    DATA_TYPE *haddr, ret, ret_e;                                   \
    bool need_bswap = get_memop(oi) & MO_BSWAP;                     \
    haddr = atomic_mmu_lookup(env_cpu(env), addr, oi, DATA_SIZE,    \
                                             retaddr, &need_bswap); \
    if (need_bswap) {                                               \
        ret = qatomic_##X(haddr, BSWAP(val));                       \
        ret_e = BSWAP(ret);                                         \
    }                                                               \
    else {                                                          \
        ret = qatomic_##X(haddr, val);                              \
        ret_e = ret;                                                \
    }                                                               \
    ATOMIC_MMU_CLEANUP;                                             \
    atomic_trace_rmw_post(env, addr,                                \
                          VALUE_LOW(ret),                           \
                          VALUE_HIGH(ret),                          \
                          VALUE_LOW(val),                           \
                          VALUE_HIGH(val),                          \
                          oi);                                      \
    return ret_e;                                                   \
}

GEN_ATOMIC_HELPER(fetch_add)
GEN_ATOMIC_HELPER(fetch_and)
GEN_ATOMIC_HELPER(fetch_or)
GEN_ATOMIC_HELPER(fetch_xor)
GEN_ATOMIC_HELPER(add_fetch)
GEN_ATOMIC_HELPER(and_fetch)
GEN_ATOMIC_HELPER(or_fetch)
GEN_ATOMIC_HELPER(xor_fetch)

#undef GEN_ATOMIC_HELPER

/*
 * These helpers are, as a whole, full barriers.  Within the helper,
 * the leading barrier is explicit and the trailing barrier is within
 * cmpxchg primitive.
 *
 * Trace this load + RMW loop as a single RMW op. This way, regardless
 * of CF_PARALLEL's value, we'll trace just a read and a write.
 */

#define GEN_ATOMIC_HELPER_FN(X, FN, XDATA_TYPE, RET)                \
ABI_TYPE ATOMIC_NAME(X)(CPUArchState *env, vaddr addr,              \
                        ABI_TYPE xval, MemOpIdx oi, uintptr_t retaddr) \
{                                                                   \
    XDATA_TYPE *haddr, ldo, ldn, old, new, val = xval;              \
    bool need_bswap = get_memop(oi) & MO_BSWAP;                     \
    haddr = atomic_mmu_lookup(env_cpu(env), addr, oi, DATA_SIZE,    \
                              retaddr, &need_bswap);                \
    smp_mb();                                                       \
    ldn = qatomic_read__nocheck(haddr);                             \
    if (need_bswap) {                                               \
        do {                                                        \
            ldo = ldn; old = BSWAP(ldo);                            \
            new = FN(old, val);                                     \
            ldn = qatomic_cmpxchg__nocheck(haddr, ldo, BSWAP(new)); \
        } while (ldo != ldn);                                       \
    }                                                               \
    else{                                                           \
        do {                                                        \
            ldo = ldn; old = ldo;                                   \
            new = FN(old, val);                                     \
            ldn = qatomic_cmpxchg__nocheck(haddr, ldo, new);        \
        } while (ldo != ldn);                                       \
    }                                                               \
    ATOMIC_MMU_CLEANUP;                                             \
    atomic_trace_rmw_post(env, addr,                                \
                        VALUE_LOW(old),                             \
                        VALUE_HIGH(old),                            \
                        VALUE_LOW(xval),                            \
                        VALUE_HIGH(xval),                           \
                        oi);                                        \
    return RET;                                                     \
}

GEN_ATOMIC_HELPER_FN(fetch_smin, MIN, SDATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(fetch_umin, MIN,  DATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(fetch_smax, MAX, SDATA_TYPE, old)
GEN_ATOMIC_HELPER_FN(fetch_umax, MAX,  DATA_TYPE, old)

GEN_ATOMIC_HELPER_FN(smin_fetch, MIN, SDATA_TYPE, new)
GEN_ATOMIC_HELPER_FN(umin_fetch, MIN,  DATA_TYPE, new)
GEN_ATOMIC_HELPER_FN(smax_fetch, MAX, SDATA_TYPE, new)
GEN_ATOMIC_HELPER_FN(umax_fetch, MAX,  DATA_TYPE, new)

#undef GEN_ATOMIC_HELPER_FN
#endif /* DATA SIZE == 16 */

#undef END

#undef BSWAP
#undef ABI_TYPE
#undef DATA_TYPE
#undef SDATA_TYPE
#undef SUFFIX
#undef DATA_SIZE
#undef SHIFT
#undef VALUE_LOW
#undef VALUE_HIGH
