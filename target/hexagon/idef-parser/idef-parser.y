%{
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

#include "idef-parser.h"
#include "parser-helpers.h"
#include "idef-parser.tab.h"
#include "idef-parser.yy.h"

/* Uncomment this to disable yyasserts */
/* #define NDEBUG */

#define ERR_LINE_CONTEXT 40

%}

%lex-param {void *scanner}
%parse-param {void *scanner}
%parse-param {Context *c}

/* Uncomment this for better errors in recent bison versions */
/* %define parse.error detailed */
%define parse.error verbose
%define parse.lac full
%define api.pure full

%locations

%union {
    char *string;
    HexValue rvalue;
    HexSat sat;
    HexCast cast;
    HexExtract extract;
    HexMpy mpy;
    bool is_unsigned;
    int index;
}

/* Tokens */
%start input

%expect 1

%token INAME DREG DIMM DPRE DEA RREG WREG FREG FIMM RPRE WPRE FPRE FWRAP FEA
%token VAR LBR RBR LPAR RPAR LSQ RSQ SEMI COLON PLUS MINUS MUL POW DIV MOD ABS
%token CROUND ROUND CIRCADD COUNTONES AND OR XOR NOT ASSIGN INC DEC ANDA ORA
%token XORA PLUSPLUS LT GT ASL ASR LSR EQ NEQ LTE GTE MIN MAX ANDL ORL NOTL
%token COMMA FOR ICIRC IF MUN FSCR FCHK SXT ZXT NEW CONSTEXT LOCNT BREV SIGN
%token LOAD STORE CONSTLL CONSTULL PC NPC LPCFG CANC QMARK IDENTITY PART1
%token BREV_4 BREV_8 ROTL INSBITS SETBITS EXTBITS EXTRANGE CAST4_8U SETOVF FAIL
%token DEINTERLEAVE INTERLEAVE

%token <rvalue> REG IMM PRE
%token <index> ELSE
%token <mpy> MPY
%token <sat> SAT
%token <cast> CAST DEPOSIT SETHALF
%token <extract> EXTRACT
%type <string> INAME
%type <rvalue> rvalue lvalue VAR assign_statement pre
%type <rvalue> DREG DIMM DPRE RREG RPRE FAIL
%type <index> if_stmt IF
%type <is_unsigned> SIGN

/* Operator Precedences */
%left MIN MAX
%left LPAR
%left COMMA
%left ASSIGN
%right CIRCADD
%right INC DEC ANDA ORA XORA
%left QMARK COLON
%left ORL
%left ANDL
%left OR
%left XOR ANDOR
%left AND
%left EQ NEQ
%left LT GT LTE GTE
%left ASL ASR LSR
%right ABS
%left MINUS PLUS
%left POW
%left MUL DIV MOD MPY
%right NOT NOTL
%left LSQ
%left NEW
%right CAST
%right LOCNT BREV

/* Bison Grammar */
%%

/* Input file containing the description of each hexagon instruction */
input : instructions
{
    YYACCEPT;
}
;

instructions : instruction instructions
| %empty
;

instruction : INAME
{
    /* Early-free if the parser failed on the previous instruction */
    free_instruction(c);

    c->total_insn++;
    c->inst.name = $1;
    emit_header(c);
}
arguments
{
    EMIT_SIG(c, ")");
    EMIT_HEAD(c, "{\n");

    /* Initialize declared but uninitialized registers, but only for */
    /* non-conditional instructions */
    for (int i = 0; i < c->inst.init_count; i++) {
        bool is64 = c->inst.init_list[i].bit_width == 64;
        const char *type = is64 ? "i64" : "i32";
        if (c->inst.init_list[i].type == REGISTER) {
            OUT(c, &@1, "tcg_gen_movi_", type,
                "(", &(c->inst.init_list[i]), ", 0);\n");
        } else if (c->inst.init_list[i].type == PREDICATE) {
            OUT(c, &@1, "tcg_gen_movi_", type,
                "(", &(c->inst.init_list[i]), ", 0);\n");
        }
    }
}
code
{
    if (c->inst.error_count != 0) {
        fprintf(stderr,
                "Parsing of instruction %s generated %d errors!\n",
                c->inst.name,
                c->inst.error_count);
        EMIT(c, "assert(false && \"This instruction is not implemented!\");");
    } else {
        free_variables(c, &@1);
        c->implemented_insn++;
        fprintf(c->enabled_file, "%s\n", c->inst.name);
        emit_footer(c);
        commit(c);
    }
    free_instruction(c);
}
| error /* Recover gracefully after instruction compilation error */
;

arguments : LPAR RPAR
|
LPAR argument_list RPAR
;

argument_list : decl COMMA argument_list
| decl
;

/* Return the modified registers list */
code : LBR statements RBR
{
    c->inst.code_begin = c->input_buffer + @2.first_column;
    c->inst.code_end = c->input_buffer + @2.last_column - 1;
}
|
LBR
{
    /* Nop */
}
RBR
;

decl : REG
{
    emit_arg(c, &@1, &$1);
    /* Enqueue register into initialization list */
    yyassert(c, &@1, c->inst.init_count < INIT_LIST_LEN,
             "init_count overflow");
    c->inst.init_list[c->inst.init_count] = $1;
    c->inst.init_count++;
}
| IMM
{
    EMIT_SIG(c, ", int %ciV", $1.imm.id);
}
| PRE
{
    emit_arg(c, &@1, &$1);
    /* Enqueue predicate into initialization list */
    c->inst.init_list[c->inst.init_count] = $1;
    c->inst.init_count++;
}
| VAR
{
    yyassert(c, &@1, !strcmp($1.var.name, "EA"), "Unknown argument variable!");
}
| RREG
{
    emit_arg(c, &@1, &$1);
}
| WREG
| FREG
| FIMM
| RPRE
{
    emit_arg(c, &@1, &$1);
}
| WPRE
| FPRE
| FEA
;

code_block : LBR statements RBR            { /* does nothing */ }
| LBR RBR                       { /* does nothing */ }
;

/* A list of one or more statements */
statements : statements statement         { /* does nothing */ }
| statement                    { /* does nothing */ }
;

/* Statements can be assignment (rvalue SEMI), control or memory statements */
statement : control_statement            { /* does nothing */ }
| rvalue SEMI                  { rvalue_free(c, &@1, &$1); }
| code_block                   { /* does nothing */ }
| SEMI                         { /* does nothing */ }
;

assign_statement : lvalue ASSIGN rvalue
{
    @1.last_column = @3.last_column;
    gen_assign(c, &@1, &$1, &$3);
    $$ = $1;
}
| lvalue INC rvalue
{
    @1.last_column = @3.last_column;
    HexValue tmp = gen_bin_op(c, &@1, ADD_OP, &$1, &$3);
    gen_assign(c, &@1, &$1, &tmp);
    $$ = $1;
}
| lvalue DEC rvalue
{
    @1.last_column = @3.last_column;
    HexValue tmp = gen_bin_op(c, &@1, SUB_OP, &$1, &$3);
    gen_assign(c, &@1, &$1, &tmp);
    $$ = $1;
}
| lvalue ANDA rvalue
{
    @1.last_column = @3.last_column;
    HexValue tmp = gen_bin_op(c, &@1, ANDB_OP, &$1, &$3);
    gen_assign(c, &@1, &$1, &tmp);
    $$ = $1;
}
| lvalue ORA rvalue
{
    @1.last_column = @3.last_column;
    HexValue tmp = gen_bin_op(c, &@1, ORB_OP, &$1, &$3);
    gen_assign(c, &@1, &$1, &tmp);
    $$ = $1;
}
| lvalue XORA rvalue
{
    @1.last_column = @3.last_column;
    HexValue tmp = gen_bin_op(c, &@1, XORB_OP, &$1, &$3);
    gen_assign(c, &@1, &$1, &tmp);
    $$ = $1;
}
| pre ASSIGN rvalue
{
    @1.last_column = @3.last_column;
    bool is_direct = is_direct_predicate(&$1);
    char pre_id[2] = " ";
    pre_id[0] = $1.pre.id;
    /* Extract predicate TCGv */
    if (is_direct) {
        $1 = gen_tmp_value(c, &@1, "0", 32);
    }
    $3 = rvalue_materialize(c, &@1, &$3);
    $3 = rvalue_truncate(c, &@1, &$3);
    /* Extract first 8 bits, and store new predicate value */
    if ($3.type == IMMEDIATE) {
        OUT(c, &@1, &$3, " = (", &$3, " & 0xff) << i;\n");
        OUT(c, &@1, "tcg_gen_ori_i32(", &$1, ", ", &$1, ", ", &$3, ");\n");
    } else {
        OUT(c, &@1, "tcg_gen_mov_i32(", &$1, ", ", &$3, ");\n");
        OUT(c, &@1, "tcg_gen_andi_i32(", &$1, ", ", &$1, ", 0xff);\n");
    }
    if (is_direct) {
        OUT(c, &@1, "gen_log_pred_write(", pre_id, ", ", &$1, ");\n");
        OUT(c, &@1, "ctx_log_pred_write(ctx, ", pre_id, ");\n");
        rvalue_free(c, &@1, &$1);
    }
    rvalue_free(c, &@1, &$3);  /* Free temporary value */
}
| IMM ASSIGN rvalue
{
    @1.last_column = @3.last_column;
    yyassert(c, &@1, $3.type == IMMEDIATE,
             "Cannot assign non-immediate to immediate!");
    yyassert(c, &@1, $1.imm.type == VARIABLE,
             "Cannot assign to non-variable!");
    /* Assign to the function argument */
    OUT(c, &@1, &$1, " = ", &$3, ";\n");
    $$ = $1;
}
| PC ASSIGN rvalue
{
    @1.last_column = @3.last_column;
    $3 = rvalue_truncate(c, &@1, &$3);
    $3 = rvalue_materialize(c, &@1, &$3);
    OUT(c, &@1, "gen_write_new_pc(", &$3, ");\n");
    rvalue_free(c, &@1, &$3); /* Free temporary value */
}
| LOAD LPAR IMM COMMA IMM COMMA SIGN COMMA VAR COMMA lvalue RPAR
{
    @1.last_column = @12.last_column;
    /* Memop width is specified in the load macro */
    int bit_width = ($5.imm.value > 4) ? 64 : 32;
    const char *sign_suffix = ($5.imm.value > 4) ? "" : (($7) ? "u" : "s");
    char size_suffix[4] = { 0 };
    /* Create temporary variable (if not present) */
    if ($11.type == VARID) {
        /* TODO: this is a common pattern, the parser should be varid-aware. */
        varid_allocate(c, &@1, &$11, bit_width, $7);
    }
    snprintf(size_suffix, 4, "%" PRIu64, $5.imm.value * 8);
    if (bit_width == 32) {
        $11 = rvalue_truncate(c, &@1, &$11);
    } else {
        $11 = rvalue_extend(c, &@1, &$11);
    }
    if ($9.type == VARID) {
        int var_id = find_variable(c, &@1, &$9);
        yyassert(c, &@1, var_id != -1, "Load variable must exist!\n");
        /* We need to enforce the variable size */
        $9.bit_width = c->inst.allocated[var_id].bit_width;
    }
    if ($9.bit_width != 32) {
        $9 = rvalue_truncate(c, &@1, &$9);
    }
    OUT(c, &@1, "if (insn->slot == 0 && pkt->pkt_has_store_s1) {\n");
    OUT(c, &@1, "process_store(ctx, pkt, 1);\n");
    OUT(c, &@1, "}\n");
    OUT(c, &@1, "tcg_gen_qemu_ld", size_suffix, sign_suffix);
    OUT(c, &@1, "(", &$11, ", ", &$9, ", 0);\n");
    /* If the var in $9 was truncated it is now a tmp HexValue, so free it. */
    rvalue_free(c, &@1, &$9);
}
| STORE LPAR IMM COMMA IMM COMMA VAR COMMA rvalue RPAR /* Store primitive */
{
    @1.last_column = @10.last_column;
    /* Memop width is specified in the store macro */
    int mem_width = $5.imm.value;
    /* Adjust operand bit width to memop bit width */
    if (mem_width < 8) {
        $9 = rvalue_truncate(c, &@1, &$9);
    } else {
        $9 = rvalue_extend(c, &@1, &$9);
    }
    if ($7.type == VARID) {
        int var_id = find_variable(c, &@1, &$7);
        yyassert(c, &@1, var_id != -1, "Load variable must exist!\n");
        /* We need to enforce the variable size */
        $7.bit_width = c->inst.allocated[var_id].bit_width;
    }
    if ($7.bit_width != 32) {
        $7 = rvalue_truncate(c, &@1, &$7);
    }
    $9 = rvalue_materialize(c, &@1, &$9);
    OUT(c, &@1, "gen_store", &mem_width, "(cpu_env, ", &$7, ", ", &$9);
    OUT(c, &@1, ", ctx, insn->slot);\n");
    rvalue_free(c, &@1, &$9);
    /* If the var in $7 was truncated it is now a tmp HexValue, so free it. */
    rvalue_free(c, &@1, &$7);
}
| LPCFG ASSIGN rvalue
{
    @1.last_column = @3.last_column;
    $3 = rvalue_truncate(c, &@1, &$3);
    $3 = rvalue_materialize(c, &@1, &$3);
    OUT(c, &@1, "SET_USR_FIELD(USR_LPCFG, ", &$3, ");\n");
    rvalue_free(c, &@1, &$3);
}
| DEPOSIT LPAR rvalue COMMA rvalue COMMA rvalue RPAR
{
    @1.last_column = @8.last_column;
    gen_deposit_op(c, &@1, &$5, &$7, &$3, &$1);
}
| SETHALF LPAR rvalue COMMA lvalue COMMA rvalue RPAR
{
    @1.last_column = @8.last_column;
    yyassert(c, &@1, $3.type == IMMEDIATE,
             "Deposit index must be immediate!\n");
    if ($5.type == VARID) {
        int var_id = find_variable(c, &@1, &$5);
        if (var_id == -1) {
            HexValue zero = gen_imm_value(c, &@1, 0, 64);
            zero.is_unsigned = true;
            $5.bit_width = 64;
            gen_assign(c, &@1, &$5, &zero);
        } else {
            /* We need to enforce the variable size (default is 32) */
            $5.bit_width = c->inst.allocated[var_id].bit_width;
        }
    }
    gen_deposit_op(c, &@1, &$5, &$7, &$3, &$1);
}
| SETBITS LPAR rvalue COMMA rvalue COMMA rvalue COMMA rvalue RPAR
{
    @1.last_column = @10.last_column;
    yyassert(c, &@1, $3.type == IMMEDIATE &&
             $3.imm.type == VALUE &&
             $5.type == IMMEDIATE &&
             $5.imm.type == VALUE,
             "Range deposit needs immediate values!\n");
    int i;
    $9 = rvalue_truncate(c, &@1, &$9);
    for (i = $5.imm.value; i <= $3.imm.value; ++i) {
        OUT(c, &@1, "gen_set_bit(", &i, ", ", &$7, ", ", &$9, ");\n");
    }
    rvalue_free(c, &@1, &$3);
    rvalue_free(c, &@1, &$5);
    rvalue_free(c, &@1, &$7);
    rvalue_free(c, &@1, &$9);
}
| INSBITS LPAR lvalue COMMA rvalue COMMA rvalue COMMA rvalue RPAR
{
    @1.last_column = @10.last_column;
    gen_rdeposit_op(c, &@1, &$3, &$9, &$7, &$5);
}
| IDENTITY LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    $$ = $3;
}
;

control_statement : frame_check          { /* does nothing */ }
| cancel_statement     { /* does nothing */ }
| if_statement         { /* does nothing */ }
| for_statement        { /* does nothing */ }
| fpart1_statement     { /* does nothing */ }
;

frame_check : FCHK LPAR rvalue COMMA rvalue RPAR SEMI  {
    /* does nothing */
    rvalue_free(c, &@1, &$3);
    rvalue_free(c, &@1, &$5);
}
;

cancel_statement : CANC
{
    OUT(c, &@1, "gen_cancel(insn->slot);\n");
}
;

if_statement : if_stmt
{
    /* Fix else label */
    OUT(c, &@1, "gen_set_label(if_label_", &$1, ");\n");
}
| if_stmt ELSE
{
    @1.last_column = @2.last_column;
    /* Generate label to jump if else is not verified */
    OUT(c, &@1, "TCGLabel *if_label_", &c->inst.if_count,
        " = gen_new_label();\n");
    $2 = c->inst.if_count;
    c->inst.if_count++;
    /* Jump out of the else statement */
    OUT(c, &@1, "tcg_gen_br(if_label_", &$2, ");\n");
    /* Fix the else label */
    OUT(c, &@1, "gen_set_label(if_label_", &$1, ");\n");
}
statement
{
    OUT(c, &@1, "gen_set_label(if_label_", &$2, ");\n");
}
;

for_statement : FOR LPAR IMM ASSIGN IMM SEMI IMM LT IMM SEMI IMM PLUSPLUS RPAR
{
    @1.last_column = @13.last_column;
    OUT(c, &@1, "for (int ", &$3, " = ", &$5, "; ", &$7, " < ", &$9);
    OUT(c, &@1, "; ", &$11, "++) {\n");
}
code_block
{
    OUT(c, &@1, "}\n");
}
;

for_statement : FOR LPAR IMM ASSIGN IMM SEMI IMM LT IMM SEMI IMM INC IMM RPAR
{
    @1.last_column = @14.last_column;
    OUT(c, &@1, "for (int ", &$3, " = ", &$5, "; ", &$7, " < ", &$9);
    OUT(c, &@1, "; ", &$11, " += ", &$13, ") {\n");
}
code_block
{
    OUT(c, &@1, "}\n");
}
;

fpart1_statement : PART1
{
    OUT(c, &@1, "if (insn->part1) {\n");
}
LPAR statements RPAR
{
    @1.last_column = @3.last_column;
    OUT(c, &@1, "return; }\n");
}
;

if_stmt : IF
{
    /* Generate an end label, if false branch to that label */
    OUT(c, &@1, "TCGLabel *if_label_", &c->inst.if_count,
        " = gen_new_label();\n");
}
LPAR rvalue RPAR
{
    @1.last_column = @3.last_column;
    $4 = rvalue_materialize(c, &@1, &$4);
    const char *bit_suffix = ($4.bit_width == 64) ? "i64" : "i32";
    OUT(c, &@1, "tcg_gen_brcondi_", bit_suffix, "(TCG_COND_EQ, ", &$4,
        ", 0, if_label_", &c->inst.if_count, ");\n");
    rvalue_free(c, &@1, &$4);
    $1 = c->inst.if_count;
    c->inst.if_count++;
}
statement
{
    $$ = $1;
}
;

rvalue : FAIL
{
    @1.last_column = @1.last_column;
    yyassert(c, &@1, false, "Encountered a FAIL token as rvalue.\n");
}
|
assign_statement            { /* does nothing */ }
| REG
{
    if ($1.reg.type == CONTROL) {
        $$ = gen_read_creg(c, &@1, &$1);
    } else {
        $$ = $1;
    }
}
| IMM
{
    $$ = $1;
}
| CONSTLL LPAR IMM RPAR
{
    $3.is_unsigned = false;
    $3.bit_width = 64;
    $$ = $3;
}
| CONSTULL LPAR IMM RPAR
{
    $3.is_unsigned = true;
    $3.bit_width = 64;
    $$ = $3;
}
| pre
{
    if (is_direct_predicate(&$1)) {
        bool is_dotnew = $1.is_dotnew;
        char predicate_id[2] = {$1.pre.id, '\0'};
        char *pre_str = (char *) &predicate_id;
        $1 = gen_tmp_value(c, &@1, "0", 32);
        if (is_dotnew) {
            OUT(c, &@1, "tcg_gen_mov_i32(", &$1, ", hex_new_pred_value[");
            OUT(c, &@1, pre_str, "]);\n");
        } else {
            OUT(c, &@1, "gen_read_preg(", &$1, ", ", pre_str, ");\n");
        }
    }
    $$ = $1;
}
| PC
{
    /* Read PC from the CR */
    $$ = gen_tmp(c, &@1, 32);
    OUT(c, &@1, "tcg_gen_mov_i32(", &$$, ", hex_gpr[HEX_REG_PC]);\n");
}
| NPC
{
    /* NPC is only read from CALLs, so we can hardcode it at translation time */
    $$ = gen_tmp(c, &@1, 32);
    OUT(c, &@1, "tcg_gen_movi_i32(", &$$, ", ctx->npc);\n");
}
| CONSTEXT
{
    HexValue rvalue;
    rvalue.type = IMMEDIATE;
    rvalue.imm.type = IMM_CONSTEXT;
    rvalue.is_unsigned = true;
    rvalue.is_dotnew = false;
    rvalue.is_manual = false;
    $$ = rvalue;
}
| VAR
{
    /* Assign correct bit width and signedness */
    bool found = false;
    for (int i = 0; i < c->inst.allocated_count; i++) {
        if (!strcmp($1.var.name, c->inst.allocated[i].name)) {
            found = true;
            free(c->inst.allocated[i].name);
            c->inst.allocated[i].name = $1.var.name;
            $1.bit_width = c->inst.allocated[i].bit_width;
            $1.is_unsigned = c->inst.allocated[i].is_unsigned;
            break;
        }
    }
    yyassert(c, &@1, found, "Undefined symbol!\n");
    $$ = $1;
}
| MPY LPAR rvalue COMMA rvalue RPAR
{
    @1.last_column = @6.last_column;
    $3.is_unsigned = $1.first_unsigned;
    $5.is_unsigned = $1.second_unsigned;
    $3 = gen_cast_op(c, &@1, &$3, $1.first_bit_width * 2);
    /* Handle fMPTY3216.. */
    if ($1.first_bit_width == 32) {
        $5 = gen_cast_op(c, &@1, &$5, 64);
    } else {
        $5 = gen_cast_op(c, &@1, &$5, $1.second_bit_width * 2);
    }
    $$ = gen_bin_op(c, &@1, MUL_OP, &$3, &$5);
    /* Handle special cases required by the language */
    if ($1.first_bit_width == 16 && $1.second_bit_width == 16) {
        HexValue src_width = gen_imm_value(c, &@1, 32, 32);
        HexValue dst_width = gen_imm_value(c, &@1, 64, 32);
        $$ = gen_extend_op(c, &@1, &src_width, &dst_width, &$$,
                           $1.first_unsigned && $1.second_unsigned);
    }
}
| rvalue PLUS rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, ADD_OP, &$1, &$3);
}
| rvalue MINUS rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, SUB_OP, &$1, &$3);
}
| rvalue MUL rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, MUL_OP, &$1, &$3);
}
| rvalue POW rvalue
{
    @1.last_column = @3.last_column;
    /* We assume that this is a shorthand for a shift */
    yyassert(c, &@1, $1.type == IMMEDIATE && $1.imm.value == 2,
             "Exponentiation is not a left shift!\n");
    HexValue one = gen_imm_value(c, &@1, 1, 32);
    HexValue shift = gen_bin_op(c, &@1, SUB_OP, &$3, &one);
    $$ = gen_bin_op(c, &@1, ASL_OP, &$1, &shift);
    rvalue_free(c, &@1, &one);
    rvalue_free(c, &@1, &shift);
}
| rvalue DIV rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, DIV_OP, &$1, &$3);
}
| rvalue MOD rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, MOD_OP, &$1, &$3);
}
| rvalue ASL rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, ASL_OP, &$1, &$3);
}
| rvalue ASR rvalue
{
    @1.last_column = @3.last_column;
    if ($1.is_unsigned) {
        $$ = gen_bin_op(c, &@1, LSR_OP, &$1, &$3);
    } else {
        $$ = gen_bin_op(c, &@1, ASR_OP, &$1, &$3);
    }
}
| rvalue LSR rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, LSR_OP, &$1, &$3);
}
| rvalue AND rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, ANDB_OP, &$1, &$3);
}
| rvalue OR rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, ORB_OP, &$1, &$3);
}
| rvalue XOR rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, XORB_OP, &$1, &$3);
}
| rvalue ANDL rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, ANDL_OP, &$1, &$3);
}
| MIN LPAR rvalue COMMA rvalue RPAR
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, MINI_OP, &$3, &$5);
}
| MAX LPAR rvalue COMMA rvalue RPAR
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_op(c, &@1, MAXI_OP, &$3, &$5);
}
| NOT rvalue
{
    @1.last_column = @2.last_column;
    const char *bit_suffix = ($2.bit_width == 64) ? "i64" : "i32";
    int bit_width = ($2.bit_width == 64) ? 64 : 32;
    HexValue res;
    res.is_unsigned = $2.is_unsigned;
    res.is_dotnew = false;
    res.is_manual = false;
    if ($2.type == IMMEDIATE) {
        res.type = IMMEDIATE;
        res.imm.type = QEMU_TMP;
        res.imm.index = c->inst.qemu_tmp_count;
        OUT(c, &@1, "int", &bit_width, "_t ", &res, " = ~", &$2, ";\n");
        c->inst.qemu_tmp_count++;
    } else {
        res = gen_tmp(c, &@1, bit_width);
        OUT(c, &@1, "tcg_gen_not_", bit_suffix, "(", &res,
            ", ", &$2, ");\n");
        rvalue_free(c, &@1, &$2);
    }
    $$ = res;
}
| NOTL rvalue
{
    @1.last_column = @2.last_column;
    const char *bit_suffix = ($2.bit_width == 64) ? "i64" : "i32";
    int bit_width = ($2.bit_width == 64) ? 64 : 32;
    HexValue res;
    res.is_unsigned = $2.is_unsigned;
    res.is_dotnew = false;
    res.is_manual = false;
    if ($2.type == IMMEDIATE) {
        res.type = IMMEDIATE;
        res.imm.type = QEMU_TMP;
        res.imm.index = c->inst.qemu_tmp_count;
        OUT(c, &@1, "int", &bit_width, "_t ", &res, " = !", &$2, ";\n");
        c->inst.qemu_tmp_count++;
        $$ = res;
    } else {
        res = gen_tmp(c, &@1, bit_width);
        HexValue zero = gen_tmp_value(c, &@1, "0", bit_width);
        HexValue one = gen_tmp_value(c, &@1, "0xff", bit_width);
        OUT(c, &@1, "tcg_gen_movcond_", bit_suffix);
        OUT(c, &@1, "(TCG_COND_EQ, ", &res, ", ", &$2, ", ", &zero);
        OUT(c, &@1, ", ", &one, ", ", &zero, ");\n");
        rvalue_free(c, &@1, &$2);
        rvalue_free(c, &@1, &zero);
        rvalue_free(c, &@1, &one);
        $$ = res;
    }
}
| SAT LPAR IMM COMMA rvalue RPAR
{
    @1.last_column = @6.last_column;
    if ($1.set_overflow) {
        yyassert(c, &@1, $3.imm.value < $5.bit_width, "To compute overflow, "
                 "source width must be greater than saturation width!");
    }
    HexValue res = gen_tmp(c, &@1, $5.bit_width);
    const char *bit_suffix = ($5.bit_width == 64) ? "i64" : "i32";
    const char *overflow_str = ($1.set_overflow) ? "true" : "false";
    const char *unsigned_str = ($1.is_unsigned) ? "u" : "";
    OUT(c, &@1, "gen_sat", unsigned_str, "_", bit_suffix, "(", &res, ", ");
    OUT(c, &@1, &$5, ", ", &$3.imm.value, ", ", overflow_str, ");\n");
    res.is_unsigned = $1.is_unsigned;
    rvalue_free(c, &@1, &$5);
    $$ = res;
}
| CAST rvalue
{
    @1.last_column = @2.last_column;
    /* Assign target signedness */
    $2.is_unsigned = $1.is_unsigned;
    $$ = gen_cast_op(c, &@1, &$2, $1.bit_width);
    $$.is_unsigned = $1.is_unsigned;
}
| rvalue LSQ rvalue RSQ
{
    @1.last_column = @4.last_column;
    HexValue one = gen_imm_value(c, &@1, 1, $3.bit_width);
    HexValue tmp = gen_bin_op(c, &@1, ASR_OP, &$1, &$3);
    $$ = gen_bin_op(c, &@1, ANDB_OP, &tmp, &one);
}
| rvalue EQ rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_cmp(c, &@1, "TCG_COND_EQ", &$1, &$3);
}
| rvalue NEQ rvalue
{
    @1.last_column = @3.last_column;
    $$ = gen_bin_cmp(c, &@1, "TCG_COND_NE", &$1, &$3);
}
| rvalue LT rvalue
{
    @1.last_column = @3.last_column;
    if ($1.is_unsigned || $3.is_unsigned) {
        $$ = gen_bin_cmp(c, &@1, "TCG_COND_LTU", &$1, &$3);
    } else {
        $$ = gen_bin_cmp(c, &@1, "TCG_COND_LT", &$1, &$3);
    }
}
| rvalue GT rvalue
{
    @1.last_column = @3.last_column;
    if ($1.is_unsigned || $3.is_unsigned) {
        $$ = gen_bin_cmp(c, &@1, "TCG_COND_GTU", &$1, &$3);
    } else {
        $$ = gen_bin_cmp(c, &@1, "TCG_COND_GT", &$1, &$3);
    }
}
| rvalue LTE rvalue
{
    @1.last_column = @3.last_column;
    if ($1.is_unsigned || $3.is_unsigned) {
        $$ = gen_bin_cmp(c, &@1, "TCG_COND_LEU", &$1, &$3);
    } else {
        $$ = gen_bin_cmp(c, &@1, "TCG_COND_LE", &$1, &$3);
    }
}
| rvalue GTE rvalue
{
    @1.last_column = @3.last_column;
    if ($1.is_unsigned || $3.is_unsigned) {
        $$ = gen_bin_cmp(c, &@1, "TCG_COND_GEU", &$1, &$3);
    } else {
        $$ = gen_bin_cmp(c, &@1, "TCG_COND_GE", &$1, &$3);
    }
}
| rvalue QMARK rvalue COLON rvalue
{
    @1.last_column = @5.last_column;
    bool is_64bit = ($3.bit_width == 64) || ($5.bit_width == 64);
    int bit_width = (is_64bit) ? 64 : 32;
    if (is_64bit) {
        $1 = rvalue_extend(c, &@1, &$1);
        $3 = rvalue_extend(c, &@1, &$3);
        $5 = rvalue_extend(c, &@1, &$5);
    } else {
        $1 = rvalue_truncate(c, &@1, &$1);
    }
    $1 = rvalue_materialize(c, &@1, &$1);
    $3 = rvalue_materialize(c, &@1, &$3);
    $5 = rvalue_materialize(c, &@1, &$5);
    HexValue res = gen_local_tmp(c, &@1, bit_width);
    HexValue zero = gen_tmp_value(c, &@1, "0", bit_width);
    OUT(c, &@1, "tcg_gen_movcond_i", &bit_width);
    OUT(c, &@1, "(TCG_COND_NE, ", &res, ", ", &$1, ", ", &zero);
    OUT(c, &@1, ", ", &$3, ", ", &$5, ");\n");
    rvalue_free(c, &@1, &zero);
    rvalue_free(c, &@1, &$1);
    rvalue_free(c, &@1, &$3);
    rvalue_free(c, &@1, &$5);
    $$ = res;
}
| FSCR LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    HexValue key = gen_tmp(c, &@1, 64);
    HexValue res = gen_tmp(c, &@1, 64);
    $3 = rvalue_extend(c, &@1, &$3);
    HexValue frame_key = gen_tmp(c, &@1, 32);
    OUT(c, &@1, "READ_REG(", &frame_key, ", HEX_REG_FRAMEKEY);\n");
    OUT(c, &@1, "tcg_gen_concat_i32_i64(",
        &key, ", ", &frame_key, ", ", &frame_key, ");\n");
    OUT(c, &@1, "tcg_gen_xor_i64(", &res, ", ", &$3, ", ", &key, ");\n");
    rvalue_free(c, &@1, &key);
    rvalue_free(c, &@1, &frame_key);
    rvalue_free(c, &@1, &$3);
    $$ = res;
}
| SXT LPAR rvalue COMMA IMM COMMA rvalue RPAR
{
    @1.last_column = @8.last_column;
    yyassert(c, &@1, $5.type == IMMEDIATE &&
             $5.imm.type == VALUE,
             "SXT expects immediate values\n");
    $5.imm.value = 64;
    $$ = gen_extend_op(c, &@1, &$3, &$5, &$7, false);
}
| ZXT LPAR rvalue COMMA IMM COMMA rvalue RPAR
{
    @1.last_column = @8.last_column;
    yyassert(c, &@1, $5.type == IMMEDIATE &&
             $5.imm.type == VALUE,
             "ZXT expects immediate values\n");
    $$ = gen_extend_op(c, &@1, &$3, &$5, &$7, true);
}
| LPAR rvalue RPAR
{
    $$ = $2;
}
| ABS rvalue
{
    @1.last_column = @2.last_column;
    const char *bit_suffix = ($2.bit_width == 64) ? "i64" : "i32";
    int bit_width = ($2.bit_width == 64) ? 64 : 32;
    HexValue res;
    res.is_unsigned = $2.is_unsigned;
    res.is_dotnew = false;
    res.is_manual = false;
    if ($2.type == IMMEDIATE) {
        res.type = IMMEDIATE;
        res.imm.type = QEMU_TMP;
        res.imm.index = c->inst.qemu_tmp_count;
        OUT(c, &@1, "int", &bit_width, "_t ", &res, " = abs(", &$2, ");\n");
        c->inst.qemu_tmp_count++;
        $$ = res;
    } else {
        res = gen_tmp(c, &@1, bit_width);
        HexValue zero = gen_tmp_value(c, &@1, "0", bit_width);
        OUT(c, &@1, "tcg_gen_neg_", bit_suffix, "(", &res, ", ",
            &$2, ");\n");
        OUT(c, &@1, "tcg_gen_movcond_i", &bit_width);
        OUT(c, &@1, "(TCG_COND_GT, ", &res, ", ", &$2, ", ", &zero);
        OUT(c, &@1, ", ", &$2, ", ", &res, ");\n");
        rvalue_free(c, &@1, &zero);
        rvalue_free(c, &@1, &$2);
        $$ = res;
    }
}
| CROUND LPAR rvalue COMMA rvalue RPAR
{
    @1.last_column = @6.last_column;
    $$ = gen_convround_n(c, &@1, &$3, &$5);
}
| CROUND LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    $$ = gen_convround(c, &@1, &$3);
}
| ROUND LPAR rvalue COMMA rvalue RPAR
{
    @1.last_column = @6.last_column;
    $$ = gen_round(c, &@1, &$3, &$5);
}
| MINUS rvalue
{
    @1.last_column = @2.last_column;
    const char *bit_suffix = ($2.bit_width == 64) ? "i64" : "i32";
    int bit_width = ($2.bit_width == 64) ? 64 : 32;
    HexValue res;
    res.is_unsigned = $2.is_unsigned;
    res.is_dotnew = false;
    res.is_manual = false;
    if ($2.type == IMMEDIATE) {
        res.type = IMMEDIATE;
        res.imm.type = QEMU_TMP;
        res.imm.index = c->inst.qemu_tmp_count;
        OUT(c, &@1, "int", &bit_width, "_t ", &res, " = -", &$2, ";\n");
        c->inst.qemu_tmp_count++;
        $$ = res;
    } else {
        res = gen_tmp(c, &@1, bit_width);
        OUT(c, &@1, "tcg_gen_neg_", bit_suffix, "(", &res, ", ",
            &$2, ");\n");
        rvalue_free(c, &@1, &$2);
        $$ = res;
    }
}
| ICIRC LPAR rvalue RPAR ASL IMM
{
    @1.last_column = @6.last_column;
    $$ = gen_tmp(c, &@1, 32);
    OUT(c, &@1, "gen_read_ireg(", &$$, ", ", &$3, ", ", &$6, ");\n");
    rvalue_free(c, &@1, &$3);
}
| CIRCADD LPAR rvalue COMMA rvalue COMMA rvalue RPAR
{
    @1.last_column = @8.last_column;
    $$ = gen_circ_op(c, &@1, &$3, &$5, &$7);
}
| LOCNT LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    /* Leading ones count */
    $$ = gen_locnt_op(c, &@1, &$3);
}
| COUNTONES LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    /* Ones count */
    $$ = gen_ctpop_op(c, &@1, &$3);
}
| LPCFG
{
    $$ = gen_tmp_value(c, &@1, "0", 32);
    OUT(c, &@1, "tcg_gen_extract_tl(", &$$, ", hex_gpr[HEX_REG_USR], ");
    OUT(c, &@1, "reg_field_info[USR_LPCFG].offset, ");
    OUT(c, &@1, "reg_field_info[USR_LPCFG].width);\n");
}
| EXTRACT LPAR rvalue COMMA rvalue RPAR
{
    @1.last_column = @6.last_column;
    $$ = gen_extract_op(c, &@1, &$5, &$3, &$1);
}
| EXTBITS LPAR rvalue COMMA rvalue COMMA rvalue RPAR
{
    @1.last_column = @8.last_column;
    yyassert(c, &@1, $5.type == IMMEDIATE &&
             $5.imm.type == VALUE &&
             $7.type == IMMEDIATE &&
             $7.imm.type == VALUE,
             "Range extract needs immediate values!\n");
    $$ = gen_rextract_op(c, &@1, &$3, $7.imm.value, $5.imm.value);
}
| EXTRANGE LPAR rvalue COMMA rvalue COMMA rvalue RPAR
{
    @1.last_column = @8.last_column;
    yyassert(c, &@1, $5.type == IMMEDIATE &&
             $5.imm.type == VALUE &&
             $7.type == IMMEDIATE &&
             $7.imm.type == VALUE,
             "Range extract needs immediate values!\n");
    $$ = gen_rextract_op(c,
                         &@1,
                         &$3,
                         $7.imm.value,
                         $5.imm.value - $7.imm.value + 1);
}
| CAST4_8U LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    $$ = rvalue_truncate(c, &@1, &$3);
    $$.is_unsigned = true;
    $$ = rvalue_materialize(c, &@1, &$$);
    $$ = rvalue_extend(c, &@1, &$$);
}
| BREV LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    yyassert(c, &@1, $3.bit_width <= 32,
             "fbrev not implemented for 64-bit integers!");
    HexValue res = gen_tmp(c, &@1, $3.bit_width);
    $3 = rvalue_materialize(c, &@1, &$3);
    OUT(c, &@1, "gen_fbrev(", &res, ", ", &$3, ");\n");
    rvalue_free(c, &@1, &$3);
    $$ = res;
}
| BREV_4 LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    $$ = gen_fbrev_4(c, &@1, &$3);
}
| BREV_8 LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    $$ = gen_fbrev_8(c, &@1, &$3);
}
| ROTL LPAR rvalue COMMA rvalue RPAR
{
    @1.last_column = @6.last_column;
    $$ = gen_rotl(c, &@1, &$3, &$5);
}
| SETOVF LPAR RPAR
{
    @1.last_column = @3.last_column;
    OUT(c, &@1, "gen_set_usr_fieldi(USR_OVF, 1);\n");
}
| SETOVF LPAR rvalue RPAR
{
    /* Convenience fSET_OVERFLOW with pass-through */
    @1.last_column = @3.last_column;
    OUT(c, &@1, "gen_set_usr_fieldi(USR_OVF, 1);\n");
    $$ = $3;
}
| DEINTERLEAVE LPAR rvalue RPAR
{
    @1.last_column = @4.last_column;
    $$ = gen_deinterleave(c, &@1, &$3);
}
| INTERLEAVE LPAR rvalue COMMA rvalue RPAR
{
    @1.last_column = @6.last_column;
    $$ = gen_interleave(c, &@1, &$3, &$5);
}
;

pre : PRE
{
    $$ = $1;
}
| pre NEW
{
    $$ = $1;
    $$.is_dotnew = true;
}
;

lvalue : FAIL
{
    @1.last_column = @1.last_column;
    yyassert(c, &@1, false, "Encountered a FAIL token as lvalue.\n");
}
| REG
{
    $$ = $1;
}
| VAR
{
    $$ = $1;
}
;

%%

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr,
                "Semantics: Hexagon ISA to tinycode generator compiler\n\n");
        fprintf(stderr,
                "Usage: ./semantics IDEFS EMITTER_C EMITTER_H "
                "ENABLED_INSTRUCTIONS_LIST\n");
        return 1;
    }

    enum {
        ARG_INDEX_ARGV0 = 0,
        ARG_INDEX_IDEFS,
        ARG_INDEX_EMITTER_C,
        ARG_INDEX_EMITTER_H,
        ARG_INDEX_ENABLED_INSTRUCTIONS_LIST
    };

    FILE *enabled_file = fopen(argv[ARG_INDEX_ENABLED_INSTRUCTIONS_LIST], "w");

    FILE *output_file = fopen(argv[ARG_INDEX_EMITTER_C], "w");
    fputs("#include \"qemu/osdep.h\"\n", output_file);
    fputs("#include \"qemu/log.h\"\n", output_file);
    fputs("#include \"cpu.h\"\n", output_file);
    fputs("#include \"internal.h\"\n", output_file);
    fputs("#include \"tcg/tcg-op.h\"\n", output_file);
    fputs("#include \"insn.h\"\n", output_file);
    fputs("#include \"opcodes.h\"\n", output_file);
    fputs("#include \"translate.h\"\n", output_file);
    fputs("#define QEMU_GENERATE\n", output_file);
    fputs("#include \"genptr.h\"\n", output_file);
    fputs("#include \"tcg/tcg.h\"\n", output_file);
    fputs("#include \"macros.h\"\n", output_file);
    fprintf(output_file, "#include \"%s\"\n", argv[ARG_INDEX_EMITTER_H]);

    FILE *defines_file = fopen(argv[ARG_INDEX_EMITTER_H], "w");
    assert(defines_file != NULL);
    fputs("#ifndef HEX_EMITTER_H\n", defines_file);
    fputs("#define HEX_EMITTER_H\n", defines_file);
    fputs("\n", defines_file);
    fputs("#include \"insn.h\"\n\n", defines_file);

    /* Parser input file */
    Context context = { 0 };
    context.defines_file = defines_file;
    context.output_file = output_file;
    context.enabled_file = enabled_file;
    /* Initialize buffers */
    context.out_buffer = (char *) calloc(OUT_BUF_LEN, sizeof(char));
    context.signature_buffer = (char *) calloc(SIGNATURE_BUF_LEN, sizeof(char));
    context.header_buffer = (char *) calloc(HEADER_BUF_LEN, sizeof(char));
    /* Read input file */
    FILE *input_file = fopen(argv[ARG_INDEX_IDEFS], "r");
    fseek(input_file, 0L, SEEK_END);
    long input_size = ftell(input_file);
    context.input_buffer = (char *) calloc(input_size + 1, sizeof(char));
    fseek(input_file, 0L, SEEK_SET);
    size_t read_chars = fread(context.input_buffer,
                              sizeof(char),
                              input_size,
                              input_file);
    if (read_chars != input_size) {
        fprintf(stderr, "Error: an error occurred while reading input file!\n");
        return -1;
    }
    yylex_init(&context.scanner);
    YY_BUFFER_STATE buffer;
    buffer = yy_scan_string(context.input_buffer, context.scanner);
    /* Start the parsing procedure */
    yyparse(context.scanner, &context);
    if (context.implemented_insn != context.total_insn) {
        fprintf(stderr, "%d/%d meta instructions have been implemented!\n",
                context.implemented_insn,
                context.total_insn);
    }
    fputs("#endif " START_COMMENT " HEX_EMITTER_h " END_COMMENT "\n",
          defines_file);
    /* Cleanup */
    yy_delete_buffer(buffer, context.scanner);
    yylex_destroy(context.scanner);
    fclose(output_file);
    fclose(input_file);
    fclose(defines_file);
    fclose(enabled_file);
    free(context.input_buffer);
    free(context.out_buffer);
    free(context.signature_buffer);

    return 0;
}
