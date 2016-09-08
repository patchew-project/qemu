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

void helper_sr(uint32_t val, uint32_t aux)
{
    switch (aux) {
        case AUX_ID_STATUS: {
        } break;

        case AUX_ID_SEMAPHORE: {
        } break;

        case AUX_ID_LP_START: {
        } break;

        case AUX_ID_LP_END: {
        } break;

        case AUX_ID_IDENTITY: {
        } break;

        case AUX_ID_DEBUG: {
        } break;

        case AUX_ID_PC: {
        } break;

        case AUX_ID_STATUS32: {
        } break;

        case AUX_ID_STATUS32_L1: {
        } break;

        case AUX_ID_STATUS32_L2: {
        } break;

        case AUX_ID_MULHI: {
        } break;

        case AUX_ID_INT_VECTOR_BASE: {
        } break;

        case AUX_ID_INT_MACMODE: {
        } break;

        case AUX_ID_IRQ_LV12: {
        } break;

        case AUX_ID_IRQ_LEV: {
        } break;

        case AUX_ID_IRQ_HINT: {
        } break;

        case AUX_ID_ERET: {
        } break;

        case AUX_ID_ERBTA: {
        } break;

        case AUX_ID_ERSTATUS: {
        } break;

        case AUX_ID_ECR: {
        } break;

        case AUX_ID_EFA: {
        } break;

        case AUX_ID_ICAUSE1: {
        } break;

        case AUX_ID_ICAUSE2: {
        } break;

        case AUX_ID_IENABLE: {
        } break;

        case AUX_ID_ITRIGGER: {
        } break;

        case AUX_ID_BTA: {
        } break;

        case AUX_ID_BTA_L1: {
        } break;

        case AUX_ID_BTA_L2: {
        } break;

        case AUX_ID_IRQ_PULSE_CANSEL: {
        } break;

        case AUX_ID_IRQ_PENDING: {
        } break;

        default: {
            cpu_outl(aux, val);
        }
    }
    cpu_outl(aux, val);
}

static target_ulong get_status(CPUARCState *env)
{
    target_ulong res = 0x00000000;

    res |= (env->stat.Zf) ? BIT(31) : 0;
    res |= (env->stat.Nf) ? BIT(30) : 0;
    res |= (env->stat.Cf) ? BIT(29) : 0;
    res |= (env->stat.Vf) ? BIT(28) : 0;
    res |= (env->stat.E2f) ? BIT(27) : 0;
    res |= (env->stat.E1f) ? BIT(26) : 0;

    if (env->stopped) {
        res |= BIT(25);
    }

    res |= (env->r[63] >> 2) & 0x03ffffff;

    return res;
}

static target_ulong get_status32(CPUARCState *env)
{
    target_ulong res = 0x00000000;

    res |= (env->stat.Lf) ? BIT(12) : 0;
    res |= (env->stat.Zf) ? BIT(11) : 0;
    res |= (env->stat.Nf) ? BIT(10) : 0;
    res |= (env->stat.Cf) ? BIT(9)  : 0;
    res |= (env->stat.Vf) ? BIT(8)  : 0;
    res |= (env->stat.Uf) ? BIT(7)  : 0;
    res |= (env->stat.DEf) ? BIT(6)  : 0;
    res |= (env->stat.AEf) ? BIT(5)  : 0;
    res |= (env->stat.A2f) ? BIT(4)  : 0;
    res |= (env->stat.A1f) ? BIT(3)  : 0;
    res |= (env->stat.E2f) ? BIT(2)  : 0;
    res |= (env->stat.E1f) ? BIT(1)  : 0;

    if (env->stopped) {
        res |= BIT(0);
    }

    return res;
}

static target_ulong get_status32_l1(CPUARCState *env)
{
    target_ulong res = 0x00000000;

    res |= (env->stat_l1.Lf) ? BIT(12) : 0;
    res |= (env->stat_l1.Zf) ? BIT(11) : 0;
    res |= (env->stat_l1.Nf) ? BIT(10) : 0;
    res |= (env->stat_l1.Cf) ? BIT(9)  : 0;
    res |= (env->stat_l1.Vf) ? BIT(8)  : 0;
    res |= (env->stat_l1.Uf) ? BIT(7)  : 0;
    res |= (env->stat_l1.DEf) ? BIT(6)  : 0;
    res |= (env->stat_l1.AEf) ? BIT(5)  : 0;
    res |= (env->stat_l1.A2f) ? BIT(4)  : 0;
    res |= (env->stat_l1.A1f) ? BIT(3)  : 0;
    res |= (env->stat_l1.E2f) ? BIT(2)  : 0;
    res |= (env->stat_l1.E1f) ? BIT(1)  : 0;

    return res;
}

static target_ulong get_status32_l2(CPUARCState *env)
{
    target_ulong res = 0x00000000;

    res |= (env->stat_l2.Lf) ? BIT(12) : 0;
    res |= (env->stat_l2.Zf) ? BIT(11) : 0;
    res |= (env->stat_l2.Nf) ? BIT(10) : 0;
    res |= (env->stat_l2.Cf) ? BIT(9)  : 0;
    res |= (env->stat_l2.Vf) ? BIT(8)  : 0;
    res |= (env->stat_l2.Uf) ? BIT(7)  : 0;
    res |= (env->stat_l2.DEf) ? BIT(6)  : 0;
    res |= (env->stat_l2.AEf) ? BIT(5)  : 0;
    res |= (env->stat_l2.A2f) ? BIT(4)  : 0;
    res |= (env->stat_l2.A1f) ? BIT(3)  : 0;
    res |= (env->stat_l2.E2f) ? BIT(2)  : 0;
    res |= (env->stat_l2.E1f) ? BIT(1)  : 0;

    return res;
}

static target_ulong get_debug(CPUARCState *env)
{
    target_ulong res = 0x00000000;

    res |= (env->debug.LD) ? BIT(31) : 0;
    res |= (env->debug.SH) ? BIT(30) : 0;
    res |= (env->debug.BH) ? BIT(29) : 0;
    res |= (env->debug.UB) ? BIT(28) : 0;
    res |= (env->debug.ZZ) ? BIT(27) : 0;
    res |= (env->debug.RA) ? BIT(22) : 0;
    res |= (env->debug.IS) ? BIT(11) : 0;
    res |= (env->debug.FH) ? BIT(1)  : 0;
    res |= (env->debug.SS) ? BIT(0)  : 0;

    return res;
}

target_ulong helper_lr(CPUARCState *env, uint32_t aux)
{
    target_ulong result = 0;

    switch (aux) {
        case AUX_ID_STATUS: {
            result = get_status(env);
        } break;

        /*
            NOTE: SEMAPHORE should be handled by a device
        */

        case AUX_ID_LP_START: {
            result = env->lps;
        } break;

        case AUX_ID_LP_END: {
            result = env->lpe;
        } break;

        case AUX_ID_IDENTITY: {
        } break;

        case AUX_ID_DEBUG: {
            result = get_debug(env);
        } break;

        case AUX_ID_PC: {
            result = env->pc & 0xfffffffe;
        } break;

        case AUX_ID_STATUS32: {
            result = get_status32(env);
        } break;

        case AUX_ID_STATUS32_L1: {
            result = get_status32_l1(env);
        } break;

        case AUX_ID_STATUS32_L2: {
            result = get_status32_l2(env);
        } break;

        case AUX_ID_MULHI: {
            result = CPU_MHI(env);
        } break;

        case AUX_ID_INT_VECTOR_BASE: {
            result = env->intvec;
        } break;

        case AUX_ID_INT_MACMODE: {
        } break;

        case AUX_ID_IRQ_LV12: {
        } break;

        case AUX_ID_IRQ_LEV: {
        } break;

        case AUX_ID_IRQ_HINT: {
        } break;

        case AUX_ID_ERET: {
            result = env->eret;
        } break;

        case AUX_ID_ERBTA: {
            result = env->erbta;
        } break;

        case AUX_ID_ERSTATUS: {
        } break;

        case AUX_ID_ECR: {
            result = env->ecr;
        } break;

        case AUX_ID_EFA: {
            result = env->efa;
        } break;

        case AUX_ID_ICAUSE1: {
        } break;

        case AUX_ID_ICAUSE2: {
        } break;

        case AUX_ID_IENABLE: {
        } break;

        case AUX_ID_ITRIGGER: {
        } break;

        case AUX_ID_BTA: {
            result = env->bta;
        } break;

        case AUX_ID_BTA_L1: {
            result = env->bta_l1;
        } break;

        case AUX_ID_BTA_L2: {
            result = env->bta_l2;
        } break;

        case AUX_ID_IRQ_PULSE_CANSEL: {
        } break;

        case AUX_ID_IRQ_PENDING: {
        } break;

        default: {
            result = cpu_inl(aux);
        }
    }

    return  result;
}

