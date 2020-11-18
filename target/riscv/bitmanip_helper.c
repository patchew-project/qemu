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

static inline target_ulong do_swap(target_ulong x, target_ulong mask, int shift)
{
    return ((x & mask) << shift) | ((x & ~mask) >> shift);
}

static target_ulong do_grev(target_ulong rs1,
                            target_ulong rs2,
                            const target_ulong masks[])
{
    target_ulong x = rs1;
    int shift = 1;
    int i = 0;

    while (shift < TARGET_LONG_BITS) {
        if (rs2 & shift) {
            x = do_swap(x, masks[i], shift);
        }
        shift <<= 1;
        ++i;
    }

    return x;
}

target_ulong HELPER(grev)(target_ulong rs1, target_ulong rs2)
{
    static const target_ulong masks[] = {
#ifdef TARGET_RISCV32
        0x55555555, 0x33333333, 0x0f0f0f0f,
        0x00ff00ff, 0x0000ffff,
#else
        0x5555555555555555, 0x3333333333333333,
        0x0f0f0f0f0f0f0f0f, 0x00ff00ff00ff00ff,
        0x0000ffff0000ffff, 0x00000000ffffffff,
#endif
    };

    return do_grev(rs1, rs2, masks);
}

/* RV64-only instruction */
#ifdef TARGET_RISCV64

target_ulong HELPER(grevw)(target_ulong rs1, target_ulong rs2)
{
    static const target_ulong masks[] = {
        0x55555555, 0x33333333, 0x0f0f0f0f,
        0x00ff00ff, 0x0000ffff,
    };

    return do_grev(rs1, rs2, masks);
}

#endif

static target_ulong do_gorc(target_ulong rs1,
                            target_ulong rs2,
                            const target_ulong masks[])
{
    target_ulong x = rs1;
    int shift = 1;
    int i = 0;

    while (shift < TARGET_LONG_BITS) {
        if (rs2 & shift) {
            x |= do_swap(x, masks[i], shift);
        }
        shift <<= 1;
        ++i;
    }

    return x;
}

target_ulong HELPER(gorc)(target_ulong rs1, target_ulong rs2)
{
    static const target_ulong masks[] = {
#ifdef TARGET_RISCV32
        0x55555555, 0x33333333, 0x0f0f0f0f,
        0x00ff00ff, 0x0000ffff,
#else
        0x5555555555555555, 0x3333333333333333, 0x0f0f0f0f0f0f0f0f,
        0x00ff00ff00ff00ff, 0x0000ffff0000ffff, 0x00000000ffffffff,
#endif
    };

    return do_gorc(rs1, rs2, masks);
}

/* RV64-only instruction */
#ifdef TARGET_RISCV64

target_ulong HELPER(gorcw)(target_ulong rs1, target_ulong rs2)
{
    static const target_ulong masks[] = {
        0x55555555, 0x33333333, 0x0f0f0f0f,
        0x00ff00ff, 0x0000ffff,
    };

    return do_gorc(rs1, rs2, masks);
}

#endif

