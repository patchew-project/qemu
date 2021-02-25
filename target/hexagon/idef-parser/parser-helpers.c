/*
 * Copyright(c) 2019-2020 rev.ng Srls. All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; withOUT even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "idef-parser.h"
#include "parser-helpers.h"
#include "idef-parser.tab.h"
#include "idef-parser.yy.h"

const char *COND_EQ = "TCG_COND_EQ";
const char *COND_NE = "TCG_COND_NE";
const char *COND_GT = "TCG_COND_GT";
const char *COND_LT = "TCG_COND_LT";
const char *COND_GE = "TCG_COND_GE";
const char *COND_LE = "TCG_COND_LE";
const char *COND_GTU = "TCG_COND_GTU";
const char *COND_LTU = "TCG_COND_LTU";
const char *COND_GEU = "TCG_COND_GEU";
const char *COND_LEU = "TCG_COND_LEU";

const char *creg_str[] = {"HEX_REG_SP", "HEX_REG_FP", "HEX_REG_LR",
                          "HEX_REG_GP", "HEX_REG_LC0", "HEX_REG_LC1",
                          "HEX_REG_SA0", "HEX_REG_SA1"};

void yyerror(YYLTYPE *locp,
             yyscan_t scanner __attribute__((unused)),
             Context *c,
             const char *s)
{
    const char *code_ptr = c->input_buffer;

    fprintf(stderr, "WARNING (%s): '%s'\n", c->inst.name, s);

    fprintf(stderr, "Problematic range: ");
    for (int i = locp->first_column; i < locp->last_column; i++) {
        if (code_ptr[i] != '\n') {
            fprintf(stderr, "%c", code_ptr[i]);
        }
    }
    fprintf(stderr, "\n");

    for (int i = 0;
         i < 80 &&
         code_ptr[locp->first_column - 10 + i] != '\0' &&
         code_ptr[locp->first_column - 10 + i] != '\n';
         i++) {
        fprintf(stderr, "%c", code_ptr[locp->first_column - 10 + i]);
    }
    fprintf(stderr, "\n");
    for (int i = 0; i < 9; i++) {
        fprintf(stderr, " ");
    }
    fprintf(stderr, "^");
    for (int i = 0; i < (locp->last_column - locp->first_column) - 1; i++) {
        fprintf(stderr, "~");
    }
    fprintf(stderr, "\n");
    c->inst.error_count++;
}

bool is_direct_predicate(HexValue *value)
{
    return value->pre.id >= '0' && value->pre.id <= '3';
}

/* Print functions */
void str_print(Context *c, YYLTYPE *locp, char *string)
{
    EMIT(c, "%s", string);
}


void uint64_print(Context *c, YYLTYPE *locp, uint64_t *num)
{
    EMIT(c, "%" PRIu64, *num);
}

void int_print(Context *c, YYLTYPE *locp, int *num)
{
    EMIT(c, "%d", *num);
}

void uint_print(Context *c, YYLTYPE *locp, unsigned *num)
{
    EMIT(c, "%u", *num);
}

void tmp_print(Context *c, YYLTYPE *locp, HexTmp *tmp)
{
    EMIT(c, "tmp_");
    EMIT(c, "%d", tmp->index);
}

void pre_print(Context *c, YYLTYPE *locp, HexPre *pre, bool is_dotnew)
{
    char suffix = is_dotnew ? 'N' : 'V';
    EMIT(c, "P%c%c", pre->id, suffix);
}

void reg_compose(Context *c, YYLTYPE *locp, HexReg *reg, char reg_id[5])
{
    switch (reg->type) {
    case GENERAL_PURPOSE:
        reg_id[0] = 'R';
        break;
    case CONTROL:
        reg_id[0] = 'C';
        break;
    case MODIFIER:
        reg_id[0] = 'M';
        break;
    case DOTNEW:
        /* The DOTNEW case is managed by the upper level function */
        break;
    }
    switch (reg->bit_width) {
    case 32:
        reg_id[1] = reg->id;
        reg_id[2] = 'V';
        break;
    case 64:
        reg_id[1] = reg->id;
        reg_id[2] = reg->id;
        reg_id[3] = 'V';
        break;
    default:
        yyassert(c, locp, false, "Unhandled register bit width!\n");
    }
}

void reg_print(Context *c, YYLTYPE *locp, HexReg *reg)
{
  if (reg->type == DOTNEW) {
    EMIT(c, "N%cN", reg->id);
  } else {
    char reg_id[5] = { 0 };
    reg_compose(c, locp, reg, reg_id);
    EMIT(c, "%s", reg_id);
  }
}

void imm_print(Context *c, YYLTYPE *locp, HexImm *imm)
{
    switch (imm->type) {
    case I:
        EMIT(c, "i");
        break;
    case VARIABLE:
        EMIT(c, "%ciV", imm->id);
        break;
    case VALUE:
        EMIT(c, "((int64_t)%" PRIu64 "ULL)", (int64_t)imm->value);
        break;
    case QEMU_TMP:
        EMIT(c, "qemu_tmp_%" PRIu64, imm->index);
        break;
    case IMM_PC:
        EMIT(c, "dc->pc");
        break;
    case IMM_CONSTEXT:
        EMIT(c, "insn->extension_valid");
        break;
    default:
        yyassert(c, locp, false, "Cannot print this expression!");
    }
}

void var_print(Context *c, YYLTYPE *locp, HexVar *var)
{
    EMIT(c, "%s", var->name);
}

void rvalue_out(Context *c, YYLTYPE *locp, void *pointer)
{
  HexValue *rvalue = (HexValue *) pointer;
  switch (rvalue->type) {
  case REGISTER:
      reg_print(c, locp, &rvalue->reg);
      break;
  case TEMP:
      tmp_print(c, locp, &rvalue->tmp);
      break;
  case IMMEDIATE:
      imm_print(c, locp, &rvalue->imm);
      break;
  case VARID:
      var_print(c, locp, &rvalue->var);
      break;
  case PREDICATE:
      pre_print(c, locp, &rvalue->pre, rvalue->is_dotnew);
      break;
  default:
      yyassert(c, locp, false, "Cannot print this expression!");
  }
}

/* Copy output code buffer */
void commit(Context *c)
{
    /* Emit instruction pseudocode */
    EMIT_SIG(c, "\n" START_COMMENT " ");
    for (char *x = c->inst.code_begin; x < c->inst.code_end; x++) {
        EMIT_SIG(c, "%c", *x);
    }
    EMIT_SIG(c, " " END_COMMENT "\n");

    /* Commit instruction code to output file */
    fwrite(c->signature_buffer, sizeof(char), c->signature_c, c->output_file);
    fwrite(c->header_buffer, sizeof(char), c->header_c, c->output_file);
    fwrite(c->out_buffer, sizeof(char), c->out_c, c->output_file);

    fwrite(c->signature_buffer, sizeof(char), c->signature_c, c->defines_file);
    fprintf(c->defines_file, ";\n");
}

const char *cmp_swap(Context *c, YYLTYPE *locp, const char *type)
{
    if (type == COND_EQ) {
        return COND_EQ;
    } else if (type == COND_NE) {
        return COND_NE;
    } else if (type == COND_GT) {
        return COND_LT;
    } else if (type == COND_LT) {
        return COND_GT;
    } else if (type == COND_GE) {
        return COND_LE;
    } else if (type == COND_LE) {
        return COND_GE;
    } else if (type == COND_GTU) {
        return COND_LTU;
    } else if (type == COND_LTU) {
        return COND_GTU;
    } else if (type == COND_GEU) {
        return COND_LEU;
    } else if (type == COND_LEU) {
        return COND_GEU;
    } else {
        yyassert(c, locp, false, "Unhandled comparison swap!");
        return NULL;
    }
}

/* Temporary values creation */
static inline HexValue gen_tmp_impl(Context *c,
                                    YYLTYPE *locp,
                                    int bit_width,
                                    bool is_local)
{
    HexValue rvalue;
    rvalue.type = TEMP;
    bit_width = (bit_width == 64) ? 64 : 32;
    rvalue.bit_width = bit_width;
    rvalue.is_unsigned = false;
    rvalue.is_dotnew = false;
    rvalue.is_manual = false;
    rvalue.tmp.index = c->inst.tmp_count;
    const char *suffix = is_local ? "local_" : "";
    OUT(c, locp, "TCGv_i", &bit_width, " tmp_", &c->inst.tmp_count,
        " = tcg_temp_", suffix, "new_i", &bit_width, "();\n");
    c->inst.tmp_count++;
    return rvalue;
}

HexValue gen_tmp(Context *c, YYLTYPE *locp, int bit_width)
{
    return gen_tmp_impl(c, locp, bit_width, false);
}

HexValue gen_local_tmp(Context *c, YYLTYPE *locp, int bit_width)
{
    return gen_tmp_impl(c, locp, bit_width, true);
}

HexValue gen_tmp_value(Context *c,
                       YYLTYPE *locp,
                       const char *value,
                       int bit_width)
{
    HexValue rvalue;
    rvalue.type = TEMP;
    rvalue.bit_width = bit_width;
    rvalue.is_unsigned = false;
    rvalue.is_dotnew = false;
    rvalue.is_manual = false;
    rvalue.tmp.index = c->inst.tmp_count;
    OUT(c, locp, "TCGv_i", &bit_width, " tmp_", &c->inst.tmp_count,
        " = tcg_const_i", &bit_width, "(", value, ");\n");
    c->inst.tmp_count++;
    return rvalue;
}

HexValue gen_imm_value(Context *c __attribute__((unused)),
                       YYLTYPE *locp,
                       int value,
                       int bit_width)
{
    HexValue rvalue;
    rvalue.type = IMMEDIATE;
    rvalue.bit_width = bit_width;
    rvalue.is_unsigned = false;
    rvalue.is_dotnew = false;
    rvalue.is_manual = false;
    rvalue.imm.type = VALUE;
    rvalue.imm.value = value;
    return rvalue;
}

void rvalue_free(Context *c, YYLTYPE *locp, HexValue *rvalue)
{
    if (rvalue->type == TEMP && !rvalue->is_manual) {
        const char *bit_suffix = (rvalue->bit_width == 64) ? "i64" : "i32";
        OUT(c, locp, "tcg_temp_free_", bit_suffix, "(", rvalue, ");\n");
    }
}

static void rvalue_free_manual(Context *c, YYLTYPE *locp, HexValue *rvalue)
{
    rvalue->is_manual = false;
    rvalue_free(c, locp, rvalue);
}

static void rvalue_free_ext(Context *c, YYLTYPE *locp, HexValue *rvalue,
                            bool free_manual) {
    if (free_manual) {
        rvalue_free_manual(c, locp, rvalue);
    } else {
        rvalue_free(c, locp, rvalue);
    }
}

HexValue rvalue_materialize(Context *c, YYLTYPE *locp, HexValue *rvalue)
{
    if (rvalue->type == IMMEDIATE) {
        HexValue tmp = gen_tmp(c, locp, rvalue->bit_width);
        tmp.is_unsigned = rvalue->is_unsigned;
        const char *bit_suffix = (rvalue->bit_width == 64) ? "i64" : "i32";
        OUT(c, locp, "tcg_gen_movi_", bit_suffix,
            "(", &tmp, ", ", rvalue, ");\n");
        rvalue_free(c, locp, rvalue);
        return tmp;
    }
    return *rvalue;
}

HexValue rvalue_extend(Context *c, YYLTYPE *locp, HexValue *rvalue)
{
    if (rvalue->type == IMMEDIATE) {
        HexValue res = *rvalue;
        res.bit_width = 64;
        return res;
    } else {
        if (rvalue->bit_width == 32) {
            HexValue res = gen_tmp(c, locp, 64);
            const char *sign_suffix = (rvalue->is_unsigned) ? "u" : "";
            OUT(c, locp, "tcg_gen_ext", sign_suffix,
                "_i32_i64(", &res, ", ", rvalue, ");\n");
            rvalue_free(c, locp, rvalue);
            return res;
        }
    }
    return *rvalue;
}

HexValue rvalue_truncate(Context *c, YYLTYPE *locp, HexValue *rvalue)
{
    if (rvalue->type == IMMEDIATE) {
        HexValue res = *rvalue;
        res.bit_width = 32;
        return res;
    } else {
        if (rvalue->bit_width == 64) {
            HexValue res = gen_tmp(c, locp, 32);
            OUT(c, locp, "tcg_gen_trunc_i64_tl(", &res, ", ", rvalue, ");\n");
            rvalue_free(c, locp, rvalue);
            return res;
        }
    }
    return *rvalue;
}

int find_variable(Context *c, YYLTYPE *locp, HexValue *varid)
{
    for (int i = 0; i < c->inst.allocated_count; i++) {
        if (!strcmp(varid->var.name, c->inst.allocated[i].name)) {
            return i;
        }
    }
    return -1;
}

void varid_allocate(Context *c,
                    YYLTYPE *locp,
                    HexValue *varid,
                    int width,
                    bool is_unsigned)
{
    varid->bit_width = width;
    const char *bit_suffix = width == 64 ? "64" : "32";
    yyassert(c, locp, c->inst.allocated_count < ALLOC_LIST_LEN,
             "Too many automatic variables required!");
    int index = find_variable(c, locp, varid);
    bool found = index != -1;
    if (found) {
        free((char *) varid->var.name);
        varid->var.name = c->inst.allocated[index].name;
        varid->bit_width = c->inst.allocated[index].bit_width;
        varid->is_unsigned = c->inst.allocated[index].is_unsigned;
    } else {
        EMIT_HEAD(c, "TCGv_i%s %s", bit_suffix, varid->var.name);
        EMIT_HEAD(c, " = tcg_temp_local_new_i%s();\n", bit_suffix);
        c->inst.allocated[c->inst.allocated_count].name = varid->var.name;
        c->inst.allocated[c->inst.allocated_count].bit_width = width;
        c->inst.allocated[c->inst.allocated_count].is_unsigned = is_unsigned;
        c->inst.allocated_count++;
    }
}

void ea_free(Context *c, YYLTYPE *locp)
{
    OUT(c, locp, "tcg_temp_free(EA);\n");
}

enum OpTypes {
    IMM_IMM = 0,
    IMM_REG = 1,
    REG_IMM = 2,
    REG_REG = 3,
};

HexValue gen_bin_cmp(Context *c,
                     YYLTYPE *locp,
                     const char *type,
                     HexValue *op1_ptr,
                     HexValue *op2_ptr)
{
    HexValue op1 = *op1_ptr;
    HexValue op2 = *op2_ptr;
    enum OpTypes op_types = (op1.type != IMMEDIATE) << 1
                            | (op2.type != IMMEDIATE);

    /* Find bit width of the two operands, if at least one is 64 bit use a */
    /* 64bit operation, eventually extend 32bit operands. */
    bool op_is64bit = op1.bit_width == 64 || op2.bit_width == 64;
    const char *bit_suffix = op_is64bit ? "i64" : "i32";
    int bit_width = (op_is64bit) ? 64 : 32;
    if (op_is64bit) {
        switch (op_types) {
        case IMM_IMM:
            break;
        case IMM_REG:
            op2 = rvalue_extend(c, locp, &op2);
            break;
        case REG_IMM:
            op1 = rvalue_extend(c, locp, &op1);
            break;
        case REG_REG:
            op1 = rvalue_extend(c, locp, &op1);
            op2 = rvalue_extend(c, locp, &op2);
            break;
        }
    }

    HexValue res = gen_tmp(c, locp, bit_width);

    switch (op_types) {
    case IMM_IMM:
    {
        OUT(c, locp, "tcg_gen_movi_", bit_suffix,
            "(", &res, ", ", &op1, " == ", &op2, ");\n");
        break;
    }
    case IMM_REG:
    {
        HexValue swp = op2;
        op2 = op1;
        op1 = swp;
        /* Swap comparison direction */
        type = cmp_swap(c, locp, type);
    }
    /* fallthrough */
    case REG_IMM:
    {
        OUT(c, locp, "tcg_gen_setcondi_", bit_suffix, "(");
        OUT(c, locp, type, ", ", &res, ", ", &op1, ", ", &op2, ");\n");
        break;
    }
    case REG_REG:
    {
        OUT(c, locp, "tcg_gen_setcond_", bit_suffix, "(");
        OUT(c, locp, type, ", ", &res, ", ", &op1, ", ", &op2, ");\n");
        break;
    }
    default:
    {
        fprintf(stderr, "Error in evalutating immediateness!");
        abort();
    }
    }

    /* Free operands */
    rvalue_free(c, locp, &op1);
    rvalue_free(c, locp, &op2);

    return res;
}

static void gen_add_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       const char *bit_suffix, HexValue *res,
                       enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ", res,
            " = ", op1, " + ", op2, ";\n");
        break;
    case IMM_REG:
        OUT(c, locp, "tcg_gen_addi_", bit_suffix,
            "(", res, ", ", op2, ", ", op1, ");\n");
        break;
    case REG_IMM:
        OUT(c, locp, "tcg_gen_addi_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    case REG_REG:
        OUT(c, locp, "tcg_gen_add_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, op1);
    rvalue_free(c, locp, op2);
}

static void gen_sub_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       const char *bit_suffix, HexValue *res,
                       enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ", res,
            " = ", op1, " - ", op2, ";\n");
        break;
    case IMM_REG:
        OUT(c, locp, "tcg_gen_subfi_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    case REG_IMM:
        OUT(c, locp, "tcg_gen_subi_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    case REG_REG:
        OUT(c, locp, "tcg_gen_sub_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, op1);
    rvalue_free(c, locp, op2);
}

static void gen_mul_op(Context *c, YYLTYPE *locp,
                       const char *bit_suffix, HexValue *res,
                       enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int64_t ", res, " = ", op1, " * ", op2, ";\n");
        break;
    case IMM_REG:
        OUT(c, locp, "tcg_gen_muli_", bit_suffix,
            "(", res, ", ", op2, ", (int64_t)", op1, ");\n");
        break;
    case REG_IMM:
        OUT(c, locp, "tcg_gen_muli_", bit_suffix,
            "(", res, ", ", op1, ", (int64_t)", op2, ");\n");
        break;
    case REG_REG:
        OUT(c, locp, "tcg_gen_mul_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, op1);
    rvalue_free(c, locp, op2);
}

static void gen_div_op(Context *c, YYLTYPE *locp, HexValue *res,
                       enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int64_t ", res, " = ", op1, " / ", op2, ";\n");
        break;
    case IMM_REG:
    case REG_IMM:
    case REG_REG:
        OUT(c, locp, res, " = gen_helper_divu("
            "cpu_env, ", op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, op1);
    rvalue_free(c, locp, op2);
}

static void gen_asl_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       bool op_is64bit, const char *bit_suffix, HexValue *res,
                       enum OpTypes op_types, HexValue *op1_ptr,
                       HexValue *op2_ptr)
{
    HexValue op1 = *op1_ptr;
    HexValue op2 = *op2_ptr;
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ", res,
            " = ", &op1, " << ", &op2, ";\n");
        break;
    case REG_IMM:
        {
            /* Need to work around assert(op2 < 64) in tcg_gen_shli */
            if (op_is64bit) {
                op2 = rvalue_extend(c, locp, &op2);
            }
            op2 = rvalue_materialize(c, locp, &op2);
            const char *mask = op_is64bit ? "0xffffffffffffffc0"
                                          : "0xffffffc0";
            HexValue zero = gen_tmp_value(c, locp, "0", bit_width);
            HexValue tmp = gen_tmp(c, locp, bit_width);
            OUT(c, locp, "tcg_gen_andi_", bit_suffix,
                "(", &tmp, ", ", &op2, ", ", mask, ");\n");
            OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
            OUT(c, locp, "(TCG_COND_EQ, ", &tmp, ", ", &tmp, ", ", &zero);
            OUT(c, locp, ", ", &op2, ", ", &zero, ");\n");
            OUT(c, locp, "tcg_gen_shl_", bit_suffix,
                "(", res, ", ", &op1, ", ", &tmp, ");\n");
            rvalue_free(c, locp, &zero);
            rvalue_free(c, locp, &tmp);
        }
        break;
    case IMM_REG:
        op1.bit_width = bit_width;
        op1 = rvalue_materialize(c, locp, &op1);
        /* Fallthrough */
    case REG_REG:
        OUT(c, locp, "tcg_gen_shl_", bit_suffix,
            "(", res, ", ", &op1, ", ", &op2, ");\n");
        break;
    }
    if (op_types != IMM_IMM) {
        /* Handle left shift by 64 which hexagon-sim expects to clear out */
        /* register */
        HexValue edge = gen_tmp_value(c, locp, "64", bit_width);
        HexValue zero = gen_tmp_value(c, locp, "0", bit_width);
        if (op_is64bit) {
            op2 = rvalue_extend(c, locp, &op2);
        }
        op1 = rvalue_materialize(c, locp, &op1);
        op2 = rvalue_materialize(c, locp, &op2);
        OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
        if (op_types == REG_REG || op_types == IMM_REG) {
            OUT(c, locp, "(TCG_COND_EQ, ", res, ", ", &op2, ", ", &edge);
        } else {
            OUT(c, locp, "(TCG_COND_EQ, ", res, ", ", &op2, ", ", &edge);
        }
        OUT(c, locp, ", ", &zero, ", ", res, ");\n");
        rvalue_free(c, locp, &edge);
        rvalue_free(c, locp, &zero);
    }
    rvalue_free(c, locp, &op1);
    rvalue_free(c, locp, &op2);
}

static void gen_asr_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       const char *bit_suffix, HexValue *res,
                       enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ", res,
            " = ", op1, " >> ", op2, ";\n");
        break;
    case REG_IMM:
        OUT(c, locp, "tcg_gen_sari_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    case IMM_REG:
        rvalue_materialize(c, locp, op1);
        /* Fallthrough */
    case REG_REG:
        OUT(c, locp, "tcg_gen_sar_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, op1);
    rvalue_free(c, locp, op2);
}

static void gen_lsr_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       const char *bit_suffix, HexValue *res,
                       enum OpTypes op_types, HexValue *op1_ptr, HexValue *op2)
{
    HexValue op1 = *op1_ptr;
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ",
            res, " = ", &op1, " >> ", op2, ";\n");
        break;
    case REG_IMM:
        OUT(c, locp, "tcg_gen_shri_", bit_suffix,
            "(", res, ", ", &op1, ", ", op2, ");\n");
        break;
    case IMM_REG:
        op1 = rvalue_materialize(c, locp, &op1);
        /* Fallthrough */
    case REG_REG:
        OUT(c, locp, "tcg_gen_shr_", bit_suffix,
            "(", res, ", ", &op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, &op1);
    rvalue_free(c, locp, op2);
}

static void gen_andb_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                        const char *bit_suffix, HexValue *res,
                        enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ",
            res, " = ", op1, " & ", op2, ";\n");
        break;
    case IMM_REG:
        OUT(c, locp, "tcg_gen_andi_", bit_suffix,
            "(", res, ", ", op2, ", ", op1, ");\n");
        break;
    case REG_IMM:
        OUT(c, locp, "tcg_gen_andi_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    case REG_REG:
        OUT(c, locp, "tcg_gen_and_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, op1);
    rvalue_free(c, locp, op2);
}

static void gen_orb_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                       const char *bit_suffix, HexValue *res,
                       enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ",
            res, " = ", op1, " & ", op2, ";\n");
        break;
    case IMM_REG:
        OUT(c, locp, "tcg_gen_ori_", bit_suffix,
            "(", res, ", ", op2, ", ", op1, ");\n");
        break;
    case REG_IMM:
        OUT(c, locp, "tcg_gen_ori_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    case REG_REG:
        OUT(c, locp, "tcg_gen_or_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, op1);
    rvalue_free(c, locp, op2);
}

static void gen_xorb_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                        const char *bit_suffix, HexValue *res,
                        enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ",
            res, " = ", op1, " & ", op2, ";\n");
        break;
    case IMM_REG:
        OUT(c, locp, "tcg_gen_xori_", bit_suffix,
            "(", res, ", ", op2, ", ", op1, ");\n");
        break;
    case REG_IMM:
        OUT(c, locp, "tcg_gen_xori_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    case REG_REG:
        OUT(c, locp, "tcg_gen_xor_", bit_suffix,
            "(", res, ", ", op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, op1);
    rvalue_free(c, locp, op2);
}

static void gen_andl_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                        const char *bit_suffix, HexValue *res,
                        enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    HexValue zero, tmp1, tmp2;
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ",
            res, " = ", op1, " && ", op2, ";\n");
        break;
    case IMM_REG:
        zero = gen_tmp_value(c, locp, "0", 32);
        tmp2 = gen_bin_cmp(c, locp, "TCG_COND_NE", op2, &zero);
        OUT(c, locp, "tcg_gen_andi_", bit_suffix,
            "(", res, ", ", op1, " != 0 , ", &tmp2, ");\n");
        rvalue_free(c, locp, &tmp2);
        break;
    case REG_IMM:
        zero = gen_tmp_value(c, locp, "0", 32);
        tmp1 = gen_bin_cmp(c, locp, "TCG_COND_NE", op1, &zero);
        OUT(c, locp, "tcg_gen_andi_", bit_suffix,
            "(", res, ", ", &tmp1, ", ", op2, " != 0);\n");
        rvalue_free(c, locp, &tmp1);
        break;
    case REG_REG:
        zero = gen_tmp_value(c, locp, "0", 32);
        zero.is_manual = true;
        tmp1 = gen_bin_cmp(c, locp, "TCG_COND_NE", op1, &zero);
        tmp2 = gen_bin_cmp(c, locp, "TCG_COND_NE", op2, &zero);
        OUT(c, locp, "tcg_gen_and_", bit_suffix,
            "(", res, ", ", &tmp1, ", ", &tmp2, ");\n");
        rvalue_free_manual(c, locp, &zero);
        rvalue_free(c, locp, &tmp1);
        rvalue_free(c, locp, &tmp2);
        break;
    }
}

static void gen_mini_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                        HexValue *res, enum OpTypes op_types,
                        HexValue *op1_ptr, HexValue *op2_ptr)
{
    HexValue op1 = *op1_ptr;
    HexValue op2 = *op2_ptr;
    const char *comparison = res->is_unsigned
                             ? "TCG_COND_LEU"
                             : "TCG_COND_LE";
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ", res, " = (", &op1, " <= ");
        OUT(c, locp, &op2, ") ? ", &op1, " : ", &op2, ";\n");
        break;
    case IMM_REG:
        op1.bit_width = bit_width;
        op1 = rvalue_materialize(c, locp, &op1);
        OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
        OUT(c, locp, "(", comparison, ", ", res, ", ", &op1, ", ", &op2);
        OUT(c, locp, ", ", &op1, ", ", &op2, ");\n");
        break;
    case REG_IMM:
        op2.bit_width = bit_width;
        op2 = rvalue_materialize(c, locp, &op2);
        /* Fallthrough */
    case REG_REG:
        OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
        OUT(c, locp, "(", comparison, ", ", res, ", ", &op1, ", ", &op2);
        OUT(c, locp, ", ", &op1, ", ", &op2, ");\n");
        break;
    }
    rvalue_free(c, locp, &op1);
    rvalue_free(c, locp, &op2);
}

static void gen_maxi_op(Context *c, YYLTYPE *locp, unsigned bit_width,
                        HexValue *res, enum OpTypes op_types,
                        HexValue *op1_ptr, HexValue *op2_ptr)
{
    HexValue op1 = *op1_ptr;
    HexValue op2 = *op2_ptr;
    const char *comparison = res->is_unsigned
                             ? "TCG_COND_LEU"
                             : "TCG_COND_LE";
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int", &bit_width, "_t ", res, " = (", &op1, " <= ");
        OUT(c, locp, &op2, ") ? ", &op2, " : ", &op1, ";\n");
        break;
    case IMM_REG:
        op1.bit_width = bit_width;
        op1 = rvalue_materialize(c, locp, &op1);
        OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
        OUT(c, locp, "(", comparison, ", ", res, ", ", &op1, ", ", &op2);
        OUT(c, locp, ", ", &op2, ", ", &op1, ");\n");
        break;
    case REG_IMM:
        op2.bit_width = bit_width;
        op2 = rvalue_materialize(c, locp, &op2);
        /* Fallthrough */
    case REG_REG:
        OUT(c, locp, "tcg_gen_movcond_i", &bit_width);
        OUT(c, locp, "(", comparison, ", ", res, ", ", &op1, ", ", &op2);
        OUT(c, locp, ", ", &op2, ", ", &op1, ");\n");
        break;
    }
    rvalue_free(c, locp, &op1);
    rvalue_free(c, locp, &op2);
}

static void gen_mod_op(Context *c, YYLTYPE *locp, HexValue *res,
                       enum OpTypes op_types, HexValue *op1, HexValue *op2)
{
    switch (op_types) {
    case IMM_IMM:
        OUT(c, locp, "int64_t ", res, " = ", op1, " % ", op2, ";\n");
        break;
    case IMM_REG:
    case REG_IMM:
    case REG_REG:
        OUT(c, locp, "gen_helper_mod(",
            res, ", ", op1, ", ", op2, ");\n");
        break;
    }
    rvalue_free(c, locp, op1);
    rvalue_free(c, locp, op2);
}

/* Code generation functions */
HexValue gen_bin_op(Context *c,
                    YYLTYPE *locp,
                    enum OpType type,
                    HexValue *operand1,
                    HexValue *operand2)
{
    /* Replicate operands to avoid side effects */
    HexValue op1 = *operand1;
    HexValue op2 = *operand2;

    /* Enforce variables' size */
    if (op1.type == VARID) {
        int index = find_variable(c, locp, &op1);
        yyassert(c, locp, index >= 0, "Variable in bin_op must exist!\n");
        op1.bit_width = c->inst.allocated[index].bit_width;
    }
    if (op2.type == VARID) {
        int index = find_variable(c, locp, &op2);
        yyassert(c, locp, index >= 0, "Variable in bin_op must exist!\n");
        op2.bit_width = c->inst.allocated[index].bit_width;
    }

    enum OpTypes op_types = (op1.type != IMMEDIATE) << 1
                            | (op2.type != IMMEDIATE);

    /* Find bit width of the two operands, if at least one is 64 bit use a */
    /* 64bit operation, eventually extend 32bit operands. */
    bool op_is64bit = op1.bit_width == 64 || op2.bit_width == 64;
    /* Shift greater than 32 are 64 bits wide */
    if (type == ASL_OP && op2.type == IMMEDIATE &&
        op2.imm.type == VALUE && op2.imm.value >= 32)
        op_is64bit = true;
    const char *bit_suffix = op_is64bit ? "i64" : "i32";
    int bit_width = (op_is64bit) ? 64 : 32;
    /* Handle bit width */
    if (op_is64bit) {
        switch (op_types) {
        case IMM_IMM:
            break;
        case IMM_REG:
            op2 = rvalue_extend(c, locp, &op2);
            break;
        case REG_IMM:
            op1 = rvalue_extend(c, locp, &op1);
            break;
        case REG_REG:
            op1 = rvalue_extend(c, locp, &op1);
            op2 = rvalue_extend(c, locp, &op2);
            break;
        }
    }
    HexValue res;
    if (op_types != IMM_IMM) {
        res = gen_tmp(c, locp, bit_width);
    } else {
        res.type = IMMEDIATE;
        res.is_dotnew = false;
        res.is_manual = false;
        res.imm.type = QEMU_TMP;
        res.imm.index = c->inst.qemu_tmp_count;
        res.bit_width = bit_width;
    }
    /* Handle signedness, if both unsigned -> result is unsigned, else signed */
    res.is_unsigned = op1.is_unsigned && op2.is_unsigned;

    switch (type) {
    case ADD_OP:
        gen_add_op(c, locp, bit_width, bit_suffix, &res, op_types, &op1, &op2);
        break;
    case SUB_OP:
        gen_sub_op(c, locp, bit_width, bit_suffix, &res, op_types, &op1, &op2);
        break;
    case MUL_OP:
        gen_mul_op(c, locp, bit_suffix, &res, op_types, &op1, &op2);
        break;
    case DIV_OP:
        gen_div_op(c, locp, &res, op_types, &op1, &op2);
        break;
    case ASL_OP:
        gen_asl_op(c, locp, bit_width, op_is64bit, bit_suffix, &res, op_types,
                   &op1, &op2);
        break;
    case ASR_OP:
        gen_asr_op(c, locp, bit_width, bit_suffix, &res, op_types, &op1, &op2);
        break;
    case LSR_OP:
        gen_lsr_op(c, locp, bit_width, bit_suffix, &res, op_types, &op1, &op2);
        break;
    case ANDB_OP:
        gen_andb_op(c, locp, bit_width, bit_suffix, &res, op_types, &op1, &op2);
        break;
    case ORB_OP:
        gen_orb_op(c, locp, bit_width, bit_suffix, &res, op_types, &op1, &op2);
        break;
    case XORB_OP:
        gen_xorb_op(c, locp, bit_width, bit_suffix, &res, op_types, &op1, &op2);
        break;
    case ANDL_OP:
        gen_andl_op(c, locp, bit_width, bit_suffix, &res, op_types, &op1, &op2);
        break;
    case MINI_OP:
        gen_mini_op(c, locp, bit_width, &res, op_types, &op1, &op2);
        break;
    case MAXI_OP:
        gen_maxi_op(c, locp, bit_width, &res, op_types, &op1, &op2);
        break;
    case MOD_OP:
        gen_mod_op(c, locp, &res, op_types, &op1, &op2);
        break;
    }
    if (op_types == IMM_IMM) {
        c->inst.qemu_tmp_count++;
    }
    return res;
}

HexValue gen_cast_op(Context *c,
                     YYLTYPE *locp,
                     HexValue *source,
                     unsigned target_width) {
    if (source->bit_width == target_width) {
        return *source;
    } else if (source->type == IMMEDIATE) {
        HexValue res = *source;
        res.bit_width = target_width;
        return res;
    } else {
        HexValue res = gen_tmp(c, locp, target_width);
        /* Truncate */
        if (source->bit_width > target_width) {
            OUT(c, locp, "tcg_gen_trunc_i64_tl(", &res, ", ", source, ");\n");
        } else {
            if (source->is_unsigned) {
                /* Extend unsigned */
                OUT(c, locp, "tcg_gen_extu_i32_i64(",
                    &res, ", ", source, ");\n");
            } else {
                /* Extend signed */
                OUT(c, locp, "tcg_gen_ext_i32_i64(",
                    &res, ", ", source, ");\n");
            }
        }
        rvalue_free(c, locp, source);
        return res;
    }
}

HexValue gen_extend_op(Context *c,
                       YYLTYPE *locp,
                       HexValue *src_width_ptr,
                       HexValue *dst_width_ptr,
                       HexValue *value_ptr,
                       bool is_unsigned) {
    HexValue src_width = *src_width_ptr;
    HexValue dst_width = *dst_width_ptr;
    HexValue value = *value_ptr;
    src_width = rvalue_extend(c, locp, &src_width);
    value = rvalue_extend(c, locp, &value);
    src_width = rvalue_materialize(c, locp, &src_width);
    value = rvalue_materialize(c, locp, &value);

    HexValue res = gen_tmp(c, locp, 64);
    HexValue shift = gen_tmp_value(c, locp, "64", 64);
    HexValue zero = gen_tmp_value(c, locp, "0", 64);
    OUT(c, locp, "tcg_gen_sub_i64(",
        &shift, ", ", &shift, ", ", &src_width, ");\n");
    if (is_unsigned) {
        HexValue mask = gen_tmp_value(c, locp, "0xffffffffffffffff", 64);
        OUT(c, locp, "tcg_gen_shr_i64(",
            &mask, ", ", &mask, ", ", &shift, ");\n");
        OUT(c, locp, "tcg_gen_and_i64(",
            &res, ", ", &value, ", ", &mask, ");\n");
        rvalue_free(c, locp, &mask);
    } else {
        OUT(c, locp, "tcg_gen_shl_i64(",
            &res, ", ", &value, ", ", &shift, ");\n");
        OUT(c, locp, "tcg_gen_sar_i64(",
            &res, ", ", &res, ", ", &shift, ");\n");
    }
    OUT(c, locp, "tcg_gen_movcond_i64(", COND_EQ, ", ", &res, ", ");
    OUT(c, locp, &src_width, ", ", &zero, ", ", &zero, ", ", &res, ");\n");

    rvalue_free(c, locp, &src_width);
    rvalue_free(c, locp, &dst_width);
    rvalue_free(c, locp, &value);
    rvalue_free(c, locp, &shift);
    rvalue_free(c, locp, &zero);

    res.is_unsigned = is_unsigned;
    return res;
}

void gen_rdeposit_op(Context *c,
                     YYLTYPE *locp,
                     HexValue *dest,
                     HexValue *value,
                     HexValue *begin,
                     HexValue *width)
{
    HexValue dest_m = *dest;
    dest_m.is_manual = true;

    HexValue value_m = rvalue_extend(c, locp, value);
    HexValue begin_m = rvalue_extend(c, locp, begin);
    HexValue width_orig = *width;
    width_orig.is_manual = true;
    HexValue width_m = rvalue_extend(c, locp, &width_orig);
    width_m = rvalue_materialize(c, locp, &width_m);

    HexValue mask = gen_tmp_value(c, locp, "0xffffffffffffffffUL", 64);
    mask.is_unsigned = true;
    HexValue k64 = gen_tmp_value(c, locp, "64", 64);
    k64 = gen_bin_op(c, locp, SUB_OP, &k64, &width_m);
    mask = gen_bin_op(c, locp, LSR_OP, &mask, &k64);
    begin_m.is_manual = true;
    mask = gen_bin_op(c, locp, ASL_OP, &mask, &begin_m);
    mask.is_manual = true;
    value_m = gen_bin_op(c, locp, ASL_OP, &value_m, &begin_m);
    value_m = gen_bin_op(c, locp, ANDB_OP, &value_m, &mask);

    OUT(c, locp, "tcg_gen_not_i64(", &mask, ", ", &mask, ");\n");
    mask.is_manual = false;
    HexValue res = gen_bin_op(c, locp, ANDB_OP, &dest_m, &mask);
    res = gen_bin_op(c, locp, ORB_OP, &res, &value_m);

    if (dest->bit_width != res.bit_width) {
        res = rvalue_truncate(c, locp, &res);
    }

    HexValue zero = gen_tmp_value(c, locp, "0", res.bit_width);
    OUT(c, locp, "tcg_gen_movcond_i", &res.bit_width, "(TCG_COND_NE, ", dest);
    OUT(c, locp, ", ", &width_orig, ", ", &zero, ", ", &res, ", ", dest,
        ");\n");

    rvalue_free(c, locp, &zero);
    rvalue_free(c, locp, width);
    rvalue_free(c, locp, &res);
}

void gen_deposit_op(Context *c,
                    YYLTYPE *locp,
                    HexValue *dest,
                    HexValue *value,
                    HexValue *index,
                    HexCast *cast)
{
    yyassert(c, locp, index->type == IMMEDIATE,
             "Deposit index must be immediate!\n");
    HexValue value_m = *value;
    int bit_width = (dest->bit_width == 64) ? 64 : 32;
    int width = cast->bit_width;
    /* If the destination value is 32, truncate the value, otherwise extend */
    if (dest->bit_width != value->bit_width) {
        if (bit_width == 32) {
            value_m = rvalue_truncate(c, locp, &value_m);
        } else {
            value_m = rvalue_extend(c, locp, &value_m);
        }
    }
    value_m = rvalue_materialize(c, locp, &value_m);
    OUT(c, locp, "tcg_gen_deposit_i", &bit_width, "(", dest, ", ", dest, ", ");
    OUT(c, locp, &value_m, ", ", index, " * ", &width, ", ", &width, ");\n");
    rvalue_free(c, locp, index);
    rvalue_free(c, locp, &value_m);
}

HexValue gen_rextract_op(Context *c,
                         YYLTYPE *locp,
                         HexValue *source,
                         int begin,
                         int width) {
    int bit_width = (source->bit_width == 64) ? 64 : 32;
    HexValue res = gen_tmp(c, locp, bit_width);
    OUT(c, locp, "tcg_gen_extract_i", &bit_width, "(", &res);
    OUT(c, locp, ", ", source, ", ", &begin, ", ", &width, ");\n");
    rvalue_free(c, locp, source);
    return res;
}

HexValue gen_extract_op(Context *c,
                        YYLTYPE *locp,
                        HexValue *source,
                        HexValue *index,
                        HexExtract *extract) {
    yyassert(c, locp, index->type == IMMEDIATE,
             "Extract index must be immediate!\n");
    int bit_width = (source->bit_width == 64) ? 64 : 32;
    const char *sign_prefix = (extract->is_unsigned) ? "" : "s";
    int width = extract->bit_width;
    HexValue res = gen_tmp(c, locp, bit_width);
    res.is_unsigned = extract->is_unsigned;
    OUT(c, locp, "tcg_gen_", sign_prefix, "extract_i", &bit_width,
        "(", &res, ", ", source);
    OUT(c, locp, ", ", index, " * ", &width, ", ", &width, ");\n");

    /* Some extract operations have bit_width != storage_bit_width */
    if (extract->storage_bit_width > bit_width) {
        HexValue tmp = gen_tmp(c, locp, extract->storage_bit_width);
        tmp.is_unsigned = extract->is_unsigned;
        if (extract->is_unsigned) {
            /* Extend unsigned */
            OUT(c, locp, "tcg_gen_extu_i32_i64(",
                &tmp, ", ", &res, ");\n");
        } else {
            /* Extend signed */
            OUT(c, locp, "tcg_gen_ext_i32_i64(",
                &tmp, ", ", &res, ");\n");
        }
        rvalue_free(c, locp, &res);
        res = tmp;
    }

    rvalue_free(c, locp, source);
    rvalue_free(c, locp, index);
    return res;
}

HexValue gen_read_creg(Context *c, YYLTYPE *locp, HexValue *reg)
{
    yyassert(c, locp, reg->type == REGISTER, "reg must be a register!");
    if (reg->reg.id < 'a') {
        HexValue tmp = gen_tmp_value(c, locp, "0", 32);
        const char *id = creg_str[(uint8_t)reg->reg.id];
        OUT(c, locp, "READ_REG(", &tmp, ", ", id, ");\n");
        rvalue_free(c, locp, reg);
        return tmp;
    }
    return *reg;
}

void gen_write_creg(Context *c,
                    YYLTYPE *locp,
                    HexValue *reg,
                    HexValue *value)
{
    yyassert(c, locp, reg->type == REGISTER, "reg must be a register!");
    HexValue value_m = *value;
    value_m = rvalue_truncate(c, locp, &value_m);
    value_m = rvalue_materialize(c, locp, &value_m);
    OUT(c,
        locp,
        "gen_log_reg_write(", creg_str[(uint8_t)reg->reg.id], ", ",
        &value_m, ");\n");
    OUT(c,
        locp,
        "ctx_log_reg_write(ctx, ", creg_str[(uint8_t)reg->reg.id], ");\n");
    rvalue_free(c, locp, reg);
    rvalue_free(c, locp, &value_m);
}

void gen_assign(Context *c,
                YYLTYPE *locp,
                HexValue *dest,
                HexValue *value)
{
    HexValue value_m = *value;
    if (dest->type == REGISTER &&
        dest->reg.type == CONTROL && dest->reg.id < 'a') {
        gen_write_creg(c, locp, dest, &value_m);
        return;
    }
    /* Create (if not present) and assign to temporary variable */
    if (dest->type == VARID) {
        varid_allocate(c, locp, dest, value_m.bit_width, value_m.is_unsigned);
    }
    int bit_width = dest->bit_width == 64 ? 64 : 32;
    if (bit_width != value_m.bit_width) {
        if (bit_width == 64) {
            value_m = rvalue_extend(c, locp, &value_m);
        } else {
            value_m = rvalue_truncate(c, locp, &value_m);
        }
    }
    value_m = rvalue_materialize(c, locp, &value_m);
    if (value_m.type == IMMEDIATE) {
        OUT(c, locp, "tcg_gen_movi_i", &bit_width,
            "(", dest, ", ", &value_m, ");\n");
    } else {
        OUT(c, locp, "tcg_gen_mov_i", &bit_width,
            "(", dest, ", ", &value_m, ");\n");
    }
    rvalue_free(c, locp, &value_m);
}

HexValue gen_convround(Context *c,
                       YYLTYPE *locp,
                       HexValue *source)
{
    HexValue src = *source;
    src.is_manual = true;

    unsigned bit_width = src.bit_width;
    const char *size = (bit_width == 32) ? "32" : "64";
    HexValue res = gen_tmp(c, locp, bit_width);
    HexValue mask = gen_tmp_value(c, locp, "0x3", bit_width);
    mask.is_manual = true;
    HexValue and = gen_bin_op(c, locp, ANDB_OP, &src, &mask);
    HexValue one = gen_tmp_value(c, locp, "1", bit_width);
    HexValue src_p1 = gen_bin_op(c, locp, ADD_OP, &src, &one);

    OUT(c, locp, "tcg_gen_movcond_i", size, "(TCG_COND_EQ, ", &res);
    OUT(c, locp, ", ", &and, ", ", &mask, ", ");
    OUT(c, locp, &src_p1, ", ", &src, ");\n");

    /* Free src but use the original `is_manual` value */
    rvalue_free(c, locp, source);

    /* Free the rest of the values */
    rvalue_free_manual(c, locp, &mask);
    rvalue_free(c, locp, &and);
    rvalue_free(c, locp, &src_p1);

    return res;
}

static HexValue gen_convround_n_a(Context *c,
                                  YYLTYPE *locp,
                                  HexValue *a,
                                  HexValue *n)
{
    HexValue res = gen_tmp(c, locp, 64);
    OUT(c, locp, "tcg_gen_ext_i32_i64(", &res, ", ", a, ");\n");
    rvalue_free(c, locp, a);
    rvalue_free(c, locp, n);
    return res;
}

static HexValue gen_convround_n_b(Context *c,
                                  YYLTYPE *locp,
                                  HexValue *a,
                                  HexValue *n)
{
    HexValue res = gen_tmp(c, locp, 64);
    OUT(c, locp, "tcg_gen_ext_i32_i64(", &res, ", ", a, ");\n");

    HexValue one = gen_tmp_value(c, locp, "1", 32);
    HexValue tmp = gen_tmp(c, locp, 32);
    HexValue tmp_64 = gen_tmp(c, locp, 64);

    OUT(c, locp, "tcg_gen_shl_i32(", &tmp);
    OUT(c, locp, ", ", &one, ", ", n, ");\n");
    OUT(c, locp, "tcg_gen_and_i32(", &tmp);
    OUT(c, locp, ", ", &tmp, ", ", a, ");\n");
    OUT(c, locp, "tcg_gen_shri_i32(", &tmp);
    OUT(c, locp, ", ", &tmp, ", 1);\n");
    OUT(c, locp, "tcg_gen_ext_i32_i64(", &tmp_64, ", ", &tmp, ");\n");
    OUT(c, locp, "tcg_gen_add_i64(", &res);
    OUT(c, locp, ", ", &res, ", ", &tmp_64, ");\n");

    rvalue_free(c, locp, a);
    rvalue_free(c, locp, n);
    rvalue_free(c, locp, &one);
    rvalue_free(c, locp, &tmp);
    rvalue_free(c, locp, &tmp_64);

    return res;
}

static HexValue gen_convround_n_c(Context *c,
                                  YYLTYPE *locp,
                                  HexValue *a,
                                  HexValue *n)
{
    HexValue res = gen_tmp(c, locp, 64);
    OUT(c, locp, "tcg_gen_ext_i32_i64(", &res, ", ", a, ");\n");

    HexValue one = gen_tmp_value(c, locp, "1", 32);
    HexValue tmp = gen_tmp(c, locp, 32);
    HexValue tmp_64 = gen_tmp(c, locp, 64);

    OUT(c, locp, "tcg_gen_subi_i32(", &tmp);
    OUT(c, locp, ", ", n, ", 1);\n");
    OUT(c, locp, "tcg_gen_shl_i32(", &tmp);
    OUT(c, locp, ", ", &one, ", ", &tmp, ");\n");
    OUT(c, locp, "tcg_gen_ext_i32_i64(", &tmp_64, ", ", &tmp, ");\n");
    OUT(c, locp, "tcg_gen_add_i64(", &res);
    OUT(c, locp, ", ", &res, ", ", &tmp_64, ");\n");

    rvalue_free(c, locp, a);
    rvalue_free(c, locp, n);
    rvalue_free(c, locp, &one);
    rvalue_free(c, locp, &tmp);
    rvalue_free(c, locp, &tmp_64);

    return res;
}

HexValue gen_convround_n(Context *c,
                         YYLTYPE *locp,
                         HexValue *source_ptr,
                         HexValue *bit_pos_ptr)
{
    /* If input is 64 bit cast it to 32 */
    HexValue source = gen_cast_op(c, locp, source_ptr, 32);
    HexValue bit_pos = gen_cast_op(c, locp, bit_pos_ptr, 32);

    source = rvalue_materialize(c, locp, &source);
    bit_pos = rvalue_materialize(c, locp, &bit_pos);

    bool free_source_sym = !rvalue_equal(&source, source_ptr);
    bool free_bit_pos_sym = !rvalue_equal(&bit_pos, bit_pos_ptr);
    source.is_manual = true;
    bit_pos.is_manual = true;

    HexValue r1 = gen_convround_n_a(c, locp, &source, &bit_pos);
    HexValue r2 = gen_convround_n_b(c, locp, &source, &bit_pos);
    HexValue r3 = gen_convround_n_c(c, locp, &source, &bit_pos);

    HexValue l_32 = gen_tmp_value(c, locp, "1", 32);

    HexValue cond = gen_tmp(c, locp, 32);
    HexValue cond_64 = gen_tmp(c, locp, 64);
    HexValue mask = gen_tmp(c, locp, 32);
    HexValue n_64 = gen_tmp(c, locp, 64);
    HexValue res = gen_tmp(c, locp, 64);
    HexValue zero = gen_tmp_value(c, locp, "0", 64);

    OUT(c, locp, "tcg_gen_sub_i32(", &mask);
    OUT(c, locp, ", ", &bit_pos, ", ", &l_32, ");\n");
    OUT(c, locp, "tcg_gen_shl_i32(", &mask);
    OUT(c, locp, ", ", &l_32, ", ", &mask, ");\n");
    OUT(c, locp, "tcg_gen_sub_i32(", &mask);
    OUT(c, locp, ", ", &mask, ", ", &l_32, ");\n");
    OUT(c, locp, "tcg_gen_and_i32(", &cond);
    OUT(c, locp, ", ", &source, ", ", &mask, ");\n");
    OUT(c, locp, "tcg_gen_extu_i32_i64(", &cond_64, ", ", &cond, ");\n");
    OUT(c, locp, "tcg_gen_ext_i32_i64(", &n_64, ", ", &bit_pos, ");\n");

    OUT(c, locp, "tcg_gen_movcond_i64");
    OUT(c, locp, "(TCG_COND_EQ, ", &res, ", ", &cond_64, ", ", &zero);
    OUT(c, locp, ", ", &r2, ", ", &r3, ");\n");

    OUT(c, locp, "tcg_gen_movcond_i64");
    OUT(c, locp, "(TCG_COND_EQ, ", &res, ", ", &n_64, ", ", &zero);
    OUT(c, locp, ", ", &r1, ", ", &res, ");\n");

    OUT(c, locp, "tcg_gen_shr_i64(", &res);
    OUT(c, locp, ", ", &res, ", ", &n_64, ");\n");

    rvalue_free_ext(c, locp, &source, free_source_sym);
    rvalue_free_ext(c, locp, &bit_pos, free_bit_pos_sym);

    rvalue_free(c, locp, &r1);
    rvalue_free(c, locp, &r2);
    rvalue_free(c, locp, &r3);

    rvalue_free(c, locp, &cond);
    rvalue_free(c, locp, &cond_64);
    rvalue_free(c, locp, &l_32);
    rvalue_free(c, locp, &mask);
    rvalue_free(c, locp, &n_64);
    rvalue_free(c, locp, &zero);

    res = rvalue_truncate(c, locp, &res);
    return res;
}

HexValue gen_round(Context *c,
                   YYLTYPE *locp,
                   HexValue *source,
                   HexValue *position) {
    yyassert(c, locp, source->bit_width <= 32,
             "fRNDN not implemented for bit widths > 32!");

    HexValue src = *source;
    HexValue pos = *position;

    HexValue src_width = gen_imm_value(c, locp, src.bit_width, 32);
    HexValue dst_width = gen_imm_value(c, locp, 64, 32);
    HexValue a = gen_extend_op(c, locp, &src_width, &dst_width, &src, false);

    src_width = gen_imm_value(c, locp, 5, 32);
    dst_width = gen_imm_value(c, locp, 64, 32);
    HexValue b = gen_extend_op(c, locp, &src_width, &dst_width, &pos, true);

    /* Disable auto-free of values used more than once */
    a.is_manual = true;
    b.is_manual = true;

    HexValue res = gen_tmp(c, locp, 64);

    HexValue one = gen_tmp_value(c, locp, "1", 64);
    HexValue n_m1 = gen_bin_op(c, locp, SUB_OP, &b, &one);
    one = gen_tmp_value(c, locp, "1", 64);
    HexValue shifted = gen_bin_op(c, locp, ASL_OP, &one, &n_m1);
    HexValue sum = gen_bin_op(c, locp, ADD_OP, &shifted, &a);

    HexValue zero = gen_tmp_value(c, locp, "0", 64);
    OUT(c, locp, "tcg_gen_movcond_i64");
    OUT(c, locp, "(TCG_COND_EQ, ", &res, ", ", &b, ", ", &zero);
    OUT(c, locp, ", ", &a, ", ", &sum, ");\n");

    rvalue_free_manual(c, locp, &a);
    rvalue_free_manual(c, locp, &b);
    rvalue_free(c, locp, &zero);
    rvalue_free(c, locp, &sum);

    return res;
}

/* Circular addressing mode with auto-increment */
HexValue gen_circ_op(Context *c,
                     YYLTYPE *locp,
                     HexValue *addr,
                     HexValue *increment,
                     HexValue *modifier) {
    HexValue increment_m = *increment;
    HexValue res = gen_tmp(c, locp, addr->bit_width);
    res.is_unsigned = addr->is_unsigned;
    HexValue cs = gen_tmp(c, locp, 32);
    increment_m = rvalue_materialize(c, locp, &increment_m);
    OUT(c, locp, "READ_REG(", &cs, ", HEX_REG_CS0 + MuN);\n");
    OUT(c,
        locp,
        "gen_helper_fcircadd(",
        &res,
        ", ",
        addr,
        ", ",
        &increment_m,
        ", ",
        modifier);
    OUT(c, locp, ", ", &cs, ");\n");
    rvalue_free(c, locp, addr);
    rvalue_free(c, locp, &increment_m);
    rvalue_free(c, locp, modifier);
    rvalue_free(c, locp, &cs);
    return res;
}

HexValue gen_locnt_op(Context *c, YYLTYPE *locp, HexValue *source)
{
    HexValue source_m = *source;
    const char *bit_suffix = source->bit_width == 64 ? "64" : "32";
    HexValue res = gen_tmp(c, locp, source->bit_width == 64 ? 64 : 32);
    res.type = TEMP;
    source_m = rvalue_materialize(c, locp, &source_m);
    OUT(c, locp, "tcg_gen_not_i", bit_suffix, "(",
        &res, ", ", &source_m, ");\n");
    OUT(c, locp, "tcg_gen_clzi_i", bit_suffix, "(", &res, ", ", &res, ", ");
    OUT(c, locp, bit_suffix, ");\n");
    rvalue_free(c, locp, &source_m);
    return res;
}

HexValue gen_ctpop_op(Context *c, YYLTYPE *locp, HexValue *source)
{
    HexValue source_m = *source;
    const char *bit_suffix = source_m.bit_width == 64 ? "64" : "32";
    HexValue res = gen_tmp(c, locp, source_m.bit_width == 64 ? 64 : 32);
    res.type = TEMP;
    source_m = rvalue_materialize(c, locp, &source_m);
    OUT(c, locp, "tcg_gen_ctpop_i", bit_suffix,
        "(", &res, ", ", &source_m, ");\n");
    rvalue_free(c, locp, &source_m);
    return res;
}

HexValue gen_fbrev_4(Context *c, YYLTYPE *locp, HexValue *source)
{
    HexValue source_m = *source;

    HexValue res = gen_tmp(c, locp, 32);
    HexValue tmp1 = gen_tmp(c, locp, 32);
    HexValue tmp2 = gen_tmp(c, locp, 32);

    source_m = rvalue_materialize(c, locp, &source_m);
    source_m = rvalue_truncate(c, locp, &source_m);

    OUT(c, locp, "tcg_gen_mov_tl(", &res, ", ", &source_m, ");\n");
    OUT(c, locp, "tcg_gen_andi_tl(", &tmp1, ", ", &res, ", 0xaaaaaaaa);\n");
    OUT(c, locp, "tcg_gen_shri_tl(", &tmp1, ", ", &tmp1, ", 1);\n");
    OUT(c, locp, "tcg_gen_andi_tl(", &tmp2, ", ", &res, ", 0x55555555);\n");
    OUT(c, locp, "tcg_gen_shli_tl(", &tmp2, ", ", &tmp2, ", 1);\n");
    OUT(c, locp, "tcg_gen_or_tl(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");
    OUT(c, locp, "tcg_gen_andi_tl(", &tmp1, ", ", &res, ", 0xcccccccc);\n");
    OUT(c, locp, "tcg_gen_shri_tl(", &tmp1, ", ", &tmp1, ", 2);\n");
    OUT(c, locp, "tcg_gen_andi_tl(", &tmp2, ", ", &res, ", 0x33333333);\n");
    OUT(c, locp, "tcg_gen_shli_tl(", &tmp2, ", ", &tmp2, ", 2);\n");
    OUT(c, locp, "tcg_gen_or_tl(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");
    OUT(c, locp, "tcg_gen_andi_tl(", &tmp1, ", ", &res, ", 0xf0f0f0f0);\n");
    OUT(c, locp, "tcg_gen_shri_tl(", &tmp1, ", ", &tmp1, ", 4);\n");
    OUT(c, locp, "tcg_gen_andi_tl(", &tmp2, ", ", &res, ", 0x0f0f0f0f);\n");
    OUT(c, locp, "tcg_gen_shli_tl(", &tmp2, ", ", &tmp2, ", 4);\n");
    OUT(c, locp, "tcg_gen_or_tl(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");
    OUT(c, locp, "tcg_gen_andi_tl(", &tmp1, ", ", &res, ", 0xff00ff00);\n");
    OUT(c, locp, "tcg_gen_shri_tl(", &tmp1, ", ", &tmp1, ", 8);\n");
    OUT(c, locp, "tcg_gen_andi_tl(", &tmp2, ", ", &res, ", 0x00ff00ff);\n");
    OUT(c, locp, "tcg_gen_shli_tl(", &tmp2, ", ", &tmp2, ", 8);\n");
    OUT(c, locp, "tcg_gen_or_tl(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");
    OUT(c, locp, "tcg_gen_shri_tl(", &tmp1, ", ", &res, ", 16);\n");
    OUT(c, locp, "tcg_gen_shli_tl(", &tmp2, ", ", &res, ", 16);\n");
    OUT(c, locp, "tcg_gen_or_tl(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");

    rvalue_free(c, locp, &tmp1);
    rvalue_free(c, locp, &tmp2);
    rvalue_free(c, locp, &source_m);

    return res;
}

HexValue gen_fbrev_8(Context *c, YYLTYPE *locp, HexValue *source)
{
    HexValue source_m = *source;

    source_m = rvalue_extend(c, locp, &source_m);
    source_m = rvalue_materialize(c, locp, &source_m);

    HexValue res = gen_tmp(c, locp, 64);
    HexValue tmp1 = gen_tmp(c, locp, 64);
    HexValue tmp2 = gen_tmp(c, locp, 64);

    OUT(c, locp, "tcg_gen_mov_i64(",
        &res, ", ", &source_m, ");\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp1, ", ", &res, ", 0xaaaaaaaaaaaaaaaa);\n");
    OUT(c, locp, "tcg_gen_shri_i64(",
        &tmp1, ", ", &tmp1, ", 1);\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp2, ", ", &res, ", 0x5555555555555555);\n");
    OUT(c, locp, "tcg_gen_shli_i64(",
        &tmp2, ", ", &tmp2, ", 1);\n");
    OUT(c, locp, "tcg_gen_or_i64(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp1, ", ", &res, ", 0xcccccccccccccccc);\n");
    OUT(c, locp, "tcg_gen_shri_i64(",
        &tmp1, ", ", &tmp1, ", 2);\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp2, ", ", &res, ", 0x3333333333333333);\n");
    OUT(c, locp, "tcg_gen_shli_i64(",
        &tmp2, ", ", &tmp2, ", 2);\n");
    OUT(c, locp, "tcg_gen_or_i64(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp1, ", ", &res, ", 0xf0f0f0f0f0f0f0f0);\n");
    OUT(c, locp, "tcg_gen_shri_i64(",
        &tmp1, ", ", &tmp1, ", 4);\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp2, ", ", &res, ", 0x0f0f0f0f0f0f0f0f);\n");
    OUT(c, locp, "tcg_gen_shli_i64(",
        &tmp2, ", ", &tmp2, ", 4);\n");
    OUT(c, locp, "tcg_gen_or_i64(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp1, ", ", &res, ", 0xff00ff00ff00ff00);\n");
    OUT(c, locp, "tcg_gen_shri_i64(",
        &tmp1, ", ", &tmp1, ", 8);\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp2, ", ", &res, ", 0x00ff00ff00ff00ff);\n");
    OUT(c, locp, "tcg_gen_shli_i64(",
        &tmp2, ", ", &tmp2, ", 8);\n");
    OUT(c, locp, "tcg_gen_or_i64(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp1, ", ", &res, ", 0xffff0000ffff0000);\n");
    OUT(c, locp, "tcg_gen_shri_i64(",
        &tmp1, ", ", &tmp1, ", 16);\n");
    OUT(c, locp, "tcg_gen_andi_i64(",
        &tmp2, ", ", &res, ", 0x0000ffff0000ffff);\n");
    OUT(c, locp, "tcg_gen_shli_i64(",
        &tmp2, ", ", &tmp2, ", 16);\n");
    OUT(c, locp, "tcg_gen_or_i64(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");
    OUT(c, locp, "tcg_gen_shri_i64(", &tmp1, ", ", &res, ", 32);\n");
    OUT(c, locp, "tcg_gen_shli_i64(", &tmp2, ", ", &res, ", 32);\n");
    OUT(c, locp, "tcg_gen_or_i64(", &res, ", ", &tmp1, ", ", &tmp2, ");\n");

    rvalue_free(c, locp, &tmp1);
    rvalue_free(c, locp, &tmp2);
    rvalue_free(c, locp, &source_m);

    return res;
}

HexValue gen_rotl(Context *c, YYLTYPE *locp, HexValue *source, HexValue *n)
{
    const char *suffix = source->bit_width == 64 ? "i64" : "i32";

    HexValue res = gen_tmp(c, locp, source->bit_width);
    res.is_unsigned = source->is_unsigned;
    HexValue tmp_l = gen_tmp(c, locp, source->bit_width);
    HexValue tmp_r = gen_tmp(c, locp, source->bit_width);
    HexValue shr = gen_tmp(c, locp, source->bit_width);

    OUT(c, locp, "tcg_gen_movi_", suffix, "(",
        &shr, ", ", &source->bit_width, ");\n");
    OUT(c, locp, "tcg_gen_subi_", suffix, "(",
        &shr, ", ", &shr, ", ", n, ");\n");
    OUT(c, locp, "tcg_gen_shli_", suffix, "(",
        &tmp_l, ", ", source, ", ", n, ");\n");
    OUT(c, locp, "tcg_gen_shr_", suffix, "(",
        &tmp_r, ", ", source, ", ", &shr, ");\n");
    OUT(c, locp, "tcg_gen_or_", suffix, "(",
        &res, ", ", &tmp_l, ", ", &tmp_r, ");\n");

    rvalue_free(c, locp, source);
    rvalue_free(c, locp, n);
    rvalue_free(c, locp, &tmp_l);
    rvalue_free(c, locp, &tmp_r);
    rvalue_free(c, locp, &shr);

    return res;
}

const char *INTERLEAVE_MASKS[6] = {
    "0x5555555555555555ULL",
    "0x3333333333333333ULL",
    "0x0f0f0f0f0f0f0f0fULL",
    "0x00ff00ff00ff00ffULL",
    "0x0000ffff0000ffffULL",
    "0x00000000ffffffffULL",
};

HexValue gen_deinterleave(Context *c, YYLTYPE *locp, HexValue *mixed)
{
    HexValue src = rvalue_extend(c, locp, mixed);

    HexValue a = gen_tmp(c, locp, 64);
    a.is_unsigned = true;
    HexValue b = gen_tmp(c, locp, 64);
    b.is_unsigned = true;

    const char **masks = INTERLEAVE_MASKS;

    OUT(c, locp, "tcg_gen_shri_i64(", &a, ", ", &src, ", 1);\n");
    OUT(c, locp, "tcg_gen_andi_i64(", &a, ", ", &a, ", ", masks[0], ");\n");
    OUT(c, locp, "tcg_gen_andi_i64(", &b, ", ", &src, ", ", masks[0], ");\n");

    HexValue res = gen_tmp(c, locp, 64);
    res.is_unsigned = true;

    unsigned shift = 1;
    for (unsigned i = 1; i < 6; ++i) {
        OUT(c, locp, "tcg_gen_shri_i64(", &res, ", ", &b, ", ", &shift, ");\n");
        OUT(c, locp, "tcg_gen_or_i64(", &b, ", ", &res, ", ", &b, ");\n");
        OUT(c, locp, "tcg_gen_andi_i64(", &b, ", ", &b, ", ", masks[i], ");\n");
        OUT(c, locp, "tcg_gen_shri_i64(", &res, ", ", &a, ", ", &shift, ");\n");
        OUT(c, locp, "tcg_gen_or_i64(", &a, ", ", &res, ", ", &a, ");\n");
        OUT(c, locp, "tcg_gen_andi_i64(", &a, ", ", &a, ", ", masks[i], ");\n");
        shift <<= 1;
    }

    OUT(c, locp, "tcg_gen_shli_i64(", &a, ", ", &a, ", 32);\n");
    OUT(c, locp, "tcg_gen_or_i64(", &res, ", ", &a, ", ", &b, ");\n");

    rvalue_free(c, locp, &a);
    rvalue_free(c, locp, &b);

    return res;
}

HexValue gen_interleave(Context *c,
                        YYLTYPE *locp,
                        HexValue *odd,
                        HexValue *even)
{
    HexValue a = rvalue_truncate(c, locp, odd);
    a.is_unsigned = true;
    HexValue b = rvalue_truncate(c, locp, even);
    a.is_unsigned = true;

    a = rvalue_extend(c, locp, &a);
    b = rvalue_extend(c, locp, &b);

    HexValue res = gen_tmp(c, locp, 64);
    res.is_unsigned = true;

    const char **masks = INTERLEAVE_MASKS;

    unsigned shift = 16;
    for (int i = 4; i >= 0; --i) {
        OUT(c, locp, "tcg_gen_shli_i64(", &res, ", ", &a, ", ", &shift, ");\n");
        OUT(c, locp, "tcg_gen_or_i64(", &a, ", ", &res, ", ", &a, ");\n");
        OUT(c, locp, "tcg_gen_andi_i64(", &a, ", ", &a, ", ", masks[i], ");\n");
        OUT(c, locp, "tcg_gen_shli_i64(", &res, ", ", &b, ", ", &shift, ");\n");
        OUT(c, locp, "tcg_gen_or_i64(", &b, ", ", &res, ", ", &b, ");\n");
        OUT(c, locp, "tcg_gen_andi_i64(", &b, ", ", &b, ", ", masks[i], ");\n");
        shift >>= 1;
    }

    OUT(c, locp, "tcg_gen_shli_i64(", &a, ", ", &a, ", 1);\n");
    OUT(c, locp, "tcg_gen_or_i64(", &res, ", ", &a, ", ", &b, ");\n");

    rvalue_free(c, locp, &a);
    rvalue_free(c, locp, &b);

    return res;
}

bool reg_equal(HexReg *r1, HexReg *r2)
{
    return !memcmp(r1, r2, sizeof(HexReg));
}

bool pre_equal(HexPre *p1, HexPre *p2)
{
    return !memcmp(p1, p2, sizeof(HexPre));
}

bool rvalue_equal(HexValue *v1, HexValue *v2)
{
    if (v1->is_dotnew != v2->is_dotnew) {
        return false;
    } else if (v1->type == REGISTER && v2->type == REGISTER) {
        return reg_equal(&(v1->reg), &(v2->reg));
    } else if (v1->type == PREDICATE && v2->type == PREDICATE) {
        return pre_equal(&(v1->pre), &(v2->pre));
    } else {
        return false;
    }
}

void emit_header(Context *c)
{
    EMIT_SIG(c, START_COMMENT " %s " END_COMMENT "\n", c->inst.name);
    EMIT_SIG(c, "void emit_%s(DisasContext *ctx, Insn *insn, Packet *pkt",
             c->inst.name);
}

void emit_arg(Context *c, YYLTYPE *locp, HexValue *arg)
{
    switch (arg->type) {
    case REGISTER:
        if (arg->reg.type == DOTNEW) {
            EMIT_SIG(c, ", TCGv N%cN", arg->reg.id);
        } else {
            bool is64 = (arg->bit_width == 64);
            const char *type = is64 ? "TCGv_i64" : "TCGv_i32";
            char reg_id[5] = { 0 };
            reg_compose(c, locp, &(arg->reg), reg_id);
            EMIT_SIG(c, ", %s %s", type, reg_id);
            /* MuV register requires also MuN to provide its index */
            if (arg->reg.type == MODIFIER) {
                EMIT_SIG(c, ", int MuN");
            }
        }
        break;
    case PREDICATE:
        {
            char suffix = arg->is_dotnew ? 'N' : 'V';
            EMIT_SIG(c, ", TCGv P%c%c", arg->pre.id, suffix);
        }
        break;
    default:
        {
            fprintf(stderr, "emit_arg got unsupported argument!");
            abort();
        }
    }
}

void emit_footer(Context *c)
{
    EMIT(c, "}\n");
    EMIT(c, "\n");
}

void free_variables(Context *c, YYLTYPE *locp)
{
    for (unsigned i = 0; i < c->inst.allocated_count; ++i) {
        Var *var = &c->inst.allocated[i];
        const char *suffix = var->bit_width == 64 ? "i64" : "i32";
        OUT(c, locp, "tcg_temp_free_", suffix, "(", var->name, ");\n");
    }
}

void free_instruction(Context *c)
{
    /* Reset buffers */
    c->signature_c = 0;
    c->out_c = 0;
    c->header_c = 0;
    /* Free allocated register tracking */
    for (int i = 0; i < c->inst.allocated_count; i++) {
        free((char *)c->inst.allocated[i].name);
    }
    /* Free INAME token value */
    free(c->inst.name);
    /* Initialize instruction-specific portion of the context */
    memset(&(c->inst), 0, sizeof(Inst));
}
