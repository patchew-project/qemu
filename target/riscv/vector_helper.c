/*
 * RISC-V Vectore Extension Helpers for QEMU.
 *
 * Copyright (c) 2019 C-SKY Limited. All rights reserved.
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
#include <math.h>

#define VECTOR_HELPER(name) HELPER(glue(vector_, name))

static inline void vector_vtype_set_ill(CPURISCVState *env)
{
    env->vfp.vtype = ((target_ulong)1) << (sizeof(target_ulong) - 1);
    return;
}

static inline int vector_vtype_get_sew(CPURISCVState *env)
{
    return (env->vfp.vtype >> 2) & 0x7;
}

static inline int vector_get_width(CPURISCVState *env)
{
    return  8 * (1 << vector_vtype_get_sew(env));
}

static inline int vector_get_lmul(CPURISCVState *env)
{
    return 1 << (env->vfp.vtype & 0x3);
}

static inline int vector_get_vlmax(CPURISCVState *env)
{
    return vector_get_lmul(env) * VLEN / vector_get_width(env);
}

void VECTOR_HELPER(vsetvl)(CPURISCVState *env, uint32_t rs1, uint32_t rs2,
    uint32_t rd)
{
    int sew, max_sew, vlmax, vl;

    if (rs2 == 0) {
        vector_vtype_set_ill(env);
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
        return;
    }
    env->vfp.vtype = env->gpr[rs2];
    sew = 1 << vector_get_width(env) / 8;
    max_sew = sizeof(target_ulong);

    if (env->misa & RVD) {
        max_sew = max_sew > 8 ? max_sew : 8;
    } else if (env->misa & RVF) {
        max_sew = max_sew > 4 ? max_sew : 4;
    }
    if (sew > max_sew) {
        vector_vtype_set_ill(env);
        return;
    }

    vlmax = vector_get_vlmax(env);
    if (rs1 == 0) {
        vl = vlmax;
    } else if (env->gpr[rs1] <= vlmax) {
        vl = env->gpr[rs1];
    } else if (env->gpr[rs1] < 2 * vlmax) {
        vl = ceil(env->gpr[rs1] / 2);
    } else {
        vl = vlmax;
    }
    env->vfp.vl = vl;
    env->gpr[rd] = vl;
    env->vfp.vstart = 0;
    return;
}

void VECTOR_HELPER(vsetvli)(CPURISCVState *env, uint32_t rs1, uint32_t zimm,
    uint32_t rd)
{
    int sew, max_sew, vlmax, vl;

    env->vfp.vtype = zimm;
    sew = vector_get_width(env) / 8;
    max_sew = sizeof(target_ulong);

    if (env->misa & RVD) {
        max_sew = max_sew > 8 ? max_sew : 8;
    } else if (env->misa & RVF) {
        max_sew = max_sew > 4 ? max_sew : 4;
    }
    if (sew > max_sew) {
        vector_vtype_set_ill(env);
        return;
    }

    vlmax = vector_get_vlmax(env);
    if (rs1 == 0) {
        vl = vlmax;
    } else if (env->gpr[rs1] <= vlmax) {
        vl = env->gpr[rs1];
    } else if (env->gpr[rs1] < 2 * vlmax) {
        vl = ceil(env->gpr[rs1] / 2);
    } else {
        vl = vlmax;
    }
    env->vfp.vl = vl;
    env->gpr[rd] = vl;
    env->vfp.vstart = 0;
    return;
}
