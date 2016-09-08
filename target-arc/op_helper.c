/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2016 Michael Rolnik
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/ioport.h"

#define AUX_ID_STATUS           0x000
#define AUX_ID_SEMAPHORE        0x001
#define AUX_ID_LP_START         0x002
#define AUX_ID_LP_END           0x003
#define AUX_ID_IDENTITY         0x004
#define AUX_ID_DEBUG            0x005
#define AUX_ID_PC               0x006

#define AUX_ID_STATUS32         0x00A
#define AUX_ID_STATUS32_L1      0x00B
#define AUX_ID_STATUS32_L2      0x00C

#define AUX_ID_MULHI            0x012

#define AUX_ID_INT_VECTOR_BASE  0x025

#define AUX_ID_INT_MACMODE      0x041

#define AUX_ID_IRQ_LV12         0x043

#define AUX_ID_IRQ_LEV          0x200
#define AUX_ID_IRQ_HINT         0x201

#define AUX_ID_ERET             0x400
#define AUX_ID_ERBTA            0x401
#define AUX_ID_ERSTATUS         0x402
#define AUX_ID_ECR              0x403
#define AUX_ID_EFA              0x404

#define AUX_ID_ICAUSE1          0x40A
#define AUX_ID_ICAUSE2          0x40B
#define AUX_ID_IENABLE          0x40C
#define AUX_ID_ITRIGGER         0x40D

#define AUX_ID_BTA              0x412
#define AUX_ID_BTA_L1           0x413
#define AUX_ID_BTA_L2           0x414
#define AUX_ID_IRQ_PULSE_CANSEL 0x415
#define AUX_ID_IRQ_PENDING      0x416

target_ulong helper_norm(CPUARCState *env, uint32_t src1)
{
    if (src1 == 0x00000000 || src1 == 0xffffffff) {
        return 31;
    } else {
        if ((src1 & 0x80000000) == 0x80000000) {
            src1 = ~src1;
        }
        return clz32(src1) - 1;
    }
}

target_ulong helper_normw(CPUARCState *env, uint32_t src1)
{
    src1 &= 0xffff;

    if (src1 == 0x0000 || src1 == 0xffff) {
        return 15;
    } else {
        if ((src1 & 0x8000) == 0x8000) {
            src1 = ~src1 & 0xffff;
        }
        return clz32(src1) - 17;
    }
}

