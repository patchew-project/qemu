/*
 * ARM SME Operations
 *
 * Copyright (c) 2022 Linaro, Ltd.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/helper-proto.h"
#include "qemu/int128.h"
#include "vec_internal.h"

/* ResetSVEState */
void arm_reset_sve_state(CPUARMState *env)
{
    memset(env->vfp.zregs, 0, sizeof(env->vfp.zregs));
    /* Recall that FFR is stored as pregs[16]. */
    memset(env->vfp.pregs, 0, sizeof(env->vfp.pregs));
    vfp_set_fpcr(env, 0x0800009f);
}

void helper_set_pstate_sm(CPUARMState *env, uint32_t i)
{
    if (i == FIELD_EX64(env->svcr, SVCR, SM)) {
        return;
    }
    env->svcr ^= R_SVCR_SM_MASK;
    arm_reset_sve_state(env);
}

void helper_set_pstate_za(CPUARMState *env, uint32_t i)
{
    if (i == FIELD_EX64(env->svcr, SVCR, ZA)) {
        return;
    }
    env->svcr ^= R_SVCR_ZA_MASK;

    /*
     * ResetSMEState.
     *
     * SetPSTATE_ZA zeros on enable and disable.  We can zero this only
     * on enable: while disabled, the storage is inaccessible and the
     * value does not matter.  We're not saving the storage in vmstate
     * when disabled either.
     */
    if (i) {
        memset(env->zarray, 0, sizeof(env->zarray));
    }
}

void helper_sme_zero(CPUARMState *env, uint32_t imm, uint32_t svl)
{
    uint32_t i;

    /*
     * Special case clearing the entire ZA space.
     * This falls into the CONSTRAINED UNPREDICTABLE zeroing of any
     * parts of the ZA storage outside of SVL.
     */
    if (imm == 0xff) {
        memset(env->zarray, 0, sizeof(env->zarray));
        return;
    }

    /*
     * Recall that ZAnH.D[m] is spread across ZA[n+8*m].
     * Unless SVL == ARM_MAX_VQ, each row is discontiguous.
     */
    for (i = 0; i < svl; i++) {
        if (imm & (1 << (i % 8))) {
            memset(&env->zarray[i], 0, svl);
        }
    }
}

#define DO_MOVA_A(NAME, TYPE, H)                                        \
void HELPER(NAME)(void *za, void *vn, void *vg, uint32_t desc)          \
{                                                                       \
    int i, oprsz = simd_oprsz(desc);                                    \
    for (i = 0; i < oprsz; ) {                                          \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));                 \
        do {                                                            \
            if (pg & 1) {                                               \
                *(TYPE *)za = *(TYPE *)(vn + H(i));                     \
            }                                                           \
            za += sizeof(ARMVectorReg) * sizeof(TYPE);                  \
            i += sizeof(TYPE);                                          \
            pg >>= sizeof(TYPE);                                        \
        } while (i & 15);                                               \
    }                                                                   \
}

#define DO_MOVA_Z(NAME, TYPE, H)                                        \
void HELPER(NAME)(void *vd, void *za, void *vg, uint32_t desc)          \
{                                                                       \
    int i, oprsz = simd_oprsz(desc);                                    \
    for (i = 0; i < oprsz; ) {                                          \
        uint16_t pg = *(uint16_t *)(vg + H1_2(i >> 3));                 \
        do {                                                            \
            if (pg & 1) {                                               \
                *(TYPE *)(vd + H(i)) = *(TYPE *)za;                     \
            }                                                           \
            za += sizeof(ARMVectorReg) * sizeof(TYPE);                  \
            i += sizeof(TYPE);                                          \
            pg >>= sizeof(TYPE);                                        \
        } while (i & 15);                                               \
    }                                                                   \
}

DO_MOVA_A(sme_mova_avz_b, uint8_t, H1)
DO_MOVA_A(sme_mova_avz_h, uint16_t, H2)
DO_MOVA_A(sme_mova_avz_s, uint32_t, H4)

DO_MOVA_Z(sme_mova_zav_b, uint8_t, H1)
DO_MOVA_Z(sme_mova_zav_h, uint16_t, H2)
DO_MOVA_Z(sme_mova_zav_s, uint32_t, H4)

void HELPER(sme_mova_avz_d)(void *za, void *vn, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pg = vg;
    uint64_t *n = vn;
    uint64_t *a = za;

    /*
     * Note that the rows of the ZAV.D tile are 8 absolute rows apart,
     * so while the address arithmetic below looks funny, it is right.
     */
    for (i = 0; i < oprsz; i++) {
        if (pg[H1_2(i)] & 1) {
            a[i * sizeof(ARMVectorReg)] = n[i];
        }
    }
}

void HELPER(sme_mova_zav_d)(void *vd, void *za, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 8;
    uint8_t *pg = vg;
    uint64_t *d = vd;
    uint64_t *a = za;

    for (i = 0; i < oprsz; i++) {
        if (pg[H1_2(i)] & 1) {
            d[i] = a[i * sizeof(ARMVectorReg)];
        }
    }
}

void HELPER(sme_mova_avz_q)(void *za, void *vn, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 16;
    uint16_t *pg = vg;
    Int128 *n = vn;
    Int128 *a = za;

    /*
     * Note that the rows of the ZAV.Q tile are 16 absolute rows apart,
     * so while the address arithmetic below looks funny, it is right.
     */
    for (i = 0; i < oprsz; i++) {
        if (pg[H2(i)] & 1) {
            a[i * sizeof(ARMVectorReg)] = n[i];
        }
    }
}

void HELPER(sme_mova_zav_q)(void *za, void *vn, void *vg, uint32_t desc)
{
    int i, oprsz = simd_oprsz(desc) / 16;
    uint16_t *pg = vg;
    Int128 *n = vn;
    Int128 *a = za;

    for (i = 0; i < oprsz; i++, za += sizeof(ARMVectorReg)) {
        if (pg[H2(i)] & 1) {
            n[i] = a[i * sizeof(ARMVectorReg)];
        }
    }
}
