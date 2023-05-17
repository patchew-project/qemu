/*
 * Macros for swapping a value if the endianness is different
 * between the target and the host.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef TSWAP_H
#define TSWAP_H

#include "hw/core/cpu.h"
#include "qemu/bswap.h"

/*
 * If we're in target-specific code, we can hard-code the swapping
 * condition, otherwise we have to do (slower) run-time checks.
 */
#ifdef NEED_CPU_H
#define target_needs_bswap()  (HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN)
#else
#define target_needs_bswap()  (target_words_bigendian() != HOST_BIG_ENDIAN)
#endif

static inline uint16_t tswap16(uint16_t s)
{
    if (target_needs_bswap()) {
        return bswap16(s);
    } else {
        return s;
    }
}

static inline uint32_t tswap32(uint32_t s)
{
    if (target_needs_bswap()) {
        return bswap32(s);
    } else {
        return s;
    }
}

static inline uint64_t tswap64(uint64_t s)
{
    if (target_needs_bswap()) {
        return bswap64(s);
    } else {
        return s;
    }
}

static inline void tswap16s(uint16_t *s)
{
    if (target_needs_bswap()) {
        *s = bswap16(*s);
    }
}

static inline void tswap32s(uint32_t *s)
{
    if (target_needs_bswap()) {
        *s = bswap32(*s);
    }
}

static inline void tswap64s(uint64_t *s)
{
    if (target_needs_bswap()) {
        *s = bswap64(*s);
    }
}

/*
 * Target-endianness CPU memory access functions. These fit into the
 * {ld,st}{type}{sign}{size}{endian}_p naming scheme described in bswap.h.
 */

static inline int lduw_p(const void *ptr)
{
    return (uint16_t)tswap16(lduw_he_p(ptr));
}

static inline int ldsw_p(const void *ptr)
{
    return (int16_t)tswap16(lduw_he_p(ptr));
}

static inline int ldl_p(const void *ptr)
{
    return tswap32(ldl_he_p(ptr));
}

static inline uint64_t ldq_p(const void *ptr)
{
    return tswap64(ldq_he_p(ptr));
}

static inline uint64_t ldn_p(const void *ptr, int sz)
{
    if (target_needs_bswap()) {
#if HOST_BIG_ENDIAN
        return ldn_le_p(ptr, sz);
#else
        return ldn_be_p(ptr, sz);
#endif
    } else {
        return ldn_he_p(ptr, sz);
    }
}

static inline void stw_p(void *ptr, uint16_t v)
{
    stw_he_p(ptr, tswap16(v));
}

static inline void stl_p(void *ptr, uint32_t v)
{
    stl_he_p(ptr, tswap32(v));
}

static inline void stq_p(void *ptr, uint64_t v)
{
    stq_he_p(ptr, tswap64(v));
}

static inline void stn_p(void *ptr, int sz, uint64_t v)
{
    if (target_needs_bswap()) {
#if HOST_BIG_ENDIAN
        stn_le_p(ptr, sz, v);
#else
        stn_be_p(ptr, sz, v);
#endif
    } else {
        stn_he_p(ptr, sz, v);
    }
}

#endif  /* TSWAP_H */
