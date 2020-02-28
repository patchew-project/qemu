/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_MMVEC_MACROS_H
#define HEXAGON_MMVEC_MACROS_H

#include "mmvec/system_ext_mmvec.h"

#ifdef QEMU_GENERATE
#else
#define VdV      (*(mmvector_t *)(VdV_void))
#define VsV      (*(mmvector_t *)(VsV_void))
#define VuV      (*(mmvector_t *)(VuV_void))
#define VvV      (*(mmvector_t *)(VvV_void))
#define VwV      (*(mmvector_t *)(VwV_void))
#define VxV      (*(mmvector_t *)(VxV_void))
#define VyV      (*(mmvector_t *)(VyV_void))

#define VddV     (*(mmvector_pair_t *)(VddV_void))
#define VuuV     (*(mmvector_pair_t *)(VuuV_void))
#define VvvV     (*(mmvector_pair_t *)(VvvV_void))
#define VxxV     (*(mmvector_pair_t *)(VxxV_void))

#define QeV      (*(mmqreg_t *)(QeV_void))
#define QdV      (*(mmqreg_t *)(QdV_void))
#define QsV      (*(mmqreg_t *)(QsV_void))
#define QtV      (*(mmqreg_t *)(QtV_void))
#define QuV      (*(mmqreg_t *)(QuV_void))
#define QvV      (*(mmqreg_t *)(QvV_void))
#define QxV      (*(mmqreg_t *)(QxV_void))
#endif

#ifdef QEMU_GENERATE
#define DECL_VREG(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t offset = new_temp_vreg_offset(ctx, 1); \
        tcg_gen_addi_ptr(VAR, cpu_env, offset); \
    } while (0)

/*
 * Certain instructions appear to have readonly operands, but
 * in reality they do not.
 *     vdelta instructions overwrite their VuV operand
 */
static bool readonly_ok(insn_t *insn)
{
    uint32_t opcode = insn->opcode;
    if (opcode == V6_vdelta ||
        opcode == V6_vrdelta) {
        return false;
    }
    return true;
}

#define DECL_VREG_READONLY(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    if (!readonly_ok(insn)) { \
        uint32_t offset = new_temp_vreg_offset(ctx, 1); \
        tcg_gen_addi_ptr(VAR, cpu_env, offset); \
    }

#define DECL_VREG_d(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)
#define DECL_VREG_s(VAR, NUM, X, OFF) \
    DECL_VREG_READONLY(VAR, NUM, X, OFF)
#define DECL_VREG_u(VAR, NUM, X, OFF) \
    DECL_VREG_READONLY(VAR, NUM, X, OFF)
#define DECL_VREG_v(VAR, NUM, X, OFF) \
    DECL_VREG_READONLY(VAR, NUM, X, OFF)
#define DECL_VREG_w(VAR, NUM, X, OFF) \
    DECL_VREG_READONLY(VAR, NUM, X, OFF)
#define DECL_VREG_x(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)
#define DECL_VREG_y(VAR, NUM, X, OFF) \
    DECL_VREG(VAR, NUM, X, OFF)

#define DECL_VREG_PAIR(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t offset = new_temp_vreg_offset(ctx, 2); \
        tcg_gen_addi_ptr(VAR, cpu_env, offset); \
    } while (0)

#define DECL_VREG_dd(VAR, NUM, X, OFF) \
    DECL_VREG_PAIR(VAR, NUM, X, OFF)
#define DECL_VREG_uu(VAR, NUM, X, OFF) \
    DECL_VREG_PAIR(VAR, NUM, X, OFF)
#define DECL_VREG_vv(VAR, NUM, X, OFF) \
    DECL_VREG_PAIR(VAR, NUM, X, OFF)
#define DECL_VREG_xx(VAR, NUM, X, OFF) \
    DECL_VREG_PAIR(VAR, NUM, X, OFF)

#define DECL_QREG(VAR, NUM, X, OFF) \
    TCGv_ptr VAR = tcg_temp_local_new_ptr(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        uint32_t __offset = new_temp_qreg_offset(ctx); \
        tcg_gen_addi_ptr(VAR, cpu_env, __offset); \
    } while (0)

#define DECL_QREG_d(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_e(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_s(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_t(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_u(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_v(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)
#define DECL_QREG_x(VAR, NUM, X, OFF) \
    DECL_QREG(VAR, NUM, X, OFF)

#define FREE_VREG(VAR)          tcg_temp_free_ptr(VAR)
#define FREE_VREG_d(VAR)        FREE_VREG(VAR)
#define FREE_VREG_s(VAR)        FREE_VREG(VAR)
#define FREE_VREG_u(VAR)        FREE_VREG(VAR)
#define FREE_VREG_v(VAR)        FREE_VREG(VAR)
#define FREE_VREG_w(VAR)        FREE_VREG(VAR)
#define FREE_VREG_x(VAR)        FREE_VREG(VAR)
#define FREE_VREG_y(VAR)        FREE_VREG(VAR)

#define FREE_VREG_PAIR(VAR)     tcg_temp_free_ptr(VAR)
#define FREE_VREG_dd(VAR)       FREE_VREG_PAIR(VAR)
#define FREE_VREG_uu(VAR)       FREE_VREG_PAIR(VAR)
#define FREE_VREG_vv(VAR)       FREE_VREG_PAIR(VAR)
#define FREE_VREG_xx(VAR)       FREE_VREG_PAIR(VAR)

#define FREE_QREG(VAR)          tcg_temp_free_ptr(VAR)
#define FREE_QREG_d(VAR)        FREE_QREG(VAR)
#define FREE_QREG_e(VAR)        FREE_QREG(VAR)
#define FREE_QREG_s(VAR)        FREE_QREG(VAR)
#define FREE_QREG_t(VAR)        FREE_QREG(VAR)
#define FREE_QREG_u(VAR)        FREE_QREG(VAR)
#define FREE_QREG_v(VAR)        FREE_QREG(VAR)
#define FREE_QREG_x(VAR)        FREE_QREG(VAR)

#define READ_VREG(VAR, NUM) \
    gen_read_vreg(VAR, NUM, 0)
#define READ_VREG_READONLY(VAR, NUM) \
    do { \
        if (readonly_ok(insn)) { \
            gen_read_vreg_readonly(VAR, NUM, 0); \
        } else { \
            gen_read_vreg(VAR, NUM, 0); \
        } \
    } while (0)

#define READ_VREG_s(VAR, NUM)    READ_VREG_READONLY(VAR, NUM)
#define READ_VREG_u(VAR, NUM)    READ_VREG_READONLY(VAR, NUM)
#define READ_VREG_v(VAR, NUM)    READ_VREG_READONLY(VAR, NUM)
#define READ_VREG_w(VAR, NUM)    READ_VREG_READONLY(VAR, NUM)
#define READ_VREG_x(VAR, NUM)    READ_VREG(VAR, NUM)
#define READ_VREG_y(VAR, NUM)    READ_VREG(VAR, NUM)

#define READ_VREG_PAIR(VAR, NUM) \
    gen_read_vreg_pair(VAR, NUM, 0)
#define READ_VREG_uu(VAR, NUM)   READ_VREG_PAIR(VAR, NUM)
#define READ_VREG_vv(VAR, NUM)   READ_VREG_PAIR(VAR, NUM)
#define READ_VREG_xx(VAR, NUM)   READ_VREG_PAIR(VAR, NUM)

#define READ_QREG(VAR, NUM) \
    gen_read_qreg(VAR, NUM, 0)
#define READ_QREG_s(VAR, NUM)     READ_QREG(VAR, NUM)
#define READ_QREG_t(VAR, NUM)     READ_QREG(VAR, NUM)
#define READ_QREG_u(VAR, NUM)     READ_QREG(VAR, NUM)
#define READ_QREG_v(VAR, NUM)     READ_QREG(VAR, NUM)
#define READ_QREG_x(VAR, NUM)     READ_QREG(VAR, NUM)

#define DECL_NEW_OREG(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME; \
    int NUM = REGNO(X) + OFF

#define READ_NEW_OREG(tmp, i) (tmp = tcg_const_tl(i))

#define FREE_NEW_OREG(NAME) \
    tcg_temp_free(NAME)

#define LOG_VREG_WRITE(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_vreg_write(VAR, NUM, VNEW, insn->slot); \
        ctx_log_vreg_write(ctx, (NUM), is_predicated); \
    } while (0)

#define LOG_VREG_WRITE_PAIR(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_vreg_write_pair(VAR, NUM, VNEW, insn->slot); \
        ctx_log_vreg_write(ctx, (NUM) ^ 0, is_predicated); \
        ctx_log_vreg_write(ctx, (NUM) ^ 1, is_predicated); \
    } while (0)

#define LOG_QREG_WRITE(NUM, VAR, VNEW) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_qreg_write(VAR, NUM, VNEW, insn->slot); \
        ctx_log_qreg_write(ctx, (NUM), is_predicated); \
    } while (0)
#else
#define NEW_WRITTEN(NUM) ((env->VRegs_select >> (NUM)) & 1)
#define TMP_WRITTEN(NUM) ((env->VRegs_updated_tmp >> (NUM)) & 1)

#define LOG_VREG_WRITE_FUNC(X) \
    _Generic((X), void * : log_vreg_write, mmvector_t : log_mmvector_write)
#define LOG_VREG_WRITE(NUM, VAR, VNEW) \
    LOG_VREG_WRITE_FUNC(VAR)(env, NUM, VAR, VNEW, slot)

#define READ_EXT_VREG(NUM, VAR, VTMP) \
    do { \
        VAR = ((NEW_WRITTEN(NUM)) ? env->future_VRegs[NUM] \
                                  : env->VRegs[NUM]); \
        VAR = ((TMP_WRITTEN(NUM)) ? env->tmp_VRegs[NUM] : VAR); \
        if (VTMP == EXT_TMP) { \
            if (env->VRegs_updated & ((VRegMask)1) << (NUM)) { \
                VAR = env->future_VRegs[NUM]; \
                env->VRegs_updated ^= ((VRegMask)1) << (NUM); \
            } \
        } \
    } while (0)

#define READ_EXT_VREG_PAIR(NUM, VAR, VTMP) \
    do { \
        READ_EXT_VREG((NUM) ^ 0, VAR.v[0], VTMP); \
        READ_EXT_VREG((NUM) ^ 1, VAR.v[1], VTMP) \
    } while (0)
#endif

#define WRITE_EXT_VREG(NUM, VAR, VNEW)   LOG_VREG_WRITE(NUM, VAR, VNEW)
#define WRITE_VREG_d(NUM, VAR, VNEW)     LOG_VREG_WRITE(NUM, VAR, VNEW)
#define WRITE_VREG_x(NUM, VAR, VNEW)     LOG_VREG_WRITE(NUM, VAR, VNEW)
#define WRITE_VREG_y(NUM, VAR, VNEW)     LOG_VREG_WRITE(NUM, VAR, VNEW)

#define WRITE_VREG_dd(NUM, VAR, VNEW)    LOG_VREG_WRITE_PAIR(NUM, VAR, VNEW)
#define WRITE_VREG_xx(NUM, VAR, VNEW)    LOG_VREG_WRITE_PAIR(NUM, VAR, VNEW)
#define WRITE_VREG_yy(NUM, VAR, VNEW)    LOG_VREG_WRITE_PAIR(NUM, VAR, VNEW)

#define WRITE_QREG_d(NUM, VAR, VNEW)     LOG_QREG_WRITE(NUM, VAR, VNEW)
#define WRITE_QREG_e(NUM, VAR, VNEW)     LOG_QREG_WRITE(NUM, VAR, VNEW)
#define WRITE_QREG_x(NUM, VAR, VNEW)     LOG_QREG_WRITE(NUM, VAR, VNEW)

#endif
