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

