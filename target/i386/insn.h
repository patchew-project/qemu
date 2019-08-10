#ifndef INSN
#   define INSN(mnem, prefix, opcode, feat)
#endif /* INSN */

#ifndef INSN_R
#   define INSN_R(mnem, prefix, opcode, feat, opR1)
#endif /* INSN_R */

#ifndef INSN_RR
#   define INSN_RR(mnem, prefix, opcode, feat, opR1, opR2)
#endif /* INSN_RR */

#ifndef INSN_W
#   define INSN_W(mnem, prefix, opcode, feat, opW1)
#endif /* INSN_W */

#ifndef INSN_WR
#   define INSN_WR(mnem, prefix, opcode, feat, opW1, opR1)
#endif /* INSN_WR */

#ifndef INSN_WRR
#   define INSN_WRR(mnem, prefix, opcode, feat, opW1, opR1, opR2)
#endif /* INSN_WRR */

#ifndef INSN_WRRR
#   define INSN_WRRR(mnem, prefix, opcode, feat, opW1, opR1, opR2, opR3)
#endif /* INSN_WRRR */

#ifndef INSN_GRP
#   define INSN_GRP(grpname, prefix, opcode)
#endif /* INSN_GRP */

#ifndef INSN_GRP_BEGIN
#   define INSN_GRP_BEGIN(grpname)
#endif /* INSN_GRP_BEGIN */

#ifndef INSN_GRPMEMB
#   define INSN_GRPMEMB(grpname, mnem, opcode, feat)
#endif /* INSN_GRPMEMB */

#ifndef INSN_GRPMEMB_R
#   define INSN_GRPMEMB_R(grpname, mnem, opcode, feat, opR1)
#endif /* INSN_GRPMEMB_R */

#ifndef INSN_GRPMEMB_RR
#   define INSN_GRPMEMB_RR(grpname, mnem, opcode, feat, opR1, opR2)
#endif /* INSN_GRPMEMB_RR */

#ifndef INSN_GRPMEMB_W
#   define INSN_GRPMEMB_W(grpname, mnem, opcode, feat, opW1)
#endif /* INSN_GRPMEMB_W */

#ifndef INSN_GRPMEMB_WR
#   define INSN_GRPMEMB_WR(grpname, mnem, opcode, feat, opW1, opR1)
#endif /* INSN_GRPMEMB_WR */

#ifndef INSN_GRPMEMB_WRR
#   define INSN_GRPMEMB_WRR(grpname, mnem, opcode, feat, opW1, opR1, opR2)
#endif /* INSN_GRPMEMB_WRR */

#ifndef INSN_GRPMEMB_WRRR
#   define INSN_GRPMEMB_WRRR(grpname, mnem, opcode, feat, opW1, opR1, opR2, opR3)
#endif /* INSN_GRPMEMB_WRRR */

#ifndef INSN_GRP_END
#   define INSN_GRP_END(grpname)
#endif /* INSN_GRP_END */

/* NP 0F 6E /r: MOVD mm,r/m32 */
INSN_WR(movd, LEG(NP, 0F, 0), 0x6e, MMX, Pq, Ed)
/* NP 0F 7E /r: MOVD r/m32,mm */
INSN_WR(movd, LEG(NP, 0F, 0), 0x7e, MMX, Ed, Pq)
/* NP REX.W + 0F 6E /r: MOVQ mm,r/m64 */
INSN_WR(movq, LEG(NP, 0F, 1), 0x6e, MMX, Pq, Eq)
/* NP REX.W + 0F 7E /r: MOVQ r/m64,mm */
INSN_WR(movq, LEG(NP, 0F, 1), 0x7e, MMX, Eq, Pq)
/* NP 0F 6F /r: MOVQ mm, mm/m64 */
INSN_WR(movq, LEG(NP, 0F, 0), 0x6f, MMX, Pq, Qq)
/* NP 0F 7F /r: MOVQ mm/m64, mm */
INSN_WR(movq, LEG(NP, 0F, 0), 0x7f, MMX, Qq, Pq)
/* NP 0F FC /r: PADDB mm, mm/m64 */
INSN_WRR(paddb, LEG(NP, 0F, 0), 0xfc, MMX, Pq, Pq, Qq)
/* NP 0F FD /r: PADDW mm, mm/m64 */
INSN_WRR(paddw, LEG(NP, 0F, 0), 0xfd, MMX, Pq, Pq, Qq)
/* NP 0F FE /r: PADDD mm, mm/m64 */
INSN_WRR(paddd, LEG(NP, 0F, 0), 0xfe, MMX, Pq, Pq, Qq)
/* NP 0F EC /r: PADDSB mm, mm/m64 */
INSN_WRR(paddsb, LEG(NP, 0F, 0), 0xec, MMX, Pq, Pq, Qq)
/* NP 0F ED /r: PADDSW mm, mm/m64 */
INSN_WRR(paddsw, LEG(NP, 0F, 0), 0xed, MMX, Pq, Pq, Qq)
/* NP 0F DC /r: PADDUSB mm,mm/m64 */
INSN_WRR(paddusb, LEG(NP, 0F, 0), 0xdc, MMX, Pq, Pq, Qq)
/* NP 0F DD /r: PADDUSW mm,mm/m64 */
INSN_WRR(paddusw, LEG(NP, 0F, 0), 0xdd, MMX, Pq, Pq, Qq)
/* NP 0F F8 /r: PSUBB mm, mm/m64 */
INSN_WRR(psubb, LEG(NP, 0F, 0), 0xf8, MMX, Pq, Pq, Qq)
/* NP 0F F9 /r: PSUBW mm, mm/m64 */
INSN_WRR(psubw, LEG(NP, 0F, 0), 0xf9, MMX, Pq, Pq, Qq)
/* NP 0F FA /r: PSUBD mm, mm/m64 */
INSN_WRR(psubd, LEG(NP, 0F, 0), 0xfa, MMX, Pq, Pq, Qq)
/* NP 0F E8 /r: PSUBSB mm, mm/m64 */
INSN_WRR(psubsb, LEG(NP, 0F, 0), 0xe8, MMX, Pq, Pq, Qq)
/* NP 0F E9 /r: PSUBSW mm, mm/m64 */
INSN_WRR(psubsw, LEG(NP, 0F, 0), 0xe9, MMX, Pq, Pq, Qq)
/* NP 0F D8 /r: PSUBUSB mm, mm/m64 */
INSN_WRR(psubusb, LEG(NP, 0F, 0), 0xd8, MMX, Pq, Pq, Qq)
/* NP 0F D9 /r: PSUBUSW mm, mm/m64 */
INSN_WRR(psubusw, LEG(NP, 0F, 0), 0xd9, MMX, Pq, Pq, Qq)
/* NP 0F D5 /r: PMULLW mm, mm/m64 */
INSN_WRR(pmullw, LEG(NP, 0F, 0), 0xd5, MMX, Pq, Pq, Qq)
/* NP 0F E5 /r: PMULHW mm, mm/m64 */
INSN_WRR(pmulhw, LEG(NP, 0F, 0), 0xe5, MMX, Pq, Pq, Qq)
/* NP 0F F5 /r: PMADDWD mm, mm/m64 */
INSN_WRR(pmaddwd, LEG(NP, 0F, 0), 0xf5, MMX, Pq, Pq, Qq)
/* NP 0F 74 /r: PCMPEQB mm,mm/m64 */
INSN_WRR(pcmpeqb, LEG(NP, 0F, 0), 0x74, MMX, Pq, Pq, Qq)
/* NP 0F 75 /r: PCMPEQW mm,mm/m64 */
INSN_WRR(pcmpeqw, LEG(NP, 0F, 0), 0x75, MMX, Pq, Pq, Qq)
/* NP 0F 76 /r: PCMPEQD mm,mm/m64 */
INSN_WRR(pcmpeqd, LEG(NP, 0F, 0), 0x76, MMX, Pq, Pq, Qq)
/* NP 0F 64 /r: PCMPGTB mm,mm/m64 */
INSN_WRR(pcmpgtb, LEG(NP, 0F, 0), 0x64, MMX, Pq, Pq, Qq)
/* NP 0F 65 /r: PCMPGTW mm,mm/m64 */
INSN_WRR(pcmpgtw, LEG(NP, 0F, 0), 0x65, MMX, Pq, Pq, Qq)
/* NP 0F 66 /r: PCMPGTD mm,mm/m64 */
INSN_WRR(pcmpgtd, LEG(NP, 0F, 0), 0x66, MMX, Pq, Pq, Qq)
/* NP 0F DB /r: PAND mm, mm/m64 */
INSN_WRR(pand, LEG(NP, 0F, 0), 0xdb, MMX, Pq, Pq, Qq)
/* NP 0F DF /r: PANDN mm, mm/m64 */
INSN_WRR(pandn, LEG(NP, 0F, 0), 0xdf, MMX, Pq, Pq, Qq)
/* NP 0F EB /r: POR mm, mm/m64 */
INSN_WRR(por, LEG(NP, 0F, 0), 0xeb, MMX, Pq, Pq, Qq)
/* NP 0F EF /r: PXOR mm, mm/m64 */
INSN_WRR(pxor, LEG(NP, 0F, 0), 0xef, MMX, Pq, Pq, Qq)
/* NP 0F F1 /r: PSLLW mm, mm/m64 */
INSN_WRR(psllw, LEG(NP, 0F, 0), 0xf1, MMX, Pq, Pq, Qq)
/* NP 0F F2 /r: PSLLD mm, mm/m64 */
INSN_WRR(pslld, LEG(NP, 0F, 0), 0xf2, MMX, Pq, Pq, Qq)
/* NP 0F F3 /r: PSLLQ mm, mm/m64 */
INSN_WRR(psllq, LEG(NP, 0F, 0), 0xf3, MMX, Pq, Pq, Qq)
/* NP 0F D1 /r: PSRLW mm, mm/m64 */
INSN_WRR(psrlw, LEG(NP, 0F, 0), 0xd1, MMX, Pq, Pq, Qq)
/* NP 0F D2 /r: PSRLD mm, mm/m64 */
INSN_WRR(psrld, LEG(NP, 0F, 0), 0xd2, MMX, Pq, Pq, Qq)
/* NP 0F D3 /r: PSRLQ mm, mm/m64 */
INSN_WRR(psrlq, LEG(NP, 0F, 0), 0xd3, MMX, Pq, Pq, Qq)
/* NP 0F E1 /r: PSRAW mm,mm/m64 */
INSN_WRR(psraw, LEG(NP, 0F, 0), 0xe1, MMX, Pq, Pq, Qq)
/* NP 0F E2 /r: PSRAD mm,mm/m64 */
INSN_WRR(psrad, LEG(NP, 0F, 0), 0xe2, MMX, Pq, Pq, Qq)
/* NP 0F 63 /r: PACKSSWB mm1, mm2/m64 */
INSN_WRR(packsswb, LEG(NP, 0F, 0), 0x63, MMX, Pq, Pq, Qq)
/* NP 0F 6B /r: PACKSSDW mm1, mm2/m64 */
INSN_WRR(packssdw, LEG(NP, 0F, 0), 0x6b, MMX, Pq, Pq, Qq)
/* NP 0F 67 /r: PACKUSWB mm, mm/m64 */
INSN_WRR(packuswb, LEG(NP, 0F, 0), 0x67, MMX, Pq, Pq, Qq)
/* NP 0F 68 /r: PUNPCKHBW mm, mm/m64 */
INSN_WRR(punpckhbw, LEG(NP, 0F, 0), 0x68, MMX, Pq, Pq, Qq)
/* NP 0F 69 /r: PUNPCKHWD mm, mm/m64 */
INSN_WRR(punpckhwd, LEG(NP, 0F, 0), 0x69, MMX, Pq, Pq, Qq)
/* NP 0F 6A /r: PUNPCKHDQ mm, mm/m64 */
INSN_WRR(punpckhdq, LEG(NP, 0F, 0), 0x6a, MMX, Pq, Pq, Qq)
/* NP 0F 60 /r: PUNPCKLBW mm, mm/m32 */
INSN_WRR(punpcklbw, LEG(NP, 0F, 0), 0x60, MMX, Pq, Pq, Qd)
/* NP 0F 61 /r: PUNPCKLWD mm, mm/m32 */
INSN_WRR(punpcklwd, LEG(NP, 0F, 0), 0x61, MMX, Pq, Pq, Qd)
/* NP 0F 62 /r: PUNPCKLDQ mm, mm/m32 */
INSN_WRR(punpckldq, LEG(NP, 0F, 0), 0x62, MMX, Pq, Pq, Qd)
/* NP 0F 77: EMMS */
INSN(emms, LEG(NP, 0F, 0), 0x77, MMX)

INSN_GRP(grp12_LEG_NP, LEG(NP, 0F, 0), 0x71)
INSN_GRP_BEGIN(grp12_LEG_NP)
    /* NP 0F 71 /6 ib: PSLLW mm1, imm8 */
    INSN_GRPMEMB_WRR(grp12_LEG_NP, psllw, 6, MMX, Nq, Nq, Ib)
    /* NP 0F 71 /2 ib: PSRLW mm, imm8 */
    INSN_GRPMEMB_WRR(grp12_LEG_NP, psrlw, 2, MMX, Nq, Nq, Ib)
    /* NP 0F 71 /4 ib: PSRAW mm,imm8 */
    INSN_GRPMEMB_WRR(grp12_LEG_NP, psraw, 4, MMX, Nq, Nq, Ib)
INSN_GRP_END(grp12_LEG_NP)

INSN_GRP(grp13_LEG_NP, LEG(NP, 0F, 0), 0x72)
INSN_GRP_BEGIN(grp13_LEG_NP)
    /* NP 0F 72 /6 ib: PSLLD mm, imm8 */
    INSN_GRPMEMB_WRR(grp13_LEG_NP, pslld, 6, MMX, Nq, Nq, Ib)
    /* NP 0F 72 /2 ib: PSRLD mm, imm8 */
    INSN_GRPMEMB_WRR(grp13_LEG_NP, psrld, 2, MMX, Nq, Nq, Ib)
    /* NP 0F 72 /4 ib: PSRAD mm,imm8 */
    INSN_GRPMEMB_WRR(grp13_LEG_NP, psrad, 4, MMX, Nq, Nq, Ib)
INSN_GRP_END(grp13_LEG_NP)

INSN_GRP(grp14_LEG_NP, LEG(NP, 0F, 0), 0x73)
INSN_GRP_BEGIN(grp14_LEG_NP)
    /* NP 0F 73 /6 ib: PSLLQ mm, imm8 */
    INSN_GRPMEMB_WRR(grp14_LEG_NP, psllq, 6, MMX, Nq, Nq, Ib)
    /* NP 0F 73 /2 ib: PSRLQ mm, imm8 */
    INSN_GRPMEMB_WRR(grp14_LEG_NP, psrlq, 2, MMX, Nq, Nq, Ib)
INSN_GRP_END(grp14_LEG_NP)

#undef LEG
#undef VEX
#undef INSN
#undef INSN_R
#undef INSN_RR
#undef INSN_W
#undef INSN_WR
#undef INSN_WRR
#undef INSN_WRRR
#undef INSN_GRP
#undef INSN_GRP_BEGIN
#undef INSN_GRPMEMB
#undef INSN_GRPMEMB_R
#undef INSN_GRPMEMB_RR
#undef INSN_GRPMEMB_W
#undef INSN_GRPMEMB_WR
#undef INSN_GRPMEMB_WRR
#undef INSN_GRPMEMB_WRRR
#undef INSN_GRP_END
