/*
 * RISC-V Crypto Emulation Helpers for QEMU.
 *
 * Copyright (c) 2021 Ruibo Lu, luruibo2000@163.com
 * Copyright (c) 2021 Zewen Ye, lustrew@foxmail.com
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "crypto/aes.h"
#include "crypto/sm4.h"

#define AES_XTIME(a) \
    ((a << 1) ^ ((a & 0x80) ? 0x1b : 0))

#define AES_GFMUL(a, b) (( \
    (((b) & 0x1) ? (a) : 0) ^ \
    (((b) & 0x2) ? AES_XTIME(a) : 0) ^ \
    (((b) & 0x4) ? AES_XTIME(AES_XTIME(a)) : 0) ^ \
    (((b) & 0x8) ? AES_XTIME(AES_XTIME(AES_XTIME(a))) : 0)) & 0xFF)

#define BY(X, I) ((X >> (8 * I)) & 0xFF)

#define AES_SHIFROWS_LO(RS1, RS2) ( \
    (((RS1 >> 24) & 0xFF) << 56) | (((RS2 >> 48) & 0xFF) << 48) | \
    (((RS2 >> 8) & 0xFF) << 40) | (((RS1 >> 32) & 0xFF) << 32) | \
    (((RS2 >> 56) & 0xFF) << 24) | (((RS2 >> 16) & 0xFF) << 16) | \
    (((RS1 >> 40) & 0xFF) << 8) | (((RS1 >> 0) & 0xFF) << 0))

#define AES_INVSHIFROWS_LO(RS1, RS2) ( \
    (((RS2 >> 24) & 0xFF) << 56) | (((RS2 >> 48) & 0xFF) << 48) | \
    (((RS1 >> 8) & 0xFF) << 40) | (((RS1 >> 32) & 0xFF) << 32) | \
    (((RS1 >> 56) & 0xFF) << 24) | (((RS2 >> 16) & 0xFF) << 16) | \
    (((RS2 >> 40) & 0xFF) << 8) | (((RS1 >> 0) & 0xFF) << 0))

#define AES_MIXBYTE(COL, B0, B1, B2, B3) ( \
    BY(COL, B3) ^ BY(COL, B2) ^ AES_GFMUL(BY(COL, B1), 3) ^ \
    AES_GFMUL(BY(COL, B0), 2))

#define AES_MIXCOLUMN(COL) ( \
    AES_MIXBYTE(COL, 3, 0, 1, 2) << 24 | \
    AES_MIXBYTE(COL, 2, 3, 0, 1) << 16 | \
    AES_MIXBYTE(COL, 1, 2, 3, 0) << 8 | AES_MIXBYTE(COL, 0, 1, 2, 3) << 0)

#define AES_INVMIXBYTE(COL, B0, B1, B2, B3) ( \
    AES_GFMUL(BY(COL, B3), 0x9) ^ AES_GFMUL(BY(COL, B2), 0xd) ^ \
    AES_GFMUL(BY(COL, B1), 0xb) ^ AES_GFMUL(BY(COL, B0), 0xe))

#define AES_INVMIXCOLUMN(COL) ( \
    AES_INVMIXBYTE(COL, 3, 0, 1, 2) << 24 | \
    AES_INVMIXBYTE(COL, 2, 3, 0, 1) << 16 | \
    AES_INVMIXBYTE(COL, 1, 2, 3, 0) << 8 | \
    AES_INVMIXBYTE(COL, 0, 1, 2, 3) << 0)

static inline uint32_t aes_mixcolumn_byte(uint8_t x, bool fwd)
{
    uint32_t u;

    if (fwd) {
        u = (AES_GFMUL(x, 3) << 24) | (x << 16) | (x << 8) |
            (AES_GFMUL(x, 2) << 0);
    } else {
        u = (AES_GFMUL(x, 0xb) << 24) | (AES_GFMUL(x, 0xd) << 16) |
            (AES_GFMUL(x, 0x9) << 8) | (AES_GFMUL(x, 0xe) << 0);
    }
    return u;
}

#define sext_xlen(x) (target_ulong)(int32_t)(x)

static inline target_ulong aes32_operation(target_ulong bs, target_ulong rs1,
                                           target_ulong rs2, bool enc,
                                           bool mix)
{
    uint8_t shamt = bs << 3;
    uint8_t si = rs2 >> shamt;
    uint8_t so;
    uint32_t mixed;
    target_ulong res;

    if (enc) {
        so = AES_sbox[si];
        if (mix) {
            mixed = aes_mixcolumn_byte(so, true);
        } else {
            mixed = so;
        }
    } else {
        so = AES_isbox[si];
        if (mix) {
            mixed = aes_mixcolumn_byte(so, false);
        } else {
            mixed = so;
        }
    }
    mixed = (mixed << shamt) | (mixed >> (32 - shamt));
    res = rs1 ^ mixed;

    return sext_xlen(res);
}

target_ulong HELPER(aes32esmi)(target_ulong rs1, target_ulong rs2,
                               target_ulong bs)
{
    return aes32_operation(bs, rs1, rs2, true, true);
}

target_ulong HELPER(aes32esi)(target_ulong rs1, target_ulong rs2,
                              target_ulong bs)
{
    return aes32_operation(bs, rs1, rs2, true, false);
}

target_ulong HELPER(aes32dsmi)(target_ulong rs1, target_ulong rs2,
                               target_ulong bs)
{
    return aes32_operation(bs, rs1, rs2, false, true);
}

target_ulong HELPER(aes32dsi)(target_ulong rs1, target_ulong rs2,
                              target_ulong bs)
{
    return aes32_operation(bs, rs1, rs2, false, false);
}
#undef sext_xlen
