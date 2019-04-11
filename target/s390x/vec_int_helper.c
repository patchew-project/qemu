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
