/*
 * Copyright(c) 2019-2021 rev.ng Srls. All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PARSER_HELPERS_H
#define PARSER_HELPERS_H

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qemu/compiler.h"
#include "tcg/tcg-cond.h"

#include "idef-parser.tab.h"
#include "idef-parser.yy.h"
#include "idef-parser.h"

/* Decomment this to disable yyasserts */
/* #define NDEBUG */

#define ERR_LINE_CONTEXT 40

#define START_COMMENT "/" "*"
#define END_COMMENT "*" "/"

void yyerror(YYLTYPE *locp,
             yyscan_t scanner __attribute__((unused)),
             Context *c,
             const char *s);

#ifndef NDEBUG
#define yyassert(context, locp, condition, msg) \
    if (!(condition)) { \
        yyerror(locp, (context)->scanner, (context), (msg)); \
    }
#endif

bool is_direct_predicate(HexValue *value);

/* Print functions */
void str_print(Context *c, YYLTYPE *locp, const char *string);

void uint64_print(Context *c, YYLTYPE *locp, uint64_t *num);

void int_print(Context *c, YYLTYPE *locp, int *num);

void uint_print(Context *c, YYLTYPE *locp, unsigned *num);

void tmp_print(Context *c, YYLTYPE *locp, HexTmp *tmp);

void pre_print(Context *c, YYLTYPE *locp, HexPre *pre, bool is_dotnew);

void reg_compose(Context *c, YYLTYPE *locp, HexReg *reg, char reg_id[5]);

void reg_print(Context *c, YYLTYPE *locp, HexReg *reg);

void imm_print(Context *c, YYLTYPE *locp, HexImm *imm);

void var_print(Context *c, YYLTYPE *locp, HexVar *var);

void rvalue_out(Context *c, YYLTYPE *locp, void *pointer);

void out_assert(Context *c, YYLTYPE *locp, void *dummy);

/* Copy output code buffer into stdout */
void commit(Context *c);

#define OUT_IMPL(c, locp, x)                    \
    QEMU_GENERIC(typeof(*x),                \
        (char,      str_print),             \
        (uint64_t,  uint64_print),          \
        (int,       int_print),             \
        (unsigned,  uint_print),            \
        (HexValue,  rvalue_out),            \
        out_assert                          \
    )(c, locp, x);                          \

/* Make a FOREACH macro */
#define FE_1(c, locp, WHAT, X) WHAT(c, locp, X)
#define FE_2(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_1(c, locp, WHAT, __VA_ARGS__)
#define FE_3(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_2(c, locp, WHAT, __VA_ARGS__)
#define FE_4(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_3(c, locp, WHAT, __VA_ARGS__)
#define FE_5(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_4(c, locp, WHAT, __VA_ARGS__)
#define FE_6(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_5(c, locp, WHAT, __VA_ARGS__)
#define FE_7(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_6(c, locp, WHAT, __VA_ARGS__)
#define FE_8(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_7(c, locp, WHAT, __VA_ARGS__)
#define FE_9(c, locp, WHAT, X, ...) \
    WHAT(c, locp, X)FE_8(c, locp, WHAT, __VA_ARGS__)
/* repeat as needed */

#define GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, _9, NAME, ...) NAME

#define FOR_EACH(c, locp, action, ...)          \
  do {                                          \
    GET_MACRO(__VA_ARGS__,                      \
              FE_9,                             \
              FE_8,                             \
              FE_7,                             \
              FE_6,                             \
              FE_5,                             \
              FE_4,                             \
              FE_3,                             \
              FE_2,                             \
              FE_1)(c, locp, action,            \
                    __VA_ARGS__)                \
  } while (0)

#define OUT(c, locp, ...) FOR_EACH((c), (locp), OUT_IMPL, __VA_ARGS__)

const char *cmp_swap(Context *c, YYLTYPE *locp, const char *type);

/* Temporary values creation */
HexValue gen_tmp(Context *c, YYLTYPE *locp, int bit_width);

HexValue gen_tmp_value(Context *c,
                          YYLTYPE *locp,
                          const char *value,
                          int bit_width);

HexValue gen_imm_value(Context *c __attribute__((unused)),
                          YYLTYPE *locp,
                          int value,
                          int bit_width);

void rvalue_free(Context *c, YYLTYPE *locp, HexValue *rvalue);

HexValue rvalue_materialize(Context *c, YYLTYPE *locp, HexValue *rvalue);

HexValue rvalue_extend(Context *c, YYLTYPE *locp, HexValue *rvalue);

HexValue rvalue_truncate(Context *c, YYLTYPE *locp, HexValue *rvalue);

int find_variable(Context *c, YYLTYPE *locp, HexValue *varid);

void varid_allocate(Context *c,
                    YYLTYPE *locp,
                    HexValue *varid,
                    int width,
                    bool is_unsigned);

void ea_free(Context *c, YYLTYPE *locp);

HexValue gen_bin_cmp(Context *c,
                     YYLTYPE *locp,
                     TCGCond type,
                     HexValue *op1_ptr,
                     HexValue *op2_ptr);

/* Code generation functions */
HexValue gen_bin_op(Context *c,
                       YYLTYPE *locp,
                       OpType type,
                       HexValue *operand1,
                       HexValue *operand2);

HexValue gen_cast_op(Context *c,
                        YYLTYPE *locp,
                        HexValue *source,
                        unsigned target_width);

HexValue gen_extend_op(Context *c,
                          YYLTYPE *locp,
                          HexValue *src_width_ptr,
                          HexValue *dst_width_ptr,
                          HexValue *value_ptr,
                          bool is_unsigned);

void gen_rdeposit_op(Context *c,
                     YYLTYPE *locp,
                     HexValue *dest,
                     HexValue *value,
                     HexValue *begin,
                     HexValue *width);

void gen_deposit_op(Context *c,
                           YYLTYPE *locp,
                           HexValue *dest,
                           HexValue *value,
                           HexValue *index,
                           HexCast *cast);

HexValue gen_rextract_op(Context *c,
                         YYLTYPE *locp,
                         HexValue *source,
                         int begin,
                         int width);

HexValue gen_extract_op(Context *c,
                           YYLTYPE *locp,
                           HexValue *source,
                           HexValue *index,
                           HexExtract *extract);

HexValue gen_read_creg(Context *c, YYLTYPE *locp, HexValue *reg);

void gen_write_creg(Context *c,
                           YYLTYPE *locp,
                           HexValue *reg,
                           HexValue *value);

void gen_assign(Context *c,
                YYLTYPE *locp,
                HexValue *dest,
                HexValue *value);

HexValue gen_convround(Context *c,
                          YYLTYPE *locp,
                          HexValue *source);

HexValue gen_round(Context *c,
                   YYLTYPE *locp,
                   HexValue *source,
                   HexValue *position);

HexValue gen_convround_n(Context *c,
                         YYLTYPE *locp,
                         HexValue *source_ptr,
                         HexValue *bit_pos_ptr);

/* Circular addressing mode with auto-increment */
void gen_circ_op(Context *c,
                 YYLTYPE *locp,
                 HexValue *addr,
                 HexValue *increment,
                 HexValue *modifier);

HexValue gen_locnt_op(Context *c, YYLTYPE *locp, HexValue *source);

HexValue gen_ctpop_op(Context *c, YYLTYPE *locp, HexValue *source);

HexValue gen_fbrev_4(Context *c, YYLTYPE *locp, HexValue *source);

HexValue gen_fbrev_8(Context *c, YYLTYPE *locp, HexValue *source);

HexValue gen_rotl(Context *c, YYLTYPE *locp, HexValue *source, HexValue *n);

HexValue gen_deinterleave(Context *c, YYLTYPE *locp, HexValue *mixed);

HexValue gen_interleave(Context *c,
                        YYLTYPE *locp,
                        HexValue *odd,
                        HexValue *even);

HexValue gen_carry_from_add(Context *c,
                            YYLTYPE *locp,
                            HexValue *op1,
                            HexValue *op2,
                            HexValue *op3);

void gen_inst(Context *c, GString *iname);

void gen_inst_args(Context *c, YYLTYPE *locp);

void gen_inst_code(Context *c, YYLTYPE *locp);

void gen_pre_assign(Context *c, YYLTYPE *locp, HexValue *lp, HexValue *rp);

void gen_load(Context *c, YYLTYPE *locp, HexValue *num, HexValue *size,
              bool is_unsigned, HexValue *ea, HexValue *dst);

void gen_store(Context *c, YYLTYPE *locp, HexValue *num, HexValue *size,
               HexValue *ea, HexValue *src);

void gen_sethalf(Context *c, YYLTYPE *locp, HexCast *sh, HexValue *n,
                 HexValue *dst, HexValue *val);

void gen_setbits(Context *c, YYLTYPE *locp, HexValue *hi, HexValue *lo,
                 HexValue *dst, HexValue *val);

int gen_if_cond(Context *c, YYLTYPE *locp, HexValue *cond);

int gen_if_else(Context *c, YYLTYPE *locp, int index);

HexValue gen_rvalue_pre(Context *c, YYLTYPE *locp, HexValue *pre);

HexValue gen_rvalue_var(Context *c, YYLTYPE *locp, HexValue *var);

HexValue gen_rvalue_mpy(Context *c, YYLTYPE *locp, HexMpy *mpy, HexValue *a,
                        HexValue *b);

HexValue gen_rvalue_pow(Context *c, YYLTYPE *locp, HexValue *l, HexValue *r);

HexValue gen_rvalue_not(Context *c, YYLTYPE *locp, HexValue *v);

HexValue gen_rvalue_notl(Context *c, YYLTYPE *locp, HexValue *v);

HexValue gen_rvalue_sat(Context *c, YYLTYPE *locp, HexSat *sat, HexValue *n,
                        HexValue *v);

HexValue gen_rvalue_ternary(Context *c, YYLTYPE *locp, HexValue *cond,
                            HexValue *t, HexValue *e);

HexValue gen_rvalue_fscr(Context *c, YYLTYPE *locp, HexValue *v);

HexValue gen_rvalue_abs(Context *c, YYLTYPE *locp, HexValue *v);

HexValue gen_rvalue_neg(Context *c, YYLTYPE *locp, HexValue *v);

HexValue gen_rvalue_brev(Context *c, YYLTYPE *locp, HexValue *v);

void gen_set_overflow(Context *c, YYLTYPE *locp, HexValue *vp);

HexValue gen_rvalue_ternary(Context *c, YYLTYPE *locp, HexValue *cond,
                            HexValue *t, HexValue *e);

const char *cond_to_str(TCGCond cond);

void emit_header(Context *c);

void emit_arg(Context *c, YYLTYPE *locp, HexValue *arg);

void emit_footer(Context *c);

void track_string(Context *c, GString *s);

void free_variables(Context *c, YYLTYPE *locp);

void free_instruction(Context *c);

#endif /* PARSER_HELPERS_h */
