#define FMTI____     (0, 0, 0, )
#define FMTI__R__    (1, 1, 0, r)
#define FMTI__RR__   (2, 2, 0, rr)
#define FMTI__W__    (1, 0, 1, w)
#define FMTI__WR__   (2, 1, 1, wr)
#define FMTI__WRR__  (3, 2, 1, wrr)
#define FMTI__WRRR__ (4, 3, 1, wrrr)

#define FMTI__(prop, fmti) FMTI_ ## prop ## __ fmti

#define FMTI_ARGC__(argc, argc_rd, argc_wr, lower)    argc
#define FMTI_ARGC_RD__(argc, argc_rd, argc_wr, lower) argc_rd
#define FMTI_ARGC_WR__(argc, argc_rd, argc_wr, lower) argc_wr
#define FMTI_LOWER__(argc, argc_rd, argc_wr, lower)   lower

#define FMT_ARGC(fmt)    FMTI__(ARGC, FMTI__ ## fmt ## __)
#define FMT_ARGC_RD(fmt) FMTI__(ARGC_RD, FMTI__ ## fmt ## __)
#define FMT_ARGC_WR(fmt) FMTI__(ARGC_WR, FMTI__ ## fmt ## __)
#define FMT_LOWER(fmt)   FMTI__(LOWER, FMTI__ ## fmt ## __)
#define FMT_UPPER(fmt)   fmt

#ifndef OPCODE
#   define OPCODE(mnem, opcode, feat, fmt, ...)
#endif /* OPCODE */

#ifndef OPCODE_GRP
#   define OPCODE_GRP(grpname, opcode)
#endif /* OPCODE_GRP */

#ifndef OPCODE_GRP_BEGIN
#   define OPCODE_GRP_BEGIN(grpname)
#endif /* OPCODE_GRP_BEGIN */

#ifndef OPCODE_GRPMEMB
#   define OPCODE_GRPMEMB(grpname, mnem, opcode, feat, fmt, ...)
#endif /* OPCODE_GRPMEMB */

#ifndef OPCODE_GRP_END
#   define OPCODE_GRP_END(grpname)
#endif /* OPCODE_GRP_END */

/* NP 0F 6E /r: MOVD mm,r/m32 */
OPCODE(movd, LEG(NP, 0F, 0, 0x6e), MMX, WR, Pq, Ed)
/* NP 0F 7E /r: MOVD r/m32,mm */
OPCODE(movd, LEG(NP, 0F, 0, 0x7e), MMX, WR, Ed, Pq)
/* NP REX.W + 0F 6E /r: MOVQ mm,r/m64 */
OPCODE(movq, LEG(NP, 0F, 1, 0x6e), MMX, WR, Pq, Eq)
/* NP REX.W + 0F 7E /r: MOVQ r/m64,mm */
OPCODE(movq, LEG(NP, 0F, 1, 0x7e), MMX, WR, Eq, Pq)
/* NP 0F 6F /r: MOVQ mm, mm/m64 */
OPCODE(movq, LEG(NP, 0F, 0, 0x6f), MMX, WR, Pq, Qq)
/* NP 0F 7F /r: MOVQ mm/m64, mm */
OPCODE(movq, LEG(NP, 0F, 0, 0x7f), MMX, WR, Qq, Pq)
/* NP 0F FC /r: PADDB mm, mm/m64 */
OPCODE(paddb, LEG(NP, 0F, 0, 0xfc), MMX, WRR, Pq, Pq, Qq)
/* NP 0F FD /r: PADDW mm, mm/m64 */
OPCODE(paddw, LEG(NP, 0F, 0, 0xfd), MMX, WRR, Pq, Pq, Qq)
/* NP 0F FE /r: PADDD mm, mm/m64 */
OPCODE(paddd, LEG(NP, 0F, 0, 0xfe), MMX, WRR, Pq, Pq, Qq)
/* NP 0F EC /r: PADDSB mm, mm/m64 */
OPCODE(paddsb, LEG(NP, 0F, 0, 0xec), MMX, WRR, Pq, Pq, Qq)
/* NP 0F ED /r: PADDSW mm, mm/m64 */
OPCODE(paddsw, LEG(NP, 0F, 0, 0xed), MMX, WRR, Pq, Pq, Qq)
/* NP 0F DC /r: PADDUSB mm,mm/m64 */
OPCODE(paddusb, LEG(NP, 0F, 0, 0xdc), MMX, WRR, Pq, Pq, Qq)
/* NP 0F DD /r: PADDUSW mm,mm/m64 */
OPCODE(paddusw, LEG(NP, 0F, 0, 0xdd), MMX, WRR, Pq, Pq, Qq)
/* NP 0F F8 /r: PSUBB mm, mm/m64 */
OPCODE(psubb, LEG(NP, 0F, 0, 0xf8), MMX, WRR, Pq, Pq, Qq)
/* NP 0F F9 /r: PSUBW mm, mm/m64 */
OPCODE(psubw, LEG(NP, 0F, 0, 0xf9), MMX, WRR, Pq, Pq, Qq)
/* NP 0F FA /r: PSUBD mm, mm/m64 */
OPCODE(psubd, LEG(NP, 0F, 0, 0xfa), MMX, WRR, Pq, Pq, Qq)
/* NP 0F E8 /r: PSUBSB mm, mm/m64 */
OPCODE(psubsb, LEG(NP, 0F, 0, 0xe8), MMX, WRR, Pq, Pq, Qq)
/* NP 0F E9 /r: PSUBSW mm, mm/m64 */
OPCODE(psubsw, LEG(NP, 0F, 0, 0xe9), MMX, WRR, Pq, Pq, Qq)
/* NP 0F D8 /r: PSUBUSB mm, mm/m64 */
OPCODE(psubusb, LEG(NP, 0F, 0, 0xd8), MMX, WRR, Pq, Pq, Qq)
/* NP 0F D9 /r: PSUBUSW mm, mm/m64 */
OPCODE(psubusw, LEG(NP, 0F, 0, 0xd9), MMX, WRR, Pq, Pq, Qq)
/* NP 0F D5 /r: PMULLW mm, mm/m64 */
OPCODE(pmullw, LEG(NP, 0F, 0, 0xd5), MMX, WRR, Pq, Pq, Qq)
/* NP 0F E5 /r: PMULHW mm, mm/m64 */
OPCODE(pmulhw, LEG(NP, 0F, 0, 0xe5), MMX, WRR, Pq, Pq, Qq)
/* NP 0F F5 /r: PMADDWD mm, mm/m64 */
OPCODE(pmaddwd, LEG(NP, 0F, 0, 0xf5), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 74 /r: PCMPEQB mm,mm/m64 */
OPCODE(pcmpeqb, LEG(NP, 0F, 0, 0x74), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 75 /r: PCMPEQW mm,mm/m64 */
OPCODE(pcmpeqw, LEG(NP, 0F, 0, 0x75), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 76 /r: PCMPEQD mm,mm/m64 */
OPCODE(pcmpeqd, LEG(NP, 0F, 0, 0x76), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 64 /r: PCMPGTB mm,mm/m64 */
OPCODE(pcmpgtb, LEG(NP, 0F, 0, 0x64), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 65 /r: PCMPGTW mm,mm/m64 */
OPCODE(pcmpgtw, LEG(NP, 0F, 0, 0x65), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 66 /r: PCMPGTD mm,mm/m64 */
OPCODE(pcmpgtd, LEG(NP, 0F, 0, 0x66), MMX, WRR, Pq, Pq, Qq)
/* NP 0F DB /r: PAND mm, mm/m64 */
OPCODE(pand, LEG(NP, 0F, 0, 0xdb), MMX, WRR, Pq, Pq, Qq)
/* NP 0F DF /r: PANDN mm, mm/m64 */
OPCODE(pandn, LEG(NP, 0F, 0, 0xdf), MMX, WRR, Pq, Pq, Qq)
/* NP 0F EB /r: POR mm, mm/m64 */
OPCODE(por, LEG(NP, 0F, 0, 0xeb), MMX, WRR, Pq, Pq, Qq)
/* NP 0F EF /r: PXOR mm, mm/m64 */
OPCODE(pxor, LEG(NP, 0F, 0, 0xef), MMX, WRR, Pq, Pq, Qq)
/* NP 0F F1 /r: PSLLW mm, mm/m64 */
OPCODE(psllw, LEG(NP, 0F, 0, 0xf1), MMX, WRR, Pq, Pq, Qq)
/* NP 0F F2 /r: PSLLD mm, mm/m64 */
OPCODE(pslld, LEG(NP, 0F, 0, 0xf2), MMX, WRR, Pq, Pq, Qq)
/* NP 0F F3 /r: PSLLQ mm, mm/m64 */
OPCODE(psllq, LEG(NP, 0F, 0, 0xf3), MMX, WRR, Pq, Pq, Qq)
/* NP 0F D1 /r: PSRLW mm, mm/m64 */
OPCODE(psrlw, LEG(NP, 0F, 0, 0xd1), MMX, WRR, Pq, Pq, Qq)
/* NP 0F D2 /r: PSRLD mm, mm/m64 */
OPCODE(psrld, LEG(NP, 0F, 0, 0xd2), MMX, WRR, Pq, Pq, Qq)
/* NP 0F D3 /r: PSRLQ mm, mm/m64 */
OPCODE(psrlq, LEG(NP, 0F, 0, 0xd3), MMX, WRR, Pq, Pq, Qq)
/* NP 0F E1 /r: PSRAW mm,mm/m64 */
OPCODE(psraw, LEG(NP, 0F, 0, 0xe1), MMX, WRR, Pq, Pq, Qq)
/* NP 0F E2 /r: PSRAD mm,mm/m64 */
OPCODE(psrad, LEG(NP, 0F, 0, 0xe2), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 63 /r: PACKSSWB mm1, mm2/m64 */
OPCODE(packsswb, LEG(NP, 0F, 0, 0x63), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 6B /r: PACKSSDW mm1, mm2/m64 */
OPCODE(packssdw, LEG(NP, 0F, 0, 0x6b), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 67 /r: PACKUSWB mm, mm/m64 */
OPCODE(packuswb, LEG(NP, 0F, 0, 0x67), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 68 /r: PUNPCKHBW mm, mm/m64 */
OPCODE(punpckhbw, LEG(NP, 0F, 0, 0x68), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 69 /r: PUNPCKHWD mm, mm/m64 */
OPCODE(punpckhwd, LEG(NP, 0F, 0, 0x69), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 6A /r: PUNPCKHDQ mm, mm/m64 */
OPCODE(punpckhdq, LEG(NP, 0F, 0, 0x6a), MMX, WRR, Pq, Pq, Qq)
/* NP 0F 60 /r: PUNPCKLBW mm, mm/m32 */
OPCODE(punpcklbw, LEG(NP, 0F, 0, 0x60), MMX, WRR, Pq, Pq, Qd)
/* NP 0F 61 /r: PUNPCKLWD mm, mm/m32 */
OPCODE(punpcklwd, LEG(NP, 0F, 0, 0x61), MMX, WRR, Pq, Pq, Qd)
/* NP 0F 62 /r: PUNPCKLDQ mm, mm/m32 */
OPCODE(punpckldq, LEG(NP, 0F, 0, 0x62), MMX, WRR, Pq, Pq, Qd)
/* NP 0F 77: EMMS */
OPCODE(emms, LEG(NP, 0F, 0, 0x77), MMX, )

OPCODE_GRP(grp12_LEG_NP, LEG(NP, 0F, 0, 0x71))
OPCODE_GRP_BEGIN(grp12_LEG_NP)
    /* NP 0F 71 /6 ib: PSLLW mm1, imm8 */
    OPCODE_GRPMEMB(grp12_LEG_NP, psllw, 6, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 71 /2 ib: PSRLW mm, imm8 */
    OPCODE_GRPMEMB(grp12_LEG_NP, psrlw, 2, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 71 /4 ib: PSRAW mm,imm8 */
    OPCODE_GRPMEMB(grp12_LEG_NP, psraw, 4, MMX, WRR, Nq, Nq, Ib)
OPCODE_GRP_END(grp12_LEG_NP)

OPCODE_GRP(grp13_LEG_NP, LEG(NP, 0F, 0, 0x72))
OPCODE_GRP_BEGIN(grp13_LEG_NP)
    /* NP 0F 72 /6 ib: PSLLD mm, imm8 */
    OPCODE_GRPMEMB(grp13_LEG_NP, pslld, 6, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 72 /2 ib: PSRLD mm, imm8 */
    OPCODE_GRPMEMB(grp13_LEG_NP, psrld, 2, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 72 /4 ib: PSRAD mm,imm8 */
    OPCODE_GRPMEMB(grp13_LEG_NP, psrad, 4, MMX, WRR, Nq, Nq, Ib)
OPCODE_GRP_END(grp13_LEG_NP)

OPCODE_GRP(grp14_LEG_NP, LEG(NP, 0F, 0, 0x73))
OPCODE_GRP_BEGIN(grp14_LEG_NP)
    /* NP 0F 73 /6 ib: PSLLQ mm, imm8 */
    OPCODE_GRPMEMB(grp14_LEG_NP, psllq, 6, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 73 /2 ib: PSRLQ mm, imm8 */
    OPCODE_GRPMEMB(grp14_LEG_NP, psrlq, 2, MMX, WRR, Nq, Nq, Ib)
OPCODE_GRP_END(grp14_LEG_NP)

#undef FMTI____
#undef FMTI__R__
#undef FMTI__RR__
#undef FMTI__W__
#undef FMTI__WR__
#undef FMTI__WRR__
#undef FMTI__WRRR__

#undef FMTI__

#undef FMTI_ARGC__
#undef FMTI_ARGC_RD__
#undef FMTI_ARGC_WR__
#undef FMTI_LOWER__

#undef FMT_ARGC
#undef FMT_ARGC_RD
#undef FMT_ARGC_WR
#undef FMT_LOWER
#undef FMT_UPPER

#undef LEG
#undef VEX
#undef OPCODE
#undef OPCODE_GRP
#undef OPCODE_GRP_BEGIN
#undef OPCODE_GRPMEMB
#undef OPCODE_GRP_END
