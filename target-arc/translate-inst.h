/*
 *  QEMU ARC CPU
 *
 *  Copyright (c); 2016 Michael Rolnik
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option); any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR dest PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see
 *  <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "translate.h"
#include "qemu/bitops.h"
#include "tcg/tcg.h"

int arc_gen_ADC(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_ADD(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_ADD1(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_ADD2(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_ADD3(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);

int arc_gen_SUB(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_SUB1(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_SUB2(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_SUB3(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_SBC(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_RSUB(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_CMP(DisasCtxt *c, TCGv src1, TCGv src2);

int arc_gen_AND(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_OR(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_BIC(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_XOR(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_TST(DisasCtxt *c, TCGv src1, TCGv src2);

int arc_gen_ASL(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_ASLm(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_ASR(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_ASRm(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_LSR(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_LSRm(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_ROR(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_RORm(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);

int arc_gen_EX(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_LD(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_LDW(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_LDB(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_ST(DisasCtxt *c, TCGv src1, TCGv src2, TCGv src3);
int arc_gen_STW(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_STB(DisasCtxt *ctx, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_PREFETCH(DisasCtxt *c, TCGv src1, TCGv src2);
int arc_gen_SYNC(DisasCtxt *c);

int arc_gen_MAX(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_MIN(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);

int arc_gen_MOV(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_EXTB(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_EXTW(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_SEXB(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_SEXW(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_SWAP(DisasCtxt *c, TCGv dest, TCGv src1);

int arc_gen_NEG(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_ABS(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_NOT(DisasCtxt *c, TCGv dest, TCGv src1);

int arc_gen_POP(DisasCtxt *c, TCGv src1);
int arc_gen_PUSH(DisasCtxt *c, TCGv src1);

int arc_gen_BCLR(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_BMSK(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_BSET(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);
int arc_gen_BTST(DisasCtxt *c, TCGv src1, TCGv src2);
int arc_gen_BXOR(DisasCtxt *c, TCGv dest, TCGv src1, TCGv src2);

int arc_gen_RRC(DisasCtxt *c, TCGv dest, TCGv src1);
int arc_gen_RLC(DisasCtxt *c, TCGv dest, TCGv src1);
