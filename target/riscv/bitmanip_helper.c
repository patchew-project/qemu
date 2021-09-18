/*
 * RISC-V Bitmanip Extension Helpers for QEMU.
 *
 * Copyright (c) 2020 Kito Cheng, kito.cheng@sifive.com
 * Copyright (c) 2020 Frank Chang, frank.chang@sifive.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "tcg/tcg.h"

static const uint64_t adjacent_masks[] = {
    dup_const(MO_8, 0x55),
    dup_const(MO_8, 0x33),
    dup_const(MO_8, 0x0f),
    dup_const(MO_16, 0xff),
    dup_const(MO_32, 0xffff),
    UINT32_MAX
};

static inline target_ulong do_swap(target_ulong x, uint64_t mask, int shift)
{
    return ((x & mask) << shift) | ((x & ~mask) >> shift);
}

static target_ulong do_grev(target_ulong rs1,
                            target_ulong rs2,
                            int bits)
{
    target_ulong x = rs1;
    int i, shift;

    for (i = 0, shift = 1; shift < bits; i++, shift <<= 1) {
        if (rs2 & shift) {
            x = do_swap(x, adjacent_masks[i], shift);
        }
    }

    return x;
}

target_ulong HELPER(grev)(target_ulong rs1, target_ulong rs2)
{
    return do_grev(rs1, rs2, TARGET_LONG_BITS);
}

target_ulong HELPER(grevw)(target_ulong rs1, target_ulong rs2)
{
    return do_grev(rs1, rs2, 32);
}

static target_ulong do_gorc(target_ulong rs1,
                            target_ulong rs2,
                            int bits)
{
    target_ulong x = rs1;
    int i, shift;

    for (i = 0, shift = 1; shift < bits; i++, shift <<= 1) {
        if (rs2 & shift) {
            x |= do_swap(x, adjacent_masks[i], shift);
        }
    }

    return x;
}

target_ulong HELPER(gorc)(target_ulong rs1, target_ulong rs2)
{
    return do_gorc(rs1, rs2, TARGET_LONG_BITS);
}

target_ulong HELPER(gorcw)(target_ulong rs1, target_ulong rs2)
{
    return do_gorc(rs1, rs2, 32);
}

#define DO_CLMULA(NAME, NUM, BODY)                          \
static target_ulong do_##NAME(target_ulong rs1,             \
                              target_ulong rs2,             \
                              int bits)                     \
{                                                           \
    target_ulong x = 0;                                     \
    int i;                                                  \
                                                            \
    for (i = NUM; i < bits; i++) {                          \
        if ((rs2 >> i) & 1) {                               \
            x ^= BODY;                                      \
        }                                                   \
    }                                                       \
    return x;                                               \
}

DO_CLMULA(clmul, 0, (rs1 << i))
DO_CLMULA(clmulh, 1, (rs1 >> (bits - i)))
DO_CLMULA(clmulr, 0, (rs1 >> (bits - i - 1)))

target_ulong HELPER(clmul)(target_ulong rs1, target_ulong rs2)
{
    return do_clmul(rs1, rs2, TARGET_LONG_BITS);
}

target_ulong HELPER(clmulh)(target_ulong rs1, target_ulong rs2)
{
    return do_clmulh(rs1, rs2, TARGET_LONG_BITS);
}

target_ulong HELPER(clmulr)(target_ulong rs1, target_ulong rs2)
{
    return do_clmulr(rs1, rs2, TARGET_LONG_BITS);
}

static target_ulong shuffle_stage(target_ulong src,
                                  uint64_t maskl,
                                  uint64_t maskr,
                                  int n)
{
    target_ulong x = src & ~(maskl | maskr);
    x |= ((src << n) & maskl) | ((src >> n) & maskr);
    return x;
}

static target_ulong do_shfl(target_ulong rs1,
                            target_ulong rs2,
                            int bits)
{
    target_ulong x = rs1;
    int shamt = rs2 & ((bits - 1) >> 1);

    if (shamt & 16) {
        x = shuffle_stage(x, 0x0000ffff00000000LL, 0x00000000ffff0000LL, 16);
    }
    if (shamt & 8) {
        x = shuffle_stage(x, 0x00ff000000ff0000LL, 0x0000ff000000ff00LL, 8);
    }
    if (shamt & 4) {
        x = shuffle_stage(x, 0x0f000f000f000f00LL, 0x00f000f000f000f0LL, 4);
    }
    if (shamt & 2) {
        x = shuffle_stage(x, 0x3030303030303030LL, 0x0c0c0c0c0c0c0c0cLL, 2);
    }
    if (shamt & 1) {
        x = shuffle_stage(x, 0x4444444444444444LL, 0x2222222222222222LL, 1);
    }

    return x;
}

static target_ulong do_unshfl(target_ulong rs1,
                              target_ulong rs2,
                              int bits)
{
    target_ulong x = rs1;
    int shamt = rs2 & ((bits - 1) >> 1);

    if (shamt & 1) {
        x = shuffle_stage(x, 0x4444444444444444LL, 0x2222222222222222LL, 1);
    }
    if (shamt & 2) {
        x = shuffle_stage(x, 0x3030303030303030LL, 0x0c0c0c0c0c0c0c0cLL, 2);
    }
    if (shamt & 4) {
        x = shuffle_stage(x, 0x0f000f000f000f00LL, 0x00f000f000f000f0LL, 4);
    }
    if (shamt & 8) {
        x = shuffle_stage(x, 0x00ff000000ff0000LL, 0x0000ff000000ff00LL, 8);
    }
    if (shamt & 16) {
        x = shuffle_stage(x, 0x0000ffff00000000LL, 0x00000000ffff0000LL, 16);
    }

    return x;
}

target_ulong HELPER(shfl)(target_ulong rs1, target_ulong rs2)
{
    return do_shfl(rs1, rs2, TARGET_LONG_BITS);
}

target_ulong HELPER(unshfl)(target_ulong rs1, target_ulong rs2)
{
    return do_unshfl(rs1, rs2, TARGET_LONG_BITS);
}

target_ulong HELPER(shflw)(target_ulong rs1, target_ulong rs2)
{
    return do_shfl(rs1, rs2, 32);
}

target_ulong HELPER(unshflw)(target_ulong rs1, target_ulong rs2)
{
    return do_unshfl(rs1, rs2, 32);
}
