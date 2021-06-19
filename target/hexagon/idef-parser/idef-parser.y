%{
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

%define parse.error verbose
%define parse.lac full
%define api.pure full

%locations

%union {
    GString *string;
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

%token INAME DREG DIMM DPRE DEA RREG WREG FREG FIMM RPRE WPRE FPRE FWRAP FEA VAR
%token POW ABS CROUND ROUND CIRCADD COUNTONES INC DEC ANDA ORA XORA PLUSPLUS ASL
%token ASR LSR EQ NEQ LTE GTE MIN MAX ANDL ORL FOR ICIRC IF MUN FSCR FCHK SXT
%token ZXT CONSTEXT LOCNT BREV SIGN LOAD STORE CONSTLL CONSTULL PC NPC LPCFG
%token CANCEL IDENTITY PART1 BREV_4 BREV_8 ROTL INSBITS SETBITS EXTBITS EXTRANGE
%token CAST4_8U SETOVF FAIL DEINTERLEAVE INTERLEAVE CARRY_FROM_ADD LSBNEW

%token <rvalue> REG IMM PRE
%token <index> ELSE
%token <mpy> MPY
%token <sat> SAT
%token <cast> CAST DEPOSIT SETHALF
%token <extract> EXTRACT
%type <string> INAME
%type <rvalue> rvalue lvalue VAR assign_statement var
%type <rvalue> DREG DIMM DPRE RREG RPRE FAIL
%type <index> if_stmt IF
%type <is_unsigned> SIGN

/* Operator Precedences */
%left MIN MAX
%left '('
%left ','
%left '='
%right CIRCADD
%right INC DEC ANDA ORA XORA
%left '?' ':'
%left ORL
%left ANDL
%left '|'
%left '^' ANDOR
%left '&'
%left EQ NEQ
%left '<' '>' LTE GTE
%left ASL ASR LSR
%right ABS
%left '-' '+'
%left POW
%left '*' '/' '%' MPY
%right '~' '!'
%left '['
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
                  gen_inst(c, $1);
              }
              arguments
              {
                  gen_inst_args(c, &@1);
              }
              code
              {
                  gen_inst_code(c, &@1);
              }
            | error /* Recover gracefully after instruction compilation error */
              {
                  free_instruction(c);
              }
            ;

arguments : '(' ')'
          | '(' argument_list ')';

argument_list : decl ',' argument_list
              | decl
              ;

var : VAR
      {
          track_string(c, $1.var.name);
          $$ = $1;
      }
    ;

/* Return the modified registers list */
code : '{' statements '}'
       {
           c->inst.code_begin = c->input_buffer + @2.first_column;
           c->inst.code_end = c->input_buffer + @2.last_column - 1;
       }
     | '{'
       {
           /* Nop */
       }
       '}'
     ;

decl : REG
       {
           emit_arg(c, &@1, &$1);
           /* Enqueue register into initialization list */
           g_array_append_val(c->inst.init_list, $1);
       }
     | IMM
       {
           EMIT_SIG(c, ", int %ciV", $1.imm.id);
       }
     | PRE
       {
           emit_arg(c, &@1, &$1);
           /* Enqueue predicate into initialization list */
           g_array_append_val(c->inst.init_list, $1);
       }
     | var
       {
           yyassert(c, &@1, !strcmp($1.var.name->str, "EA"),
                    "Unknown argument variable!");
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

code_block : '{' statements '}'
           | '{' '}'
           ;

/* A list of one or more statements */
statements : statements statement
           | statement
           ;

/* Statements can be assignment (rvalue ';'), control or memory statements */
statement : control_statement
          | rvalue ';'
            {
                rvalue_free(c, &@1, &$1);
            }
          | code_block
          | ';'
          ;

assign_statement : lvalue '=' rvalue
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
                 | PRE '=' rvalue
                   {
                       @1.last_column = @3.last_column;
                       gen_pre_assign(c, &@1, &$1, &$3);
                   }
                 | IMM '=' rvalue
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
                 | PC '=' rvalue
                   {
                       @1.last_column = @3.last_column;
                       yyassert(c, &@1, c->ternary->len == 0,
                                "Assignment side-effect not modeled!");
                       $3 = rvalue_truncate(c, &@1, &$3);
                       $3 = rvalue_materialize(c, &@1, &$3);
                       OUT(c, &@1, "gen_write_new_pc(", &$3, ");\n");
                       rvalue_free(c, &@1, &$3); /* Free temporary value */
                   }
                 | LOAD '(' IMM ',' IMM ',' SIGN ',' var ',' lvalue ')'
                   {
                       @1.last_column = @12.last_column;
                       yyassert(c, &@1, c->ternary->len == 0,
                                "Assignment side-effect not modeled!");
                       gen_load(c, &@1, &$3, &$5, $7, &$9, &$11);
                   }
                 | STORE '(' IMM ',' IMM ',' var ',' rvalue ')'
                   /* Store primitive */
                   {
                       @1.last_column = @10.last_column;
                       yyassert(c, &@1, c->ternary->len == 0,
                                "Assignment side-effect not modeled!");
                       gen_store(c, &@1, &$3, &$5, &$7, &$9);
                   }
                 | LPCFG '=' rvalue
                   {
                       @1.last_column = @3.last_column;
                       yyassert(c, &@1, c->ternary->len == 0,
                                "Assignment side-effect not modeled!");
                       $3 = rvalue_truncate(c, &@1, &$3);
                       $3 = rvalue_materialize(c, &@1, &$3);
                       OUT(c, &@1, "SET_USR_FIELD(USR_LPCFG, ", &$3, ");\n");
                       rvalue_free(c, &@1, &$3);
                   }
                 | DEPOSIT '(' rvalue ',' rvalue ',' rvalue ')'
                   {
                       @1.last_column = @8.last_column;
                       yyassert(c, &@1, c->ternary->len == 0,
                                "Assignment side-effect not modeled!");
                       gen_deposit_op(c, &@1, &$5, &$7, &$3, &$1);
                   }
                 | SETHALF '(' rvalue ',' lvalue ',' rvalue ')'
                   {
                       @1.last_column = @8.last_column;
                       yyassert(c, &@1, c->ternary->len == 0,
                                "Assignment side-effect not modeled!");
                       gen_sethalf(c, &@1, &$1, &$3, &$5, &$7);
                   }
                 | SETBITS '(' rvalue ',' rvalue ',' rvalue ',' rvalue ')'
                   {
                       @1.last_column = @10.last_column;
                       yyassert(c, &@1, c->ternary->len == 0,
                                "Assignment side-effect not modeled!");
                       gen_setbits(c, &@1, &$3, &$5, &$7, &$9);
                   }
                 | INSBITS '(' lvalue ',' rvalue ',' rvalue ',' rvalue ')'
                   {
                       @1.last_column = @10.last_column;
                       yyassert(c, &@1, c->ternary->len == 0,
                                "Assignment side-effect not modeled!");
                       gen_rdeposit_op(c, &@1, &$3, &$9, &$7, &$5);
                   }
                 | IDENTITY '(' rvalue ')'
                   {
                       @1.last_column = @4.last_column;
                       $$ = $3;
                   }
                 ;

control_statement : frame_check
                  | cancel_statement
                  | if_statement
                  | for_statement
                  | fpart1_statement
                  ;

frame_check : FCHK '(' rvalue ',' rvalue ')' ';'
              {
                  rvalue_free(c, &@1, &$3);
                  rvalue_free(c, &@1, &$5);
              }
            ;

cancel_statement : CANCEL
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
                   $2 = gen_if_else(c, &@1, $1);
               }
               statement
               {
                   OUT(c, &@1, "gen_set_label(if_label_", &$2, ");\n");
               }
             ;

for_statement : FOR '(' IMM '=' IMM ';' IMM '<' IMM ';' IMM PLUSPLUS ')'
                {
                    @1.last_column = @13.last_column;
                    OUT(c, &@1, "for (int ", &$3, " = ", &$5, "; ",
                        &$7, " < ", &$9);
                    OUT(c, &@1, "; ", &$11, "++) {\n");
                }
                code_block
                {
                    OUT(c, &@1, "}\n");
                }
              | FOR '(' IMM '=' IMM ';' IMM '<' IMM ';' IMM INC IMM ')'
                {
                    @1.last_column = @14.last_column;
                    OUT(c, &@1, "for (int ", &$3, " = ", &$5, "; ",
                        &$7, " < ", &$9);
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
                   '(' statements ')'
                   {
                       @1.last_column = @3.last_column;
                       OUT(c, &@1, "return; }\n");
                   }
                 ;

if_stmt : IF '(' rvalue ')'
          {
              @1.last_column = @3.last_column;
              $1 = gen_if_cond(c, &@1, &$3);
          }
          statement
          {
              $$ = $1;
          }
        ;

rvalue : FAIL
         {
             yyassert(c, &@1, false, "Encountered a FAIL token as rvalue.\n");
         }
       | assign_statement
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
       | CONSTLL '(' IMM ')'
         {
             $3.is_unsigned = false;
             $3.bit_width = 64;
             $$ = $3;
         }
       | CONSTULL '(' IMM ')'
         {
             $3.is_unsigned = true;
             $3.bit_width = 64;
             $$ = $3;
         }
       | PRE
         {
             $$ = gen_rvalue_pre(c, &@1, &$1);
         }
       | PC
         {
             /* Read PC from the CR */
             HexValue rvalue;
             rvalue.type = IMMEDIATE;
             rvalue.imm.type = IMM_PC;
             rvalue.bit_width = 32;
             rvalue.is_unsigned = true;
             $$ = rvalue;
         }
       | NPC
         {
             /* NPC is only read from CALLs, so we can hardcode it
                at translation time */
             HexValue rvalue;
             rvalue.type = IMMEDIATE;
             rvalue.imm.type = IMM_NPC;
             rvalue.bit_width = 32;
             rvalue.is_unsigned = true;
             $$ = rvalue;
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
       | var
         {
             $$ = gen_rvalue_var(c, &@1, &$1);
         }
       | MPY '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_rvalue_mpy(c, &@1, &$1, &$3, &$5);
         }
       | rvalue '+' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, ADD_OP, &$1, &$3);
         }
       | rvalue '-' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, SUB_OP, &$1, &$3);
         }
       | rvalue '*' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, MUL_OP, &$1, &$3);
         }
       | rvalue POW rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_rvalue_pow(c, &@1, &$1, &$3);
         }
       | rvalue '%' rvalue
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
       | rvalue '&' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, ANDB_OP, &$1, &$3);
         }
       | rvalue '|' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, ORB_OP, &$1, &$3);
         }
       | rvalue '^' rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, XORB_OP, &$1, &$3);
         }
       | rvalue ANDL rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, ANDL_OP, &$1, &$3);
         }
       | MIN '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, MINI_OP, &$3, &$5);
         }
       | MAX '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_op(c, &@1, MAXI_OP, &$3, &$5);
         }
       | '~' rvalue
         {
             @1.last_column = @2.last_column;
             $$ = gen_rvalue_not(c, &@1, &$2);
         }
       | '!' rvalue
         {
             @1.last_column = @2.last_column;
             $$ = gen_rvalue_notl(c, &@1, &$2);
         }
       | SAT '(' IMM ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_rvalue_sat(c, &@1, &$1, &$3, &$5);
         }
       | CAST rvalue
         {
             @1.last_column = @2.last_column;
             /* Assign target signedness */
             $2.is_unsigned = $1.is_unsigned;
             $$ = gen_cast_op(c, &@1, &$2, $1.bit_width);
             $$.is_unsigned = $1.is_unsigned;
         }
       | rvalue '[' rvalue ']'
         {
             @1.last_column = @4.last_column;
             if ($3.type == IMMEDIATE) {
                 $$ = gen_tmp(c, &@1, $1.bit_width);
                 OUT(c, &@1, "tcg_gen_extract_i", &$$.bit_width, "(");
                 OUT(c, &@1, &$$, ", ", &$1, ", ", &$3, ", 1);\n");
             } else {
                 HexValue one = gen_imm_value(c, &@1, 1, $3.bit_width);
                 HexValue tmp = gen_bin_op(c, &@1, ASR_OP, &$1, &$3);
                 $$ = gen_bin_op(c, &@1, ANDB_OP, &tmp, &one);
             }
         }
       | rvalue EQ rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_cmp(c, &@1, TCG_COND_EQ, &$1, &$3);
         }
       | rvalue NEQ rvalue
         {
             @1.last_column = @3.last_column;
             $$ = gen_bin_cmp(c, &@1, TCG_COND_NE, &$1, &$3);
         }
       | rvalue '<' rvalue
         {
             @1.last_column = @3.last_column;
             if ($1.is_unsigned || $3.is_unsigned) {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_LTU, &$1, &$3);
             } else {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_LT, &$1, &$3);
             }
         }
       | rvalue '>' rvalue
         {
             @1.last_column = @3.last_column;
             if ($1.is_unsigned || $3.is_unsigned) {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_GTU, &$1, &$3);
             } else {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_GT, &$1, &$3);
             }
         }
       | rvalue LTE rvalue
         {
             @1.last_column = @3.last_column;
             if ($1.is_unsigned || $3.is_unsigned) {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_LEU, &$1, &$3);
             } else {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_LE, &$1, &$3);
             }
         }
       | rvalue GTE rvalue
         {
             @1.last_column = @3.last_column;
             if ($1.is_unsigned || $3.is_unsigned) {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_GEU, &$1, &$3);
             } else {
                 $$ = gen_bin_cmp(c, &@1, TCG_COND_GE, &$1, &$3);
             }
         }
       | rvalue '?'
         {
             $1.is_manual = true;
             Ternary t = {0};
             t.state = IN_LEFT;
             t.cond = $1;
             g_array_append_val(c->ternary, t);
         }
         rvalue ':'
         {
             Ternary *t = &g_array_index(c->ternary, Ternary,
                                         c->ternary->len - 1);
             t->state = IN_RIGHT;
         }
         rvalue
         {
             @1.last_column = @5.last_column;
             $$ = gen_rvalue_ternary(c, &@1, &$1, &$4, &$7);
         }
       | FSCR '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_rvalue_fscr(c, &@1, &$3);
         }
       | SXT '(' rvalue ',' IMM ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             yyassert(c, &@1, $5.type == IMMEDIATE &&
                      $5.imm.type == VALUE,
                      "SXT expects immediate values\n");
             $5.imm.value = 64;
             $$ = gen_extend_op(c, &@1, &$3, &$5, &$7, false);
         }
       | ZXT '(' rvalue ',' IMM ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             yyassert(c, &@1, $5.type == IMMEDIATE &&
                      $5.imm.type == VALUE,
                      "ZXT expects immediate values\n");
             $$ = gen_extend_op(c, &@1, &$3, &$5, &$7, true);
         }
       | '(' rvalue ')'
         {
             $$ = $2;
         }
       | ABS rvalue
         {
             @1.last_column = @2.last_column;
             $$ = gen_rvalue_abs(c, &@1, &$2);
         }
       | CROUND '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_convround_n(c, &@1, &$3, &$5);
         }
       | CROUND '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_convround(c, &@1, &$3);
         }
       | ROUND '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_round(c, &@1, &$3, &$5);
         }
       | '-' rvalue
         {
             @1.last_column = @2.last_column;
             $$ = gen_rvalue_neg(c, &@1, &$2);
         }
       | ICIRC '(' rvalue ')' ASL IMM
         {
             @1.last_column = @6.last_column;
             $$ = gen_tmp(c, &@1, 32);
             OUT(c, &@1, "gen_read_ireg(", &$$, ", ", &$3, ", ", &$6, ");\n");
             rvalue_free(c, &@1, &$3);
         }
       | CIRCADD '(' rvalue ',' rvalue ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             gen_circ_op(c, &@1, &$3, &$5, &$7);
         }
       | LOCNT '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             /* Leading ones count */
             $$ = gen_locnt_op(c, &@1, &$3);
         }
       | COUNTONES '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             /* Ones count */
             $$ = gen_ctpop_op(c, &@1, &$3);
         }
       | LPCFG
         {
             $$ = gen_tmp_value(c, &@1, "0", 32);
             OUT(c, &@1, "tcg_gen_extract_tl(", &$$,
                 ", hex_gpr[HEX_REG_USR], ");
             OUT(c, &@1, "reg_field_info[USR_LPCFG].offset, ");
             OUT(c, &@1, "reg_field_info[USR_LPCFG].width);\n");
         }
       | EXTRACT '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_extract_op(c, &@1, &$5, &$3, &$1);
         }
       | EXTBITS '(' rvalue ',' rvalue ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             yyassert(c, &@1, $5.type == IMMEDIATE &&
                      $5.imm.type == VALUE &&
                      $7.type == IMMEDIATE &&
                      $7.imm.type == VALUE,
                      "Range extract needs immediate values!\n");
             $$ = gen_rextract_op(c, &@1, &$3, $7.imm.value, $5.imm.value);
         }
       | EXTRANGE '(' rvalue ',' rvalue ',' rvalue ')'
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
       | CAST4_8U '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = rvalue_truncate(c, &@1, &$3);
             $$.is_unsigned = true;
             $$ = rvalue_materialize(c, &@1, &$$);
             $$ = rvalue_extend(c, &@1, &$$);
         }
       | BREV '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_rvalue_brev(c, &@1, &$3);
         }
       | BREV_4 '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_fbrev_4(c, &@1, &$3);
         }
       | BREV_8 '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_fbrev_8(c, &@1, &$3);
         }
       | ROTL '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_rotl(c, &@1, &$3, &$5);
         }
       | SETOVF '(' ')'
         {
             @1.last_column = @3.last_column;
             HexValue ovfl = gen_imm_value(c, &@1, 1, 32);
             gen_set_overflow(c, &@1, &ovfl);
         }
       | SETOVF '(' rvalue ')'
         {
             /* Convenience fSET_OVERFLOW with pass-through */
             @1.last_column = @3.last_column;
             HexValue ovfl = gen_imm_value(c, &@1, 1, 32);
             gen_set_overflow(c, &@1, &ovfl);
             $$ = $3;
         }
       | DEINTERLEAVE '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             $$ = gen_deinterleave(c, &@1, &$3);
         }
       | INTERLEAVE '(' rvalue ',' rvalue ')'
         {
             @1.last_column = @6.last_column;
             $$ = gen_interleave(c, &@1, &$3, &$5);
         }
       | CARRY_FROM_ADD '(' rvalue ',' rvalue ',' rvalue ')'
         {
             @1.last_column = @8.last_column;
             $$ = gen_carry_from_add(c, &@1, &$3, &$5, &$7);
         }
       | LSBNEW '(' rvalue ')'
         {
             @1.last_column = @4.last_column;
             HexValue one = gen_imm_value(c, &@1, 1, 32);
             $$ = gen_bin_op(c, &@1, ANDB_OP, &$3, &one);
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
       | var
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
    context.out_str = g_string_new(NULL);
    context.signature_str = g_string_new(NULL);
    context.header_str = g_string_new(NULL);
    context.ternary = g_array_new(FALSE, TRUE, sizeof(Ternary));
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
        fprintf(stderr,
                "Warning: %d/%d meta instructions have been implemented!\n",
                context.implemented_insn,
                context.total_insn);
    }
    fputs("#endif " START_COMMENT " HEX_EMITTER_h " END_COMMENT "\n",
          defines_file);
    /* Cleanup */
    yy_delete_buffer(buffer, context.scanner);
    yylex_destroy(context.scanner);
    free(context.input_buffer);
    g_string_free(context.out_str, TRUE);
    g_string_free(context.signature_str, TRUE);
    g_string_free(context.header_str, TRUE);
    g_array_free(context.ternary, TRUE);
    fclose(output_file);
    fclose(input_file);
    fclose(defines_file);
    fclose(enabled_file);

    return 0;
}
