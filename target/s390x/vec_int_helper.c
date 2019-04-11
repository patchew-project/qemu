/*
 * QEMU TCG support -- s390x vector integer instruction support
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "vec.h"
#include "exec/helper-proto.h"

/*
 * Add two 128 bit vectors, returning the carry.
 */
static bool s390_vec_add(S390Vector *d, const S390Vector *a,
                         const S390Vector *b)
{
    bool low_carry = false, high_carry = false;

    if (a->doubleword[0] + b->doubleword[0] < a->doubleword[0]) {
        high_carry = true;
    }
    if (a->doubleword[1] + b->doubleword[1] < a->doubleword[1]) {
        low_carry = true;
        if (a->doubleword[0] == b->doubleword[0]) {
            high_carry = true;
        }
    }
    d->doubleword[0] = a->doubleword[0] + b->doubleword[0] + low_carry;
    d->doubleword[1] = a->doubleword[1] + b->doubleword[1];
    return high_carry;
}

void HELPER(gvec_vacc128)(void *v1, const void *v2, const void *v3,
                          uint32_t desc)
{
    S390Vector tmp, *dst = v1;

    dst->doubleword[0] = 0;
    dst->doubleword[1] = s390_vec_add(&tmp, v2, v3);
}

void HELPER(gvec_vaccc128)(void *v1, const void *v2, const void *v3,
                           const void *v4, uint32_t desc)
{
    const S390Vector old_carry = {
        .doubleword[0] = 0,
        .doubleword[1] = ((S390Vector *)v4)->doubleword[1] & 1,
    };
    S390Vector tmp, *dst = v1;
    bool carry;

    carry = s390_vec_add(&tmp, v2, v3);
    carry |= s390_vec_add(&tmp, &tmp, &old_carry);
    dst->doubleword[0] = 0;
    dst->doubleword[1] = carry;
}

#define DEF_VAVG(BITS)                                                         \
void HELPER(gvec_vavg##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const int32_t a = (int##BITS##_t)s390_vec_read_element##BITS(v2, i);   \
        const int32_t b = (int##BITS##_t)s390_vec_read_element##BITS(v3, i);   \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a + b + 1) >> 1);                 \
    }                                                                          \
}
DEF_VAVG(8)
DEF_VAVG(16)

#define DEF_VAVGL(BITS)                                                        \
void HELPER(gvec_vavgl##BITS)(void *v1, const void *v2, const void *v3,        \
                              uint32_t desc)                                   \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
        const uint##BITS##_t b = s390_vec_read_element##BITS(v3, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a + b + 1) >> 1);                 \
    }                                                                          \
}
DEF_VAVGL(8)
DEF_VAVGL(16)

#define DEF_VCLZ(BITS)                                                         \
void HELPER(gvec_vclz##BITS)(void *v1, const void *v2, uint32_t desc)          \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, clz32(a) - 32 + BITS);             \
    }                                                                          \
}
DEF_VCLZ(8)
DEF_VCLZ(16)
