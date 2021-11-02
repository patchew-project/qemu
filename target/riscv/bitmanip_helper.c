/*
 * RISC-V Bitmanip Extension Helpers for QEMU.
 *
 * Copyright (c) 2020 Kito Cheng, kito.cheng@sifive.com
 * Copyright (c) 2020 Frank Chang, frank.chang@sifive.com
 * Copyright (c) 2021 Philipp Tomsich, philipp.tomsich@vrull.eu
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

target_ulong HELPER(clmul)(target_ulong rs1, target_ulong rs2)
{
    target_ulong result = 0;

    for (int i = 0; i < TARGET_LONG_BITS; i++) {
        if ((rs2 >> i) & 1) {
            result ^= (rs1 << i);
        }
    }

    return result;
}

target_ulong HELPER(clmulr)(target_ulong rs1, target_ulong rs2)
{
    target_ulong result = 0;

    for (int i = 0; i < TARGET_LONG_BITS; i++) {
        if ((rs2 >> i) & 1) {
            result ^= (rs1 >> (TARGET_LONG_BITS - i - 1));
        }
    }

    return result;
}

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

target_ulong HELPER(xperm)(target_ulong rs1, target_ulong rs2, uint32_t sz_log2)
{
    target_ulong r = 0;
    target_ulong sz = 1LL << sz_log2;
    target_ulong mask = (1LL << sz) - 1;
    for (int i = 0; i < TARGET_LONG_BITS; i += sz) {
        target_ulong pos = ((rs2 >> i) & mask) << sz_log2;
        if (pos < sizeof(target_ulong) * 8) {
            r |= ((rs1 >> pos) & mask) << i;
        }
    }
    return r;
}

static const uint64_t shuf_masks[] = {
    dup_const(MO_8, 0x44),
    dup_const(MO_8, 0x30),
    dup_const(MO_16, 0x0f00),
    dup_const(MO_32, 0xff0000),
    dup_const(MO_64, 0xffff00000000)
};

static inline target_ulong do_shuf_stage(target_ulong src, uint64_t maskL,
                                         uint64_t maskR, int shift)
{
    target_ulong x = src & ~(maskL | maskR);
    x |= ((src << shift) & maskL) | ((src >> shift) & maskR);
    return x;
}

target_ulong HELPER(unshfl)(target_ulong rs1,
                            target_ulong rs2)
{
    target_ulong x = rs1;
    int i, shift;
    int bits = TARGET_LONG_BITS >> 1;
    for (i = 0, shift = 1; shift < bits; i++, shift <<= 1) {
        if (rs2 & shift) {
            x = do_shuf_stage(x, shuf_masks[i], shuf_masks[i] >> shift, shift);
        }
    }
    return x;
}

target_ulong HELPER(shfl)(target_ulong rs1,
                          target_ulong rs2)
{
    target_ulong x = rs1;
    int i, shift;
    shift = TARGET_LONG_BITS >> 2;
    i = (shift == 8) ? 3 : 4;
    for (; i >= 0; i--, shift >>= 1) {
        if (rs2 & shift) {
            x = do_shuf_stage(x, shuf_masks[i], shuf_masks[i] >> shift, shift);
        }
    }
    return x;
}
