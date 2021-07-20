%option noyywrap noinput nounput
%option 8bit reentrant bison-bridge
%option warn nodefault
%option bison-locations

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

#include <string.h>
#include <stdbool.h>

#include "hex_regs.h"

#include "idef-parser.h"
#include "idef-parser.tab.h"

/* Keep track of scanner position for error message printout */
#define YY_USER_ACTION yylloc->first_column = yylloc->last_column; \
    for (int i = 0; yytext[i] != '\0'; i++) {   \
        yylloc->last_column++;                  \
    }

/* Global Error Counter */
int error_count;

%}

/* Definitions */
DIGIT                    [0-9]
LOWER_ID                 [a-z]
UPPER_ID                 [A-Z]
ID                       LOWER_ID|UPPER_ID
INST_NAME                [A-Z]+[0-9]_([A-Za-z]|[0-9]|_)+
HEX_DIGIT                [0-9a-fA-F]
REG_ID_32                e|s|d|t|u|v|x|y
REG_ID_64                ee|ss|dd|tt|uu|vv|xx|yy
SYS_ID_32                s|d
SYS_ID_64                ss|dd
LOWER_PRE                d|s|t|u|v|e|x|x
IMM_ID                   r|s|S|u|U
VAR_ID                   [a-zA-Z_][a-zA-Z0-9_]*
SIGN_ID                  s|u

/* Tokens */
%%

[ \t\f\v]+                { /* Ignore whitespaces. */ }
[\n\r]+                   { /* Ignore newlines. */ }
^#.*$                     { /* Ignore linemarkers. */ }

{INST_NAME}               { yylval->string = g_string_new(yytext);
                            return INAME; }
"fFLOAT"                 |
"fUNFLOAT"               |
"fDOUBLE"                |
"fUNDOUBLE"              |
"0.0"                    |
"0x1.0p52"               |
"0x1.0p-52"              { return FAIL; }
"R"{REG_ID_32}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"R"{REG_ID_32}"N" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = DOTNEW;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = true;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"R"{REG_ID_64}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 64;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"R"{REG_ID_64}"N" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = DOTNEW;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 64;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.is_dotnew = true;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"MuV" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = MODIFIER;
                           yylval->rvalue.reg.id = 'u';
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"C"{REG_ID_32}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
"C"{REG_ID_64}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 64;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return REG; }
{IMM_ID}"iV" {
                           yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.signedness = SIGNED;
                           yylval->rvalue.imm.type = VARIABLE;
                           yylval->rvalue.imm.id = yytext[0];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           return IMM; }
"P"{LOWER_PRE}"V" {
                           yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pre.id = yytext[1];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return PRE; }
"P"{LOWER_PRE}"N" {
                           yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pre.id = yytext[1];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = true;
                           yylval->rvalue.signedness = SIGNED;
                           return PRE; }
"in R"{REG_ID_32}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = yytext[4];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return RREG; }
"in R"{REG_ID_64}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = yytext[4];
                           yylval->rvalue.reg.bit_width = 64;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return RREG; }
"in N"{REG_ID_32}"N" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = DOTNEW;
                           yylval->rvalue.reg.id = yytext[4];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = true;
                           yylval->rvalue.signedness = SIGNED;
                           return RREG; }
"in N"{REG_ID_64}"N" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = DOTNEW;
                           yylval->rvalue.reg.id = yytext[4];
                           yylval->rvalue.reg.bit_width = 64;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.is_dotnew = true;
                           yylval->rvalue.signedness = SIGNED;
                           return RREG; }
"in P"{LOWER_PRE}"V" {
                           yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pre.id = yytext[4];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return RPRE; }
"in P"{LOWER_PRE}"N" {
                           yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pre.id = yytext[4];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = true;
                           yylval->rvalue.signedness = SIGNED;
                           return RPRE; }
"in MuV" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = MODIFIER;
                           yylval->rvalue.reg.id = 'u';
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = SIGNED;
                           return RREG; }
"in C"{REG_ID_32}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = yytext[4];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return RREG; }
"in C"{REG_ID_64}"V" {
                           yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = yytext[4];
                           yylval->rvalue.reg.bit_width = 64;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.is_dotnew = false;
                           yylval->rvalue.signedness = SIGNED;
                           return RREG; }
"fGEN_TCG_"{INST_NAME}"(" { return FWRAP; }
"IV1DEAD()"              |
"fPAUSE(uiV);"           { return ';'; }
"+="                     { return INC; }
"-="                     { return DEC; }
"++"                     { return PLUSPLUS; }
"&="                     { return ANDA; }
"|="                     { return ORA; }
"^="                     { return XORA; }
"<<"                     { return ASL; }
">>"                     { return ASR; }
">>>"                    { return LSR; }
"=="                     { return EQ; }
"!="                     { return NEQ; }
"<="                     { return LTE; }
">="                     { return GTE; }
"&&"                     { return ANDL; }
"||"                     { return ORL; }
"else"                   { return ELSE; }
"for"                    { return FOR; }
"fREAD_IREG"             { return ICIRC; }
"fPART1"                 { return PART1; }
"if"                     { return IF; }
"fFRAME_SCRAMBLE"        { return FSCR; }
"fFRAME_UNSCRAMBLE"      { return FSCR; }
"fFRAMECHECK"            { return FCHK; }
"Constant_extended"      { return CONSTEXT; }
"fCL1_"{DIGIT}           { return LOCNT; }
"fBREV_8"                { return BREV_8; }
"fBREV_4"                { return BREV_4; }
"fbrev"                  { return BREV; }
"fSXTN"                  { return SXT; }
"fZXTN"                  { return ZXT; }
"fDF_MAX"                |
"fSF_MAX"                |
"fMAX"                   { return MAX; }
"fDF_MIN"                |
"fSF_MIN"                |
"fMIN"                   { return MIN; }
"fABS"                   { return ABS; }
"fRNDN"                  { return ROUND; }
"fCRND"                  { return CROUND; }
"fCRNDN"                 { return CROUND; }
"fPM_CIRI"               { return CIRCADD; }
"fPM_CIRR"               { return CIRCADD; }
"fCOUNTONES_"{DIGIT}     { return COUNTONES; }
"fSATN"                  { yylval->sat.set_overflow = true;
                           yylval->sat.signedness = SIGNED;
                           return SAT; }
"fVSATN"                 { yylval->sat.set_overflow = false;
                           yylval->sat.signedness = SIGNED;
                           return SAT; }
"fSATUN"                 { yylval->sat.set_overflow = false;
                           yylval->sat.signedness = UNSIGNED;
                           return SAT; }
"fSE32_64"               { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"fCAST4_4u"              { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fCAST4_8s"              { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"fCAST4_8u"              { return CAST4_8U; }
"fCAST4u"                { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fNEWREG"                |
"fCAST4s"                { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"fCAST8_8u"              { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fCAST8u"                { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fCAST8s"                { yylval->cast.bit_width = 64;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"fGETBIT"                { yylval->extract.bit_width = 1;
                           yylval->extract.storage_bit_width = 1;
                           yylval->extract.signedness = UNSIGNED;
                           return EXTRACT; }
"fGETBYTE"               { yylval->extract.bit_width = 8;
                           yylval->extract.storage_bit_width = 8;
                           yylval->extract.signedness = SIGNED;
                           return EXTRACT; }
"fGETUBYTE"              { yylval->extract.bit_width = 8;
                           yylval->extract.storage_bit_width = 8;
                           yylval->extract.signedness = UNSIGNED;
                           return EXTRACT; }
"fGETHALF"               { yylval->extract.bit_width = 16;
                           yylval->extract.storage_bit_width = 16;
                           yylval->extract.signedness = SIGNED;
                           return EXTRACT; }
"fGETUHALF"              { yylval->extract.bit_width = 16;
                           yylval->extract.storage_bit_width = 16;
                           yylval->extract.signedness = UNSIGNED;
                           return EXTRACT; }
"fGETWORD"               { yylval->extract.bit_width = 32;
                           yylval->extract.storage_bit_width = 64;
                           yylval->extract.signedness = SIGNED;
                           return EXTRACT; }
"fGETUWORD"              { yylval->extract.bit_width = 32;
                           yylval->extract.storage_bit_width = 64;
                           yylval->extract.signedness = UNSIGNED;
                           return EXTRACT; }
"fEXTRACTU_BITS"         { return EXTBITS; }
"fEXTRACTU_RANGE"        { return EXTRANGE; }
"fSETBIT"                { yylval->cast.bit_width = 1;
                           yylval->cast.signedness = SIGNED;
                           return DEPOSIT; }
"fSETBYTE"               { yylval->cast.bit_width = 8;
                           yylval->cast.signedness = SIGNED;
                           return DEPOSIT; }
"fSETHALF"               { yylval->cast.bit_width = 16;
                           yylval->cast.signedness = SIGNED;
                           return SETHALF; }
"fSETWORD"               { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = SIGNED;
                           return DEPOSIT; }
"fINSERT_BITS"           { return INSBITS; }
"fSETBITS"               { return SETBITS; }
"fMPY8UU"                { yylval->mpy.first_bit_width = 8;
                           yylval->mpy.second_bit_width = 8;
                           yylval->mpy.first_signedness = UNSIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fMPY8US"                { yylval->mpy.first_bit_width = 8;
                           yylval->mpy.second_bit_width = 8;
                           yylval->mpy.first_signedness = UNSIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY8SU"                { yylval->mpy.first_bit_width = 8;
                           yylval->mpy.second_bit_width = 8;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fMPY8SS"                { yylval->mpy.first_bit_width = 8;
                           yylval->mpy.second_bit_width = 8;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY16UU"               { yylval->mpy.first_bit_width = 16;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = UNSIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fMPY16US"               { yylval->mpy.first_bit_width = 16;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = UNSIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY16SU"               { yylval->mpy.first_bit_width = 16;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fMPY16SS"               { yylval->mpy.first_bit_width = 16;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY32UU"               { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 32;
                           yylval->mpy.first_signedness = UNSIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fMPY32US"               { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 32;
                           yylval->mpy.first_signedness = UNSIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY32SU"               { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 32;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fSFMPY"                 |
"fMPY32SS"               { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 32;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY3216SS"             { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = SIGNED;
                           return MPY; }
"fMPY3216SU"             { yylval->mpy.first_bit_width = 32;
                           yylval->mpy.second_bit_width = 16;
                           yylval->mpy.first_signedness = SIGNED;
                           yylval->mpy.second_signedness = UNSIGNED;
                           return MPY; }
"fNEWREG_ST"             |
"fIMMEXT"                |
"fMUST_IMMEXT"           |
"fCAST2_2s"              |
"fCAST2_2u"              |
"fCAST4_4s"              |
"fCAST8_8s"              |
"fZE8_16"                |
"fSE8_16"                |
"fZE16_32"               |
"fSE16_32"               |
"fZE32_64"               |
"fPASS"                  |
"fECHO"                  { return IDENTITY; }
"(size8"[us]"_t)"        { yylval->cast.bit_width = 64;
                           if (yytext[6] == 'u') {
                               yylval->cast.signedness = UNSIGNED;
                           } else {
                               yylval->cast.signedness = SIGNED;
                           }
                           return CAST; }
"(int)"                  { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = SIGNED;
                           return CAST; }
"(unsigned int)"         { yylval->cast.bit_width = 32;
                           yylval->cast.signedness = UNSIGNED;
                           return CAST; }
"fREAD_PC()"             |
"PC"                     { return PC; }
"fREAD_NPC()"            |
"NPC"                    { return NPC; }
"fGET_LPCFG"             |
"USR.LPCFG"              { return LPCFG; }
"LOAD_CANCEL(EA)"        |
"STORE_CANCEL(EA)"       |
"CANCEL"                 { return CANCEL; }
"N"{LOWER_ID}            { yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"N"{LOWER_ID}"N"         { yylval->rvalue.type = REGISTER_ARG;
                           yylval->rvalue.reg.type = DOTNEW;
                           yylval->rvalue.reg.id = yytext[1];
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_SP()"             |
"SP"                     { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = HEX_REG_SP;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_FP()"             |
"FP"                     { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = HEX_REG_FP;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_LR()"             |
"LR"                     { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = GENERAL_PURPOSE;
                           yylval->rvalue.reg.id = HEX_REG_LR;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_GP()"             |
"GP"                     { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = HEX_REG_GP;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_LC"[01]           { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = HEX_REG_LC0
                                                 + (yytext[8] - '0') * 2;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"LC"[01]                 { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = HEX_REG_LC0
                                                 + (yytext[2] - '0') * 2;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"fREAD_SA"[01]           { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = HEX_REG_SA0
                                                 + (yytext[8] - '0') * 2;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"SA"[01]                 { yylval->rvalue.type = REGISTER;
                           yylval->rvalue.reg.type = CONTROL;
                           yylval->rvalue.reg.id = HEX_REG_SA0
                                                 + (yytext[2] - '0') * 2;
                           yylval->rvalue.reg.bit_width = 32;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = UNSIGNED;
                           return REG; }
"MuN"                    { return MUN; }
"fREAD_P0()"             { yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pre.id = '0';
                           yylval->rvalue.bit_width = 32;
                           return PRE; }
[pP]{DIGIT}              { yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pre.id = yytext[1];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = false;
                           return PRE; }
[pP]{DIGIT}[nN]          { yylval->rvalue.type = PREDICATE;
                           yylval->rvalue.pre.id = yytext[1];
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.is_dotnew = true;
                           return PRE; }
"fLSBNEW"                { return LSBNEW; }
"N"                      { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.imm.type = VARIABLE;
                           yylval->rvalue.imm.id = 'N';
                           return IMM; }
"i"                      { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.imm.type = I;
                           return IMM; }
{SIGN_ID}                { if (yytext[0] == 'u') {
                               yylval->signedness = UNSIGNED;
                           } else {
                               yylval->signedness = SIGNED;
                           }
                           return SIGN;
                         }
"fSF_BIAS()"             { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = SIGNED;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = 127;
                           return IMM; }
{DIGIT}+                 { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = SIGNED;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = atoi(yytext);
                           return IMM; }
{DIGIT}+"LL"             { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.signedness = SIGNED;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = strtoll(yytext, NULL, 10);
                           return IMM; }
{DIGIT}+"ULL"            { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.signedness = UNSIGNED;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = strtoull(yytext,
                                                               NULL,
                                                               10);
                           return IMM; }
"0x"{HEX_DIGIT}+         { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = SIGNED;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = strtoul(yytext, NULL, 16);
                           return IMM; }
"0x"{HEX_DIGIT}+"LL"     { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.signedness = SIGNED;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = strtoll(yytext, NULL, 16);
                           return IMM; }
"0x"{HEX_DIGIT}+"ULL"    { yylval->rvalue.type = IMMEDIATE;
                           yylval->rvalue.bit_width = 64;
                           yylval->rvalue.signedness = UNSIGNED;
                           yylval->rvalue.imm.type = VALUE;
                           yylval->rvalue.imm.value = strtoull(yytext,
                                                               NULL,
                                                               16);
                           return IMM; }
"fCONSTLL"               { return CONSTLL; }
"fCONSTULL"              { return CONSTULL; }
"fLOAD"                  { return LOAD; }
"fSTORE"                 { return STORE; }
"fROTL"                  { return ROTL; }
"fSET_OVERFLOW"          { return SETOVF; }
"fDEINTERLEAVE"          { return DEINTERLEAVE; }
"fINTERLEAVE"            { return INTERLEAVE; }
"fCARRY_FROM_ADD"        { return CARRY_FROM_ADD; }
{VAR_ID}                 { /* Variable name, we adopt the C names convention */
                           yylval->rvalue.type = VARID;
                           yylval->rvalue.var.name = g_string_new(yytext);
                           /* Default types are int */
                           yylval->rvalue.bit_width = 32;
                           yylval->rvalue.signedness = SIGNED;
                           return VAR; }
"fHINTJR(RsV)"           { /* Emit no token */ }
.                        { return yytext[0]; }

%%
