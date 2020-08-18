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

#ifndef HEXAGON_MACROS_H
#define HEXAGON_MACROS_H

#include "qemu.h"
#include "cpu.h"
#include "hex_regs.h"
#include "reg_fields.h"

#ifdef QEMU_GENERATE
#define DECL_REG(NAME, NUM, X, OFF) \
    TCGv NAME = tcg_temp_local_new(); \
    int NUM = REGNO(X) + OFF

#define DECL_REG_WRITABLE(NAME, NUM, X, OFF) \
    TCGv NAME = tcg_temp_local_new(); \
    int NUM = REGNO(X) + OFF; \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        if (is_predicated && !is_preloaded(ctx, NUM)) { \
            tcg_gen_mov_tl(hex_new_value[NUM], hex_gpr[NUM]); \
        } \
    } while (0)
/*
 * For read-only temps, avoid allocating and freeing
 */
#define DECL_REG_READONLY(NAME, NUM, X, OFF) \
    TCGv NAME; \
    int NUM = REGNO(X) + OFF

#define DECL_RREG_d(NAME, NUM, X, OFF) \
    DECL_REG_WRITABLE(NAME, NUM, X, OFF)
#define DECL_RREG_e(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_RREG_s(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_RREG_t(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_RREG_u(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_RREG_v(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_RREG_x(NAME, NUM, X, OFF) \
    DECL_REG_WRITABLE(NAME, NUM, X, OFF)
#define DECL_RREG_y(NAME, NUM, X, OFF) \
    DECL_REG_WRITABLE(NAME, NUM, X, OFF)

#define DECL_PREG_d(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_PREG_e(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_PREG_s(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_PREG_t(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_PREG_u(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_PREG_v(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_PREG_x(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_PREG_y(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)

#define DECL_CREG_d(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_CREG_s(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)

#define DECL_MREG_u(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)

#define DECL_NEW_NREG_s(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_NEW_NREG_t(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)

#define DECL_NEW_PREG_t(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_NEW_PREG_u(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_NEW_PREG_v(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)

#define DECL_NEW_OREG_s(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)

#define DECL_PAIR(NAME, NUM, X, OFF) \
    TCGv_i64 NAME = tcg_temp_local_new_i64(); \
    size1u_t NUM = REGNO(X) + OFF

#define DECL_PAIR_WRITABLE(NAME, NUM, X, OFF) \
    TCGv_i64 NAME = tcg_temp_local_new_i64(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        if (is_predicated) { \
            if (!is_preloaded(ctx, NUM)) { \
                tcg_gen_mov_tl(hex_new_value[NUM], hex_gpr[NUM]); \
            } \
            if (!is_preloaded(ctx, NUM + 1)) { \
                tcg_gen_mov_tl(hex_new_value[NUM + 1], hex_gpr[NUM + 1]); \
            } \
        } \
    } while (0)

#define DECL_RREG_dd(NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(NAME, NUM, X, OFF)
#define DECL_RREG_ss(NAME, NUM, X, OFF) \
    DECL_PAIR(NAME, NUM, X, OFF)
#define DECL_RREG_tt(NAME, NUM, X, OFF) \
    DECL_PAIR(NAME, NUM, X, OFF)
#define DECL_RREG_xx(NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(NAME, NUM, X, OFF)
#define DECL_RREG_yy(NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(NAME, NUM, X, OFF)

#define DECL_CREG_dd(NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(NAME, NUM, X, OFF)
#define DECL_CREG_ss(NAME, NUM, X, OFF) \
    DECL_PAIR(NAME, NUM, X, OFF)

#define DECL_IMM(NAME, X) \
    int NAME = IMMNO(X); \
    do { \
        NAME = NAME; \
    } while (0)
#define DECL_TCG_IMM(TCG_NAME, VAL) \
    TCGv TCG_NAME = tcg_const_tl(VAL)

#define DECL_EA \
    TCGv EA; \
    do { \
        if (GET_ATTRIB(insn->opcode, A_CONDEXEC)) { \
            EA = tcg_temp_local_new(); \
        } else { \
            EA = tcg_temp_new(); \
        } \
    } while (0)

#define LOG_REG_WRITE(RNUM, VAL)\
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_reg_write(RNUM, VAL, insn->slot, is_predicated); \
        ctx_log_reg_write(ctx, (RNUM)); \
    } while (0)

#define LOG_PRED_WRITE(PNUM, VAL) \
    do { \
        gen_log_pred_write(PNUM, VAL); \
        ctx_log_pred_write(ctx, (PNUM)); \
    } while (0)

#define FREE_REG(NAME) \
    tcg_temp_free(NAME)
#define FREE_REG_READONLY(NAME) \
    /* Nothing */

#define FREE_RREG_d(NAME)            FREE_REG(NAME)
#define FREE_RREG_e(NAME)            FREE_REG(NAME)
#define FREE_RREG_s(NAME)            FREE_REG_READONLY(NAME)
#define FREE_RREG_t(NAME)            FREE_REG_READONLY(NAME)
#define FREE_RREG_u(NAME)            FREE_REG_READONLY(NAME)
#define FREE_RREG_v(NAME)            FREE_REG_READONLY(NAME)
#define FREE_RREG_x(NAME)            FREE_REG(NAME)
#define FREE_RREG_y(NAME)            FREE_REG(NAME)

#define FREE_PREG_d(NAME)            FREE_REG(NAME)
#define FREE_PREG_e(NAME)            FREE_REG(NAME)
#define FREE_PREG_s(NAME)            FREE_REG_READONLY(NAME)
#define FREE_PREG_t(NAME)            FREE_REG_READONLY(NAME)
#define FREE_PREG_u(NAME)            FREE_REG_READONLY(NAME)
#define FREE_PREG_v(NAME)            FREE_REG_READONLY(NAME)
#define FREE_PREG_x(NAME)            FREE_REG(NAME)

#define FREE_CREG_d(NAME)            FREE_REG(NAME)
#define FREE_CREG_s(NAME)            FREE_REG_READONLY(NAME)

#define FREE_MREG_u(NAME)            FREE_REG_READONLY(NAME)

#define FREE_NEW_NREG_s(NAME)        FREE_REG(NAME)
#define FREE_NEW_NREG_t(NAME)        FREE_REG(NAME)

#define FREE_NEW_PREG_t(NAME)        FREE_REG_READONLY(NAME)
#define FREE_NEW_PREG_u(NAME)        FREE_REG_READONLY(NAME)
#define FREE_NEW_PREG_v(NAME)        FREE_REG_READONLY(NAME)

#define FREE_NEW_OREG_s(NAME)        FREE_REG(NAME)

#define FREE_REG_PAIR(NAME) \
    tcg_temp_free_i64(NAME)

#define FREE_RREG_dd(NAME)           FREE_REG_PAIR(NAME)
#define FREE_RREG_ss(NAME)           FREE_REG_PAIR(NAME)
#define FREE_RREG_tt(NAME)           FREE_REG_PAIR(NAME)
#define FREE_RREG_xx(NAME)           FREE_REG_PAIR(NAME)
#define FREE_RREG_yy(NAME)           FREE_REG_PAIR(NAME)

#define FREE_CREG_dd(NAME)           FREE_REG_PAIR(NAME)
#define FREE_CREG_ss(NAME)           FREE_REG_PAIR(NAME)

#define FREE_IMM(NAME)               /* nothing */
#define FREE_TCG_IMM(NAME)           tcg_temp_free(NAME)

#define FREE_EA \
    tcg_temp_free(EA)
#else
#define LOG_REG_WRITE(RNUM, VAL)\
    log_reg_write(env, RNUM, VAL, slot)
#define LOG_PRED_WRITE(RNUM, VAL)\
    log_pred_write(env, RNUM, VAL)
#endif

#define SLOT_WRAP(CODE) \
    do { \
        TCGv slot = tcg_const_tl(insn->slot); \
        CODE; \
        tcg_temp_free(slot); \
    } while (0)

#define PART1_WRAP(CODE) \
    do { \
        TCGv part1 = tcg_const_tl(insn->part1); \
        CODE; \
        tcg_temp_free(part1); \
    } while (0)

#define MARK_LATE_PRED_WRITE(RNUM) /* Not modelled in qemu */

#define REGNO(NUM) (insn->regno[NUM])
#define IMMNO(NUM) (insn->immed[NUM])

#ifdef QEMU_GENERATE
#define READ_REG(dest, NUM) \
    gen_read_reg(dest, NUM)
#define READ_REG_READONLY(dest, NUM) \
    do { dest = hex_gpr[NUM]; } while (0)

#define READ_RREG_s(dest, NUM) \
    READ_REG_READONLY(dest, NUM)
#define READ_RREG_t(dest, NUM) \
    READ_REG_READONLY(dest, NUM)
#define READ_RREG_u(dest, NUM) \
    READ_REG_READONLY(dest, NUM)
#define READ_RREG_x(dest, NUM) \
    READ_REG(dest, NUM)
#define READ_RREG_y(dest, NUM) \
    READ_REG(dest, NUM)

#define READ_OREG_s(dest, NUM) \
    READ_REG_READONLY(dest, NUM)

#define READ_CREG_s(dest, NUM) \
    do { \
        if ((NUM) + HEX_REG_SA0 == HEX_REG_P3_0) { \
            gen_read_p3_0(dest); \
        } else { \
            READ_REG_READONLY(dest, ((NUM) + HEX_REG_SA0)); \
        } \
    } while (0)

#define READ_MREG_u(dest, NUM) \
    do { \
        READ_REG_READONLY(dest, ((NUM) + HEX_REG_M0)); \
        dest = dest; \
    } while (0)
#else
#define READ_REG(NUM) \
    (env->gpr[(NUM)])
#endif

#ifdef QEMU_GENERATE
#define READ_REG_PAIR(tmp, NUM) \
    tcg_gen_concat_i32_i64(tmp, hex_gpr[NUM], hex_gpr[(NUM) + 1])
#define READ_RREG_ss(tmp, NUM)          READ_REG_PAIR(tmp, NUM)
#define READ_RREG_tt(tmp, NUM)          READ_REG_PAIR(tmp, NUM)
#define READ_RREG_xx(tmp, NUM)          READ_REG_PAIR(tmp, NUM)
#define READ_RREG_yy(tmp, NUM)          READ_REG_PAIR(tmp, NUM)

#define READ_CREG_PAIR(tmp, i) \
    READ_REG_PAIR(tmp, ((i) + HEX_REG_SA0))
#define READ_CREG_ss(tmp, i)            READ_CREG_PAIR(tmp, i)
#endif

#ifdef QEMU_GENERATE
#define READ_PREG(dest, NUM)             gen_read_preg(dest, (NUM))
#define READ_PREG_READONLY(dest, NUM)    do { dest = hex_pred[NUM]; } while (0)

#define READ_PREG_s(dest, NUM)           READ_PREG_READONLY(dest, NUM)
#define READ_PREG_t(dest, NUM)           READ_PREG_READONLY(dest, NUM)
#define READ_PREG_u(dest, NUM)           READ_PREG_READONLY(dest, NUM)
#define READ_PREG_v(dest, NUM)           READ_PREG_READONLY(dest, NUM)
#define READ_PREG_x(dest, NUM)           READ_PREG(dest, NUM)

#define READ_NEW_PREG(pred, PNUM) \
    do { pred = hex_new_pred_value[PNUM]; } while (0)
#define READ_NEW_PREG_t(pred, PNUM)      READ_NEW_PREG(pred, PNUM)
#define READ_NEW_PREG_u(pred, PNUM)      READ_NEW_PREG(pred, PNUM)
#define READ_NEW_PREG_v(pred, PNUM)      READ_NEW_PREG(pred, PNUM)

#define READ_NEW_REG(tmp, i) \
    do { tmp = tcg_const_tl(i); } while (0)
#define READ_NEW_NREG_s(tmp, i)          READ_NEW_REG(tmp, i)
#define READ_NEW_NREG_t(tmp, i)          READ_NEW_REG(tmp, i)
#define READ_NEW_OREG_s(tmp, i)          READ_NEW_REG(tmp, i)
#else
#define READ_PREG(NUM)                (env->pred[NUM])
#endif


#define WRITE_RREG(NUM, VAL)             LOG_REG_WRITE(NUM, VAL)
#define WRITE_RREG_d(NUM, VAL)           LOG_REG_WRITE(NUM, VAL)
#define WRITE_RREG_e(NUM, VAL)           LOG_REG_WRITE(NUM, VAL)
#define WRITE_RREG_x(NUM, VAL)           LOG_REG_WRITE(NUM, VAL)
#define WRITE_RREG_y(NUM, VAL)           LOG_REG_WRITE(NUM, VAL)

#define WRITE_PREG(NUM, VAL)             LOG_PRED_WRITE(NUM, VAL)
#define WRITE_PREG_d(NUM, VAL)           LOG_PRED_WRITE(NUM, VAL)
#define WRITE_PREG_e(NUM, VAL)           LOG_PRED_WRITE(NUM, VAL)
#define WRITE_PREG_x(NUM, VAL)           LOG_PRED_WRITE(NUM, VAL)

#ifdef QEMU_GENERATE
#define WRITE_CREG(i, tmp) \
    do { \
        if (i + HEX_REG_SA0 == HEX_REG_P3_0) { \
            gen_write_p3_0(tmp); \
        } else { \
            WRITE_RREG((i) + HEX_REG_SA0, tmp); \
        } \
    } while (0)
#define WRITE_CREG_d(NUM, VAL)           WRITE_CREG(NUM, VAL)

#define WRITE_CREG_PAIR(i, tmp)          WRITE_REG_PAIR((i) + HEX_REG_SA0, tmp)
#define WRITE_CREG_dd(NUM, VAL)          WRITE_CREG_PAIR(NUM, VAL)

#define WRITE_REG_PAIR(NUM, VAL) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_reg_write_pair(NUM, VAL, insn->slot, is_predicated); \
        ctx_log_reg_write(ctx, (NUM)); \
        ctx_log_reg_write(ctx, (NUM) + 1); \
    } while (0)

#define WRITE_RREG_dd(NUM, VAL)          WRITE_REG_PAIR(NUM, VAL)
#define WRITE_RREG_xx(NUM, VAL)          WRITE_REG_PAIR(NUM, VAL)
#define WRITE_RREG_yy(NUM, VAL)          WRITE_REG_PAIR(NUM, VAL)
#endif

