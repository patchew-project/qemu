/*
 * QEMU unaligned/endian-independent host pointer access
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_LDST_UNALIGNED_H
#define QEMU_LDST_UNALIGNED_H

/*
 * Any compiler worth its salt will turn these memcpy into native unaligned
 * operations.  Thus we don't need to play games with packed attributes, or
 * inline byte-by-byte stores.
 * Some compilation environments (eg some fortify-source implementations)
 * may intercept memcpy() in a way that defeats the compiler optimization,
 * though, so we use __builtin_memcpy() to give ourselves the best chance
 * of good performance.
 */

static inline int lduw_unaligned_p(const void *ptr)
{
    uint16_t r;
    __builtin_memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline int ldsw_unaligned_p(const void *ptr)
{
    int16_t r;
    __builtin_memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline void stw_unaligned_p(void *ptr, uint16_t v)
{
    __builtin_memcpy(ptr, &v, sizeof(v));
}

static inline void st24_unaligned_p(void *ptr, uint32_t v)
{
    __builtin_memcpy(ptr, &v, 3);
}

static inline int ldl_unaligned_p(const void *ptr)
{
    int32_t r;
    __builtin_memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline void stl_unaligned_p(void *ptr, uint32_t v)
{
    __builtin_memcpy(ptr, &v, sizeof(v));
}

static inline uint64_t ldq_unaligned_p(const void *ptr)
{
    uint64_t r;
    __builtin_memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline void stq_unaligned_p(void *ptr, uint64_t v)
{
    __builtin_memcpy(ptr, &v, sizeof(v));
}

#endif
