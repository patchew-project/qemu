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
/* 66 0F 6E /r: MOVD xmm,r/m32 */
OPCODE(movd, LEG(66, 0F, 0, 0x6e), SSE2, WR, Vdq, Ed)
/* 66 0F 7E /r: MOVD r/m32,xmm */
OPCODE(movd, LEG(66, 0F, 0, 0x7e), SSE2, WR, Ed, Vdq)
/* NP REX.W + 0F 6E /r: MOVQ mm,r/m64 */
OPCODE(movq, LEG(NP, 0F, 1, 0x6e), MMX, WR, Pq, Eq)
/* NP REX.W + 0F 7E /r: MOVQ r/m64,mm */
OPCODE(movq, LEG(NP, 0F, 1, 0x7e), MMX, WR, Eq, Pq)
/* 66 REX.W 0F 6E /r: MOVQ xmm,r/m64 */
OPCODE(movq, LEG(66, 0F, 1, 0x6e), SSE2, WR, Vdq, Eq)
/* 66 REX.W 0F 7E /r: MOVQ r/m64,xmm */
OPCODE(movq, LEG(66, 0F, 1, 0x7e), SSE2, WR, Eq, Vdq)
/* NP 0F 6F /r: MOVQ mm, mm/m64 */
OPCODE(movq, LEG(NP, 0F, 0, 0x6f), MMX, WR, Pq, Qq)
/* NP 0F 7F /r: MOVQ mm/m64, mm */
OPCODE(movq, LEG(NP, 0F, 0, 0x7f), MMX, WR, Qq, Pq)
/* F3 0F 7E /r: MOVQ xmm1, xmm2/m64 */
OPCODE(movq, LEG(F3, 0F, 0, 0x7e), SSE2, WR, Vdq, Wq)
/* 66 0F D6 /r: MOVQ xmm2/m64, xmm1 */
OPCODE(movq, LEG(66, 0F, 0, 0xd6), SSE2, WR, UdqMq, Vq)
/* NP 0F 28 /r: MOVAPS xmm1, xmm2/m128 */
OPCODE(movaps, LEG(NP, 0F, 0, 0x28), SSE, WR, Vdq, Wdq)
/* NP 0F 29 /r: MOVAPS xmm2/m128, xmm1 */
OPCODE(movaps, LEG(NP, 0F, 0, 0x29), SSE, WR, Wdq, Vdq)
/* 66 0F 28 /r: MOVAPD xmm1, xmm2/m128 */
OPCODE(movapd, LEG(66, 0F, 0, 0x28), SSE2, WR, Vdq, Wdq)
/* 66 0F 29 /r: MOVAPD xmm2/m128, xmm1 */
OPCODE(movapd, LEG(66, 0F, 0, 0x29), SSE2, WR, Wdq, Vdq)
/* 66 0F 6F /r: MOVDQA xmm1, xmm2/m128 */
OPCODE(movdqa, LEG(66, 0F, 0, 0x6f), SSE2, WR, Vdq, Wdq)
/* 66 0F 7F /r: MOVDQA xmm2/m128, xmm1 */
OPCODE(movdqa, LEG(66, 0F, 0, 0x7f), SSE2, WR, Wdq, Vdq)
/* NP 0F 10 /r: MOVUPS xmm1, xmm2/m128 */
OPCODE(movups, LEG(NP, 0F, 0, 0x10), SSE, WR, Vdq, Wdq)
/* NP 0F 11 /r: MOVUPS xmm2/m128, xmm1 */
OPCODE(movups, LEG(NP, 0F, 0, 0x11), SSE, WR, Wdq, Vdq)
/* 66 0F 10 /r: MOVUPD xmm1, xmm2/m128 */
OPCODE(movupd, LEG(66, 0F, 0, 0x10), SSE2, WR, Vdq, Wdq)
/* 66 0F 11 /r: MOVUPD xmm2/m128, xmm1 */
OPCODE(movupd, LEG(66, 0F, 0, 0x11), SSE2, WR, Wdq, Vdq)
/* F3 0F 6F /r: MOVDQU xmm1,xmm2/m128 */
OPCODE(movdqu, LEG(F3, 0F, 0, 0x6f), SSE2, WR, Vdq, Wdq)
/* F3 0F 7F /r: MOVDQU xmm2/m128,xmm1 */
OPCODE(movdqu, LEG(F3, 0F, 0, 0x7f), SSE2, WR, Wdq, Vdq)
/* F3 0F 10 /r: MOVSS xmm1, xmm2/m32 */
OPCODE(movss, LEG(F3, 0F, 0, 0x10), SSE, WRRR, Vdq, Vdq, Wd, modrm_mod)
/* F3 0F 11 /r: MOVSS xmm2/m32, xmm1 */
OPCODE(movss, LEG(F3, 0F, 0, 0x11), SSE, WR, Wd, Vd)
/* F2 0F 10 /r: MOVSD xmm1, xmm2/m64 */
OPCODE(movsd, LEG(F2, 0F, 0, 0x10), SSE2, WRRR, Vdq, Vdq, Wq, modrm_mod)
/* F2 0F 11 /r: MOVSD xmm1/m64, xmm2 */
OPCODE(movsd, LEG(F2, 0F, 0, 0x11), SSE2, WR, Wq, Vq)
/* F3 0F D6 /r: MOVQ2DQ xmm, mm */
OPCODE(movq2dq, LEG(F3, 0F, 0, 0xd6), SSE2, WR, Vdq, Nq)
/* F2 0F D6 /r: MOVDQ2Q mm, xmm */
OPCODE(movdq2q, LEG(F2, 0F, 0, 0xd6), SSE2, WR, Pq, Uq)
/* NP 0F 12 /r: MOVHLPS xmm1, xmm2 */
/* NP 0F 12 /r: MOVLPS xmm1, m64 */
OPCODE(movhlps, LEG(NP, 0F, 0, 0x12), SSE, WR, Vq, UdqMhq)
/* 0F 13 /r: MOVLPS m64, xmm1 */
OPCODE(movlps, LEG(NP, 0F, 0, 0x13), SSE, WR, Mq, Vq)
/* 66 0F 12 /r: MOVLPD xmm1,m64 */
OPCODE(movlpd, LEG(66, 0F, 0, 0x12), SSE2, WR, Vq, Mq)
/* 66 0F 13 /r: MOVLPD m64,xmm1 */
OPCODE(movlpd, LEG(66, 0F, 0, 0x13), SSE2, WR, Mq, Vq)
/* NP 0F 16 /r: MOVLHPS xmm1, xmm2 */
/* NP 0F 16 /r: MOVHPS xmm1, m64 */
OPCODE(movlhps, LEG(NP, 0F, 0, 0x16), SSE, WRR, Vdq, Vq, Wq)
/* NP 0F 17 /r: MOVHPS m64, xmm1 */
OPCODE(movhps, LEG(NP, 0F, 0, 0x17), SSE, WR, Mq, Vdq)
/* 66 0F 16 /r: MOVHPD xmm1, m64 */
OPCODE(movhpd, LEG(66, 0F, 0, 0x16), SSE2, WRR, Vdq, Vd, Mq)
/* 66 0F 17 /r: MOVHPD m64, xmm1 */
OPCODE(movhpd, LEG(66, 0F, 0, 0x17), SSE2, WR, Mq, Vdq)
/* NP 0F D7 /r: PMOVMSKB r32, mm */
OPCODE(pmovmskb, LEG(NP, 0F, 0, 0xd7), SSE, WR, Gd, Nq)
/* NP REX.W 0F D7 /r: PMOVMSKB r64, mm */
OPCODE(pmovmskb, LEG(NP, 0F, 1, 0xd7), SSE, WR, Gq, Nq)
/* 66 0F D7 /r: PMOVMSKB r32, xmm */
OPCODE(pmovmskb, LEG(66, 0F, 0, 0xd7), SSE2, WR, Gd, Udq)
/* 66 REX.W 0F D7 /r: PMOVMSKB r64, xmm */
OPCODE(pmovmskb, LEG(66, 0F, 1, 0xd7), SSE2, WR, Gq, Udq)
/* NP 0F 50 /r: MOVMSKPS r32, xmm */
OPCODE(movmskps, LEG(NP, 0F, 0, 0x50), SSE, WR, Gd, Udq)
/* NP REX.W 0F 50 /r: MOVMSKPS r64, xmm */
OPCODE(movmskps, LEG(NP, 0F, 1, 0x50), SSE, WR, Gq, Udq)
/* 66 0F 50 /r: MOVMSKPD r32, xmm */
OPCODE(movmskpd, LEG(66, 0F, 0, 0x50), SSE2, WR, Gd, Udq)
/* 66 REX.W 0F 50 /r: MOVMSKPD r64, xmm */
OPCODE(movmskpd, LEG(66, 0F, 1, 0x50), SSE2, WR, Gq, Udq)
/* F2 0F F0 /r: LDDQU xmm1, m128 */
OPCODE(lddqu, LEG(F2, 0F, 0, 0xf0), SSE3, WR, Vdq, Mdq)
/* F3 0F 16 /r: MOVSHDUP xmm1, xmm2/m128 */
OPCODE(movshdup, LEG(F3, 0F, 0, 0x16), SSE3, WR, Vdq, Wdq)
/* F3 0F 12 /r: MOVSLDUP xmm1, xmm2/m128 */
OPCODE(movsldup, LEG(F3, 0F, 0, 0x12), SSE3, WR, Vdq, Wdq)
/* F2 0F 12 /r: MOVDDUP xmm1, xmm2/m64 */
OPCODE(movddup, LEG(F2, 0F, 0, 0x12), SSE3, WR, Vdq, Wq)
/* NP 0F FC /r: PADDB mm, mm/m64 */
OPCODE(paddb, LEG(NP, 0F, 0, 0xfc), MMX, WRR, Pq, Pq, Qq)
/* 66 0F FC /r: PADDB xmm1, xmm2/m128 */
OPCODE(paddb, LEG(66, 0F, 0, 0xfc), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F FD /r: PADDW mm, mm/m64 */
OPCODE(paddw, LEG(NP, 0F, 0, 0xfd), MMX, WRR, Pq, Pq, Qq)
/* 66 0F FD /r: PADDW xmm1, xmm2/m128 */
OPCODE(paddw, LEG(66, 0F, 0, 0xfd), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F FE /r: PADDD mm, mm/m64 */
OPCODE(paddd, LEG(NP, 0F, 0, 0xfe), MMX, WRR, Pq, Pq, Qq)
/* 66 0F FE /r: PADDD xmm1, xmm2/m128 */
OPCODE(paddd, LEG(66, 0F, 0, 0xfe), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F D4 /r: PADDQ mm, mm/m64 */
OPCODE(paddq, LEG(NP, 0F, 0, 0xd4), SSE2, WRR, Pq, Pq, Qq)
/* 66 0F D4 /r: PADDQ xmm1, xmm2/m128 */
OPCODE(paddq, LEG(66, 0F, 0, 0xd4), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F EC /r: PADDSB mm, mm/m64 */
OPCODE(paddsb, LEG(NP, 0F, 0, 0xec), MMX, WRR, Pq, Pq, Qq)
/* 66 0F EC /r: PADDSB xmm1, xmm2/m128 */
OPCODE(paddsb, LEG(66, 0F, 0, 0xec), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F ED /r: PADDSW mm, mm/m64 */
OPCODE(paddsw, LEG(NP, 0F, 0, 0xed), MMX, WRR, Pq, Pq, Qq)
/* 66 0F ED /r: PADDSW xmm1, xmm2/m128 */
OPCODE(paddsw, LEG(66, 0F, 0, 0xed), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F DC /r: PADDUSB mm,mm/m64 */
OPCODE(paddusb, LEG(NP, 0F, 0, 0xdc), MMX, WRR, Pq, Pq, Qq)
/* 66 0F DC /r: PADDUSB xmm1,xmm2/m128 */
OPCODE(paddusb, LEG(66, 0F, 0, 0xdc), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F DD /r: PADDUSW mm,mm/m64 */
OPCODE(paddusw, LEG(NP, 0F, 0, 0xdd), MMX, WRR, Pq, Pq, Qq)
/* 66 0F DD /r: PADDUSW xmm1,xmm2/m128 */
OPCODE(paddusw, LEG(66, 0F, 0, 0xdd), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 58 /r: ADDPS xmm1, xmm2/m128 */
OPCODE(addps, LEG(NP, 0F, 0, 0x58), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 58 /r: ADDPD xmm1, xmm2/m128 */
OPCODE(addpd, LEG(66, 0F, 0, 0x58), SSE2, WRR, Vdq, Vdq, Wdq)
/* F3 0F 58 /r: ADDSS xmm1, xmm2/m32 */
OPCODE(addss, LEG(F3, 0F, 0, 0x58), SSE, WRR, Vd, Vd, Wd)
/* F2 0F 58 /r: ADDSD xmm1, xmm2/m64 */
OPCODE(addsd, LEG(F2, 0F, 0, 0x58), SSE2, WRR, Vq, Vq, Wq)
/* F2 0F 7C /r: HADDPS xmm1, xmm2/m128 */
OPCODE(haddps, LEG(F2, 0F, 0, 0x7c), SSE3, WRR, Vdq, Vdq, Wdq)
/* 66 0F 7C /r: HADDPD xmm1, xmm2/m128 */
OPCODE(haddpd, LEG(66, 0F, 0, 0x7c), SSE3, WRR, Vdq, Vdq, Wdq)
/* NP 0F F8 /r: PSUBB mm, mm/m64 */
OPCODE(psubb, LEG(NP, 0F, 0, 0xf8), MMX, WRR, Pq, Pq, Qq)
/* 66 0F F8 /r: PSUBB xmm1, xmm2/m128 */
OPCODE(psubb, LEG(66, 0F, 0, 0xf8), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F F9 /r: PSUBW mm, mm/m64 */
OPCODE(psubw, LEG(NP, 0F, 0, 0xf9), MMX, WRR, Pq, Pq, Qq)
/* 66 0F F9 /r: PSUBW xmm1, xmm2/m128 */
OPCODE(psubw, LEG(66, 0F, 0, 0xf9), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F FA /r: PSUBD mm, mm/m64 */
OPCODE(psubd, LEG(NP, 0F, 0, 0xfa), MMX, WRR, Pq, Pq, Qq)
/* 66 0F FA /r: PSUBD xmm1, xmm2/m128 */
OPCODE(psubd, LEG(66, 0F, 0, 0xfa), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F FB /r: PSUBQ mm1, mm2/m64 */
OPCODE(psubq, LEG(NP, 0F, 0, 0xfb), SSE2, WRR, Pq, Pq, Qq)
/* 66 0F FB /r: PSUBQ xmm1, xmm2/m128 */
OPCODE(psubq, LEG(66, 0F, 0, 0xfb), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F E8 /r: PSUBSB mm, mm/m64 */
OPCODE(psubsb, LEG(NP, 0F, 0, 0xe8), MMX, WRR, Pq, Pq, Qq)
/* 66 0F E8 /r: PSUBSB xmm1, xmm2/m128 */
OPCODE(psubsb, LEG(66, 0F, 0, 0xe8), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F E9 /r: PSUBSW mm, mm/m64 */
OPCODE(psubsw, LEG(NP, 0F, 0, 0xe9), MMX, WRR, Pq, Pq, Qq)
/* 66 0F E9 /r: PSUBSW xmm1, xmm2/m128 */
OPCODE(psubsw, LEG(66, 0F, 0, 0xe9), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F D8 /r: PSUBUSB mm, mm/m64 */
OPCODE(psubusb, LEG(NP, 0F, 0, 0xd8), MMX, WRR, Pq, Pq, Qq)
/* 66 0F D8 /r: PSUBUSB xmm1, xmm2/m128 */
OPCODE(psubusb, LEG(66, 0F, 0, 0xd8), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F D9 /r: PSUBUSW mm, mm/m64 */
OPCODE(psubusw, LEG(NP, 0F, 0, 0xd9), MMX, WRR, Pq, Pq, Qq)
/* 66 0F D9 /r: PSUBUSW xmm1, xmm2/m128 */
OPCODE(psubusw, LEG(66, 0F, 0, 0xd9), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 5C /r: SUBPS xmm1, xmm2/m128 */
OPCODE(subps, LEG(NP, 0F, 0, 0x5c), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 5C /r: SUBPD xmm1, xmm2/m128 */
OPCODE(subpd, LEG(66, 0F, 0, 0x5c), SSE2, WRR, Vdq, Vdq, Wdq)
/* F3 0F 5C /r: SUBSS xmm1, xmm2/m32 */
OPCODE(subss, LEG(F3, 0F, 0, 0x5c), SSE, WRR, Vd, Vd, Wd)
/* F2 0F 5C /r: SUBSD xmm1, xmm2/m64 */
OPCODE(subsd, LEG(F2, 0F, 0, 0x5c), SSE2, WRR, Vq, Vq, Wq)
/* F2 0F 7D /r: HSUBPS xmm1, xmm2/m128 */
OPCODE(hsubps, LEG(F2, 0F, 0, 0x7d), SSE3, WRR, Vdq, Vdq, Wdq)
/* 66 0F 7D /r: HSUBPD xmm1, xmm2/m128 */
OPCODE(hsubpd, LEG(66, 0F, 0, 0x7d), SSE3, WRR, Vdq, Vdq, Wdq)
/* F2 0F D0 /r: ADDSUBPS xmm1, xmm2/m128 */
OPCODE(addsubps, LEG(F2, 0F, 0, 0xd0), SSE3, WRR, Vdq, Vdq, Wdq)
/* 66 0F D0 /r: ADDSUBPD xmm1, xmm2/m128 */
OPCODE(addsubpd, LEG(66, 0F, 0, 0xd0), SSE3, WRR, Vdq, Vdq, Wdq)
/* NP 0F D5 /r: PMULLW mm, mm/m64 */
OPCODE(pmullw, LEG(NP, 0F, 0, 0xd5), MMX, WRR, Pq, Pq, Qq)
/* 66 0F D5 /r: PMULLW xmm1, xmm2/m128 */
OPCODE(pmullw, LEG(66, 0F, 0, 0xd5), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F E5 /r: PMULHW mm, mm/m64 */
OPCODE(pmulhw, LEG(NP, 0F, 0, 0xe5), MMX, WRR, Pq, Pq, Qq)
/* 66 0F E5 /r: PMULHW xmm1, xmm2/m128 */
OPCODE(pmulhw, LEG(66, 0F, 0, 0xe5), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F E4 /r: PMULHUW mm1, mm2/m64 */
OPCODE(pmulhuw, LEG(NP, 0F, 0, 0xe4), SSE, WRR, Pq, Pq, Qq)
/* 66 0F E4 /r: PMULHUW xmm1, xmm2/m128 */
OPCODE(pmulhuw, LEG(66, 0F, 0, 0xe4), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F F4 /r: PMULUDQ mm1, mm2/m64 */
OPCODE(pmuludq, LEG(NP, 0F, 0, 0xf4), SSE2, WRR, Pq, Pq, Qq)
/* 66 0F F4 /r: PMULUDQ xmm1, xmm2/m128 */
OPCODE(pmuludq, LEG(66, 0F, 0, 0xf4), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 59 /r: MULPS xmm1, xmm2/m128 */
OPCODE(mulps, LEG(NP, 0F, 0, 0x59), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 59 /r: MULPD xmm1, xmm2/m128 */
OPCODE(mulpd, LEG(66, 0F, 0, 0x59), SSE2, WRR, Vdq, Vdq, Wdq)
/* F3 0F 59 /r: MULSS xmm1,xmm2/m32 */
OPCODE(mulss, LEG(F3, 0F, 0, 0x59), SSE, WRR, Vd, Vd, Wd)
/* F2 0F 59 /r: MULSD xmm1,xmm2/m64 */
OPCODE(mulsd, LEG(F2, 0F, 0, 0x59), SSE2, WRR, Vq, Vq, Wq)
/* NP 0F F5 /r: PMADDWD mm, mm/m64 */
OPCODE(pmaddwd, LEG(NP, 0F, 0, 0xf5), MMX, WRR, Pq, Pq, Qq)
/* 66 0F F5 /r: PMADDWD xmm1, xmm2/m128 */
OPCODE(pmaddwd, LEG(66, 0F, 0, 0xf5), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 5E /r: DIVPS xmm1, xmm2/m128 */
OPCODE(divps, LEG(NP, 0F, 0, 0x5e), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 5E /r: DIVPD xmm1, xmm2/m128 */
OPCODE(divpd, LEG(66, 0F, 0, 0x5e), SSE2, WRR, Vdq, Vdq, Wdq)
/* F3 0F 5E /r: DIVSS xmm1, xmm2/m32 */
OPCODE(divss, LEG(F3, 0F, 0, 0x5e), SSE, WRR, Vd, Vd, Wd)
/* F2 0F 5E /r: DIVSD xmm1, xmm2/m64 */
OPCODE(divsd, LEG(F2, 0F, 0, 0x5e), SSE2, WRR, Vq, Vq, Wq)
/* NP 0F 53 /r: RCPPS xmm1, xmm2/m128 */
OPCODE(rcpps, LEG(NP, 0F, 0, 0x53), SSE, WR, Vdq, Wdq)
/* F3 0F 53 /r: RCPSS xmm1, xmm2/m32 */
OPCODE(rcpss, LEG(F3, 0F, 0, 0x53), SSE, WR, Vd, Wd)
/* NP 0F 51 /r: SQRTPS xmm1, xmm2/m128 */
OPCODE(sqrtps, LEG(NP, 0F, 0, 0x51), SSE, WR, Vdq, Wdq)
/* 66 0F 51 /r: SQRTPD xmm1, xmm2/m128 */
OPCODE(sqrtpd, LEG(66, 0F, 0, 0x51), SSE2, WR, Vdq, Wdq)
/* F3 0F 51 /r: SQRTSS xmm1, xmm2/m32 */
OPCODE(sqrtss, LEG(F3, 0F, 0, 0x51), SSE, WR, Vd, Wd)
/* F2 0F 51 /r: SQRTSD xmm1,xmm2/m64 */
OPCODE(sqrtsd, LEG(F2, 0F, 0, 0x51), SSE2, WR, Vq, Wq)
/* NP 0F 52 /r: RSQRTPS xmm1, xmm2/m128 */
OPCODE(rsqrtps, LEG(NP, 0F, 0, 0x52), SSE, WR, Vdq, Wdq)
/* F3 0F 52 /r: RSQRTSS xmm1, xmm2/m32 */
OPCODE(rsqrtss, LEG(F3, 0F, 0, 0x52), SSE, WR, Vd, Wd)
/* NP 0F DA /r: PMINUB mm1, mm2/m64 */
OPCODE(pminub, LEG(NP, 0F, 0, 0xda), SSE, WRR, Pq, Pq, Qq)
/* 66 0F DA /r: PMINUB xmm1, xmm2/m128 */
OPCODE(pminub, LEG(66, 0F, 0, 0xda), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F EA /r: PMINSW mm1, mm2/m64 */
OPCODE(pminsw, LEG(NP, 0F, 0, 0xea), SSE, WRR, Pq, Pq, Qq)
/* 66 0F EA /r: PMINSW xmm1, xmm2/m128 */
OPCODE(pminsw, LEG(66, 0F, 0, 0xea), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 5D /r: MINPS xmm1, xmm2/m128 */
OPCODE(minps, LEG(NP, 0F, 0, 0x5d), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 5D /r: MINPD xmm1, xmm2/m128 */
OPCODE(minpd, LEG(66, 0F, 0, 0x5d), SSE2, WRR, Vdq, Vdq, Wdq)
/* F3 0F 5D /r: MINSS xmm1,xmm2/m32 */
OPCODE(minss, LEG(F3, 0F, 0, 0x5d), SSE, WRR, Vd, Vd, Wd)
/* F2 0F 5D /r: MINSD xmm1, xmm2/m64 */
OPCODE(minsd, LEG(F2, 0F, 0, 0x5d), SSE2, WRR, Vq, Vq, Wq)
/* NP 0F DE /r: PMAXUB mm1, mm2/m64 */
OPCODE(pmaxub, LEG(NP, 0F, 0, 0xde), SSE, WRR, Pq, Pq, Qq)
/* 66 0F DE /r: PMAXUB xmm1, xmm2/m128 */
OPCODE(pmaxub, LEG(66, 0F, 0, 0xde), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F EE /r: PMAXSW mm1, mm2/m64 */
OPCODE(pmaxsw, LEG(NP, 0F, 0, 0xee), SSE, WRR, Pq, Pq, Qq)
/* 66 0F EE /r: PMAXSW xmm1, xmm2/m128 */
OPCODE(pmaxsw, LEG(66, 0F, 0, 0xee), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 5F /r: MAXPS xmm1, xmm2/m128 */
OPCODE(maxps, LEG(NP, 0F, 0, 0x5f), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 5F /r: MAXPD xmm1, xmm2/m128 */
OPCODE(maxpd, LEG(66, 0F, 0, 0x5f), SSE2, WRR, Vdq, Vdq, Wdq)
/* F3 0F 5F /r: MAXSS xmm1, xmm2/m32 */
OPCODE(maxss, LEG(F3, 0F, 0, 0x5f), SSE, WRR, Vd, Vd, Wd)
/* F2 0F 5F /r: MAXSD xmm1, xmm2/m64 */
OPCODE(maxsd, LEG(F2, 0F, 0, 0x5f), SSE2, WRR, Vq, Vq, Wq)
/* NP 0F E0 /r: PAVGB mm1, mm2/m64 */
OPCODE(pavgb, LEG(NP, 0F, 0, 0xe0), SSE, WRR, Pq, Pq, Qq)
/* 66 0F E0 /r: PAVGB xmm1, xmm2/m128 */
OPCODE(pavgb, LEG(66, 0F, 0, 0xe0), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F E3 /r: PAVGW mm1, mm2/m64 */
OPCODE(pavgw, LEG(NP, 0F, 0, 0xe3), SSE, WRR, Pq, Pq, Qq)
/* 66 0F E3 /r: PAVGW xmm1, xmm2/m128 */
OPCODE(pavgw, LEG(66, 0F, 0, 0xe3), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F F6 /r: PSADBW mm1, mm2/m64 */
OPCODE(psadbw, LEG(NP, 0F, 0, 0xf6), SSE, WRR, Pq, Pq, Qq)
/* 66 0F F6 /r: PSADBW xmm1, xmm2/m128 */
OPCODE(psadbw, LEG(66, 0F, 0, 0xf6), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 74 /r: PCMPEQB mm,mm/m64 */
OPCODE(pcmpeqb, LEG(NP, 0F, 0, 0x74), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 74 /r: PCMPEQB xmm1,xmm2/m128 */
OPCODE(pcmpeqb, LEG(66, 0F, 0, 0x74), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 75 /r: PCMPEQW mm,mm/m64 */
OPCODE(pcmpeqw, LEG(NP, 0F, 0, 0x75), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 75 /r: PCMPEQW xmm1,xmm2/m128 */
OPCODE(pcmpeqw, LEG(66, 0F, 0, 0x75), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 76 /r: PCMPEQD mm,mm/m64 */
OPCODE(pcmpeqd, LEG(NP, 0F, 0, 0x76), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 76 /r: PCMPEQD xmm1,xmm2/m128 */
OPCODE(pcmpeqd, LEG(66, 0F, 0, 0x76), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 64 /r: PCMPGTB mm,mm/m64 */
OPCODE(pcmpgtb, LEG(NP, 0F, 0, 0x64), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 64 /r: PCMPGTB xmm1,xmm2/m128 */
OPCODE(pcmpgtb, LEG(66, 0F, 0, 0x64), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 65 /r: PCMPGTW mm,mm/m64 */
OPCODE(pcmpgtw, LEG(NP, 0F, 0, 0x65), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 65 /r: PCMPGTW xmm1,xmm2/m128 */
OPCODE(pcmpgtw, LEG(66, 0F, 0, 0x65), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 66 /r: PCMPGTD mm,mm/m64 */
OPCODE(pcmpgtd, LEG(NP, 0F, 0, 0x66), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 66 /r: PCMPGTD xmm1,xmm2/m128 */
OPCODE(pcmpgtd, LEG(66, 0F, 0, 0x66), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F C2 /r ib: CMPPS xmm1, xmm2/m128, imm8 */
OPCODE(cmpps, LEG(NP, 0F, 0, 0xc2), SSE, WRRR, Vdq, Vdq, Wdq, Ib)
/* 66 0F C2 /r ib: CMPPD xmm1, xmm2/m128, imm8 */
OPCODE(cmppd, LEG(66, 0F, 0, 0xc2), SSE2, WRRR, Vdq, Vdq, Wdq, Ib)
/* F3 0F C2 /r ib: CMPSS xmm1, xmm2/m32, imm8 */
OPCODE(cmpss, LEG(F3, 0F, 0, 0xc2), SSE, WRRR, Vd, Vd, Wd, Ib)
/* F2 0F C2 /r ib: CMPSD xmm1, xmm2/m64, imm8 */
OPCODE(cmpsd, LEG(F2, 0F, 0, 0xc2), SSE2, WRRR, Vq, Vq, Wq, Ib)
/* NP 0F 2E /r: UCOMISS xmm1, xmm2/m32 */
OPCODE(ucomiss, LEG(NP, 0F, 0, 0x2e), SSE, RR, Vd, Wd)
/* 66 0F 2E /r: UCOMISD xmm1, xmm2/m64 */
OPCODE(ucomisd, LEG(66, 0F, 0, 0x2e), SSE2, RR, Vq, Wq)
/* NP 0F 2F /r: COMISS xmm1, xmm2/m32 */
OPCODE(comiss, LEG(NP, 0F, 0, 0x2f), SSE, RR, Vd, Wd)
/* 66 0F 2F /r: COMISD xmm1, xmm2/m64 */
OPCODE(comisd, LEG(66, 0F, 0, 0x2f), SSE2, RR, Vq, Wq)
/* NP 0F DB /r: PAND mm, mm/m64 */
OPCODE(pand, LEG(NP, 0F, 0, 0xdb), MMX, WRR, Pq, Pq, Qq)
/* 66 0F DB /r: PAND xmm1, xmm2/m128 */
OPCODE(pand, LEG(66, 0F, 0, 0xdb), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 54 /r: ANDPS xmm1, xmm2/m128 */
OPCODE(andps, LEG(NP, 0F, 0, 0x54), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 54 /r: ANDPD xmm1, xmm2/m128 */
OPCODE(andpd, LEG(66, 0F, 0, 0x54), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F DF /r: PANDN mm, mm/m64 */
OPCODE(pandn, LEG(NP, 0F, 0, 0xdf), MMX, WRR, Pq, Pq, Qq)
/* 66 0F DF /r: PANDN xmm1, xmm2/m128 */
OPCODE(pandn, LEG(66, 0F, 0, 0xdf), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 55 /r: ANDNPS xmm1, xmm2/m128 */
OPCODE(andnps, LEG(NP, 0F, 0, 0x55), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 55 /r: ANDNPD xmm1, xmm2/m128 */
OPCODE(andnpd, LEG(66, 0F, 0, 0x55), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F EB /r: POR mm, mm/m64 */
OPCODE(por, LEG(NP, 0F, 0, 0xeb), MMX, WRR, Pq, Pq, Qq)
/* 66 0F EB /r: POR xmm1, xmm2/m128 */
OPCODE(por, LEG(66, 0F, 0, 0xeb), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 56 /r: ORPS xmm1, xmm2/m128 */
OPCODE(orps, LEG(NP, 0F, 0, 0x56), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 56 /r: ORPD xmm1, xmm2/m128 */
OPCODE(orpd, LEG(66, 0F, 0, 0x56), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F EF /r: PXOR mm, mm/m64 */
OPCODE(pxor, LEG(NP, 0F, 0, 0xef), MMX, WRR, Pq, Pq, Qq)
/* 66 0F EF /r: PXOR xmm1, xmm2/m128 */
OPCODE(pxor, LEG(66, 0F, 0, 0xef), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 57 /r: XORPS xmm1, xmm2/m128 */
OPCODE(xorps, LEG(NP, 0F, 0, 0x57), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 57 /r: XORPD xmm1, xmm2/m128 */
OPCODE(xorpd, LEG(66, 0F, 0, 0x57), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F F1 /r: PSLLW mm, mm/m64 */
OPCODE(psllw, LEG(NP, 0F, 0, 0xf1), MMX, WRR, Pq, Pq, Qq)
/* 66 0F F1 /r: PSLLW xmm1, xmm2/m128 */
OPCODE(psllw, LEG(66, 0F, 0, 0xf1), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F F2 /r: PSLLD mm, mm/m64 */
OPCODE(pslld, LEG(NP, 0F, 0, 0xf2), MMX, WRR, Pq, Pq, Qq)
/* 66 0F F2 /r: PSLLD xmm1, xmm2/m128 */
OPCODE(pslld, LEG(66, 0F, 0, 0xf2), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F F3 /r: PSLLQ mm, mm/m64 */
OPCODE(psllq, LEG(NP, 0F, 0, 0xf3), MMX, WRR, Pq, Pq, Qq)
/* 66 0F F3 /r: PSLLQ xmm1, xmm2/m128 */
OPCODE(psllq, LEG(66, 0F, 0, 0xf3), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F D1 /r: PSRLW mm, mm/m64 */
OPCODE(psrlw, LEG(NP, 0F, 0, 0xd1), MMX, WRR, Pq, Pq, Qq)
/* 66 0F D1 /r: PSRLW xmm1, xmm2/m128 */
OPCODE(psrlw, LEG(66, 0F, 0, 0xd1), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F D2 /r: PSRLD mm, mm/m64 */
OPCODE(psrld, LEG(NP, 0F, 0, 0xd2), MMX, WRR, Pq, Pq, Qq)
/* 66 0F D2 /r: PSRLD xmm1, xmm2/m128 */
OPCODE(psrld, LEG(66, 0F, 0, 0xd2), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F D3 /r: PSRLQ mm, mm/m64 */
OPCODE(psrlq, LEG(NP, 0F, 0, 0xd3), MMX, WRR, Pq, Pq, Qq)
/* 66 0F D3 /r: PSRLQ xmm1, xmm2/m128 */
OPCODE(psrlq, LEG(66, 0F, 0, 0xd3), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F E1 /r: PSRAW mm,mm/m64 */
OPCODE(psraw, LEG(NP, 0F, 0, 0xe1), MMX, WRR, Pq, Pq, Qq)
/* 66 0F E1 /r: PSRAW xmm1,xmm2/m128 */
OPCODE(psraw, LEG(66, 0F, 0, 0xe1), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F E2 /r: PSRAD mm,mm/m64 */
OPCODE(psrad, LEG(NP, 0F, 0, 0xe2), MMX, WRR, Pq, Pq, Qq)
/* 66 0F E2 /r: PSRAD xmm1,xmm2/m128 */
OPCODE(psrad, LEG(66, 0F, 0, 0xe2), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 63 /r: PACKSSWB mm1, mm2/m64 */
OPCODE(packsswb, LEG(NP, 0F, 0, 0x63), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 63 /r: PACKSSWB xmm1, xmm2/m128 */
OPCODE(packsswb, LEG(66, 0F, 0, 0x63), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 6B /r: PACKSSDW mm1, mm2/m64 */
OPCODE(packssdw, LEG(NP, 0F, 0, 0x6b), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 6B /r: PACKSSDW xmm1, xmm2/m128 */
OPCODE(packssdw, LEG(66, 0F, 0, 0x6b), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 67 /r: PACKUSWB mm, mm/m64 */
OPCODE(packuswb, LEG(NP, 0F, 0, 0x67), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 67 /r: PACKUSWB xmm1, xmm2/m128 */
OPCODE(packuswb, LEG(66, 0F, 0, 0x67), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 68 /r: PUNPCKHBW mm, mm/m64 */
OPCODE(punpckhbw, LEG(NP, 0F, 0, 0x68), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 68 /r: PUNPCKHBW xmm1, xmm2/m128 */
OPCODE(punpckhbw, LEG(66, 0F, 0, 0x68), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 69 /r: PUNPCKHWD mm, mm/m64 */
OPCODE(punpckhwd, LEG(NP, 0F, 0, 0x69), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 69 /r: PUNPCKHWD xmm1, xmm2/m128 */
OPCODE(punpckhwd, LEG(66, 0F, 0, 0x69), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 6A /r: PUNPCKHDQ mm, mm/m64 */
OPCODE(punpckhdq, LEG(NP, 0F, 0, 0x6a), MMX, WRR, Pq, Pq, Qq)
/* 66 0F 6A /r: PUNPCKHDQ xmm1, xmm2/m128 */
OPCODE(punpckhdq, LEG(66, 0F, 0, 0x6a), SSE2, WRR, Vdq, Vdq, Wdq)
/* 66 0F 6D /r: PUNPCKHQDQ xmm1, xmm2/m128 */
OPCODE(punpckhqdq, LEG(66, 0F, 0, 0x6d), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 60 /r: PUNPCKLBW mm, mm/m32 */
OPCODE(punpcklbw, LEG(NP, 0F, 0, 0x60), MMX, WRR, Pq, Pq, Qd)
/* 66 0F 60 /r: PUNPCKLBW xmm1, xmm2/m128 */
OPCODE(punpcklbw, LEG(66, 0F, 0, 0x60), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 61 /r: PUNPCKLWD mm, mm/m32 */
OPCODE(punpcklwd, LEG(NP, 0F, 0, 0x61), MMX, WRR, Pq, Pq, Qd)
/* 66 0F 61 /r: PUNPCKLWD xmm1, xmm2/m128 */
OPCODE(punpcklwd, LEG(66, 0F, 0, 0x61), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 62 /r: PUNPCKLDQ mm, mm/m32 */
OPCODE(punpckldq, LEG(NP, 0F, 0, 0x62), MMX, WRR, Pq, Pq, Qd)
/* 66 0F 62 /r: PUNPCKLDQ xmm1, xmm2/m128 */
OPCODE(punpckldq, LEG(66, 0F, 0, 0x62), SSE2, WRR, Vdq, Vdq, Wdq)
/* 66 0F 6C /r: PUNPCKLQDQ xmm1, xmm2/m128 */
OPCODE(punpcklqdq, LEG(66, 0F, 0, 0x6c), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 14 /r: UNPCKLPS xmm1, xmm2/m128 */
OPCODE(unpcklps, LEG(NP, 0F, 0, 0x14), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 14 /r: UNPCKLPD xmm1, xmm2/m128 */
OPCODE(unpcklpd, LEG(66, 0F, 0, 0x14), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 15 /r: UNPCKHPS xmm1, xmm2/m128 */
OPCODE(unpckhps, LEG(NP, 0F, 0, 0x15), SSE, WRR, Vdq, Vdq, Wdq)
/* 66 0F 15 /r: UNPCKHPD xmm1, xmm2/m128 */
OPCODE(unpckhpd, LEG(66, 0F, 0, 0x15), SSE2, WRR, Vdq, Vdq, Wdq)
/* NP 0F 70 /r ib: PSHUFW mm1, mm2/m64, imm8 */
OPCODE(pshufw, LEG(NP, 0F, 0, 0x70), SSE, WRR, Pq, Qq, Ib)
/* F2 0F 70 /r ib: PSHUFLW xmm1, xmm2/m128, imm8 */
OPCODE(pshuflw, LEG(F2, 0F, 0, 0x70), SSE2, WRR, Vdq, Wdq, Ib)
/* F3 0F 70 /r ib: PSHUFHW xmm1, xmm2/m128, imm8 */
OPCODE(pshufhw, LEG(F3, 0F, 0, 0x70), SSE2, WRR, Vdq, Wdq, Ib)
/* 66 0F 70 /r ib: PSHUFD xmm1, xmm2/m128, imm8 */
OPCODE(pshufd, LEG(66, 0F, 0, 0x70), SSE2, WRR, Vdq, Wdq, Ib)
/* NP 0F C6 /r ib: SHUFPS xmm1, xmm3/m128, imm8 */
OPCODE(shufps, LEG(NP, 0F, 0, 0xc6), SSE, WRRR, Vdq, Vdq, Wdq, Ib)
/* 66 0F C6 /r ib: SHUFPD xmm1, xmm2/m128, imm8 */
OPCODE(shufpd, LEG(66, 0F, 0, 0xc6), SSE2, WRRR, Vdq, Vdq, Wdq, Ib)
/* NP 0F C4 /r ib: PINSRW mm, r32/m16, imm8 */
OPCODE(pinsrw, LEG(NP, 0F, 0, 0xc4), SSE, WRRR, Pq, Pq, RdMw, Ib)
/* 66 0F C4 /r ib: PINSRW xmm, r32/m16, imm8 */
OPCODE(pinsrw, LEG(66, 0F, 0, 0xc4), SSE2, WRRR, Vdq, Vdq, RdMw, Ib)
/* NP 0F C5 /r ib: PEXTRW r32, mm, imm8 */
OPCODE(pextrw, LEG(NP, 0F, 0, 0xc5), SSE, WRR, Gd, Nq, Ib)
/* NP REX.W 0F C5 /r ib: PEXTRW r64, mm, imm8 */
OPCODE(pextrw, LEG(NP, 0F, 1, 0xc5), SSE, WRR, Gq, Nq, Ib)
/* 66 0F C5 /r ib: PEXTRW r32, xmm, imm8 */
OPCODE(pextrw, LEG(66, 0F, 0, 0xc5), SSE2, WRR, Gd, Udq, Ib)
/* 66 REX.W 0F C5 /r ib: PEXTRW r64, xmm, imm8 */
OPCODE(pextrw, LEG(66, 0F, 1, 0xc5), SSE2, WRR, Gq, Udq, Ib)
/* NP 0F 2A /r: CVTPI2PS xmm, mm/m64 */
OPCODE(cvtpi2ps, LEG(NP, 0F, 0, 0x2a), SSE, WR, Vdq, Qq)
/* F3 0F 2A /r: CVTSI2SS xmm1,r/m32 */
OPCODE(cvtsi2ss, LEG(F3, 0F, 0, 0x2a), SSE, WR, Vd, Ed)
/* F3 REX.W 0F 2A /r: CVTSI2SS xmm1,r/m64 */
OPCODE(cvtsi2ss, LEG(F3, 0F, 1, 0x2a), SSE, WR, Vd, Eq)
/* 66 0F 2A /r: CVTPI2PD xmm, mm/m64 */
OPCODE(cvtpi2pd, LEG(66, 0F, 0, 0x2a), SSE2, WR, Vdq, Qq)
/* F2 0F 2A /r: CVTSI2SD xmm1,r32/m32 */
OPCODE(cvtsi2sd, LEG(F2, 0F, 0, 0x2a), SSE2, WR, Vq, Ed)
/* F2 REX.W 0F 2A /r: CVTSI2SD xmm1,r/m64 */
OPCODE(cvtsi2sd, LEG(F2, 0F, 1, 0x2a), SSE2, WR, Vq, Eq)
/* NP 0F 2D /r: CVTPS2PI mm, xmm/m64 */
OPCODE(cvtps2pi, LEG(NP, 0F, 0, 0x2d), SSE, WR, Pq, Wq)
/* F3 0F 2D /r: CVTSS2SI r32,xmm1/m32 */
OPCODE(cvtss2si, LEG(F3, 0F, 0, 0x2d), SSE, WR, Gd, Wd)
/* F3 REX.W 0F 2D /r: CVTSS2SI r64,xmm1/m32 */
OPCODE(cvtss2si, LEG(F3, 0F, 1, 0x2d), SSE, WR, Gq, Wd)
/* 66 0F 2D /r: CVTPD2PI mm, xmm/m128 */
OPCODE(cvtpd2pi, LEG(66, 0F, 0, 0x2d), SSE2, WR, Pq, Wdq)
/* F2 0F 2D /r: CVTSD2SI r32,xmm1/m64 */
OPCODE(cvtsd2si, LEG(F2, 0F, 0, 0x2d), SSE2, WR, Gd, Wq)
/* F2 REX.W 0F 2D /r: CVTSD2SI r64,xmm1/m64 */
OPCODE(cvtsd2si, LEG(F2, 0F, 1, 0x2d), SSE2, WR, Gq, Wq)
/* NP 0F 2C /r: CVTTPS2PI mm, xmm/m64 */
OPCODE(cvttps2pi, LEG(NP, 0F, 0, 0x2c), SSE, WR, Pq, Wq)
/* F3 0F 2C /r: CVTTSS2SI r32,xmm1/m32 */
OPCODE(cvttss2si, LEG(F3, 0F, 0, 0x2c), SSE, WR, Gd, Wd)
/* F3 REX.W 0F 2C /r: CVTTSS2SI r64,xmm1/m32 */
OPCODE(cvttss2si, LEG(F3, 0F, 1, 0x2c), SSE, WR, Gq, Wd)
/* 66 0F 2C /r: CVTTPD2PI mm, xmm/m128 */
OPCODE(cvttpd2pi, LEG(66, 0F, 0, 0x2c), SSE2, WR, Pq, Wdq)
/* F2 0F 2C /r: CVTTSD2SI r32,xmm1/m64 */
OPCODE(cvttsd2si, LEG(F2, 0F, 0, 0x2c), SSE2, WR, Gd, Wq)
/* F2 REX.W 0F 2C /r: CVTTSD2SI r64,xmm1/m64 */
OPCODE(cvttsd2si, LEG(F2, 0F, 1, 0x2c), SSE2, WR, Gq, Wq)
/* F2 0F E6 /r: CVTPD2DQ xmm1, xmm2/m128 */
OPCODE(cvtpd2dq, LEG(F2, 0F, 0, 0xe6), SSE2, WR, Vdq, Wdq)
/* 66 0F E6 /r: CVTTPD2DQ xmm1, xmm2/m128 */
OPCODE(cvttpd2dq, LEG(66, 0F, 0, 0xe6), SSE2, WR, Vdq, Wdq)
/* F3 0F E6 /r: CVTDQ2PD xmm1, xmm2/m64 */
OPCODE(cvtdq2pd, LEG(F3, 0F, 0, 0xe6), SSE2, WR, Vdq, Wq)
/* NP 0F 5A /r: CVTPS2PD xmm1, xmm2/m64 */
OPCODE(cvtps2pd, LEG(NP, 0F, 0, 0x5a), SSE2, WR, Vdq, Wq)
/* 66 0F 5A /r: CVTPD2PS xmm1, xmm2/m128 */
OPCODE(cvtpd2ps, LEG(66, 0F, 0, 0x5a), SSE2, WR, Vdq, Wdq)
/* F3 0F 5A /r: CVTSS2SD xmm1, xmm2/m32 */
OPCODE(cvtss2sd, LEG(F3, 0F, 0, 0x5a), SSE2, WR, Vq, Wd)
/* F2 0F 5A /r: CVTSD2SS xmm1, xmm2/m64 */
OPCODE(cvtsd2ss, LEG(F2, 0F, 0, 0x5a), SSE2, WR, Vd, Wq)
/* NP 0F 5B /r: CVTDQ2PS xmm1, xmm2/m128 */
OPCODE(cvtdq2ps, LEG(NP, 0F, 0, 0x5b), SSE2, WR, Vdq, Wdq)
/* 66 0F 5B /r: CVTPS2DQ xmm1, xmm2/m128 */
OPCODE(cvtps2dq, LEG(66, 0F, 0, 0x5b), SSE2, WR, Vdq, Wdq)
/* F3 0F 5B /r: CVTTPS2DQ xmm1, xmm2/m128 */
OPCODE(cvttps2dq, LEG(F3, 0F, 0, 0x5b), SSE2, WR, Vdq, Wdq)
/* NP 0F F7 /r: MASKMOVQ mm1, mm2 */
OPCODE(maskmovq, LEG(NP, 0F, 0, 0xf7), SSE, RR, Pq, Nq)
/* 66 0F F7 /r: MASKMOVDQU xmm1, xmm2 */
OPCODE(maskmovdqu, LEG(66, 0F, 0, 0xf7), SSE2, RR, Vdq, Udq)
/* NP 0F 2B /r: MOVNTPS m128, xmm1 */
OPCODE(movntps, LEG(NP, 0F, 0, 0x2b), SSE, WR, Mdq, Vdq)
/* 66 0F 2B /r: MOVNTPD m128, xmm1 */
OPCODE(movntpd, LEG(66, 0F, 0, 0x2b), SSE2, WR, Mdq, Vdq)
/* NP 0F C3 /r: MOVNTI m32, r32 */
OPCODE(movnti, LEG(NP, 0F, 0, 0xc3), SSE2, WR, Md, Gd)
/* NP REX.W + 0F C3 /r: MOVNTI m64, r64 */
OPCODE(movnti, LEG(NP, 0F, 1, 0xc3), SSE2, WR, Mq, Gq)
/* NP 0F E7 /r: MOVNTQ m64, mm */
OPCODE(movntq, LEG(NP, 0F, 0, 0xe7), SSE, WR, Mq, Pq)
/* 66 0F E7 /r: MOVNTDQ m128, xmm1 */
OPCODE(movntdq, LEG(66, 0F, 0, 0xe7), SSE2, WR, Mdq, Vdq)
/* F3 90: PAUSE */
OPCODE(pause, LEG(F3, NA, 0, 0x90), SSE2, )
/* NP 0F 77: EMMS */
OPCODE(emms, LEG(NP, 0F, 0, 0x77), MMX, )

OPCODE_GRP(grp12_LEG_66, LEG(66, 0F, 0, 0x71))
OPCODE_GRP_BEGIN(grp12_LEG_66)
    /* 66 0F 71 /6 ib: PSLLW xmm1, imm8 */
    OPCODE_GRPMEMB(grp12_LEG_66, psllw, 6, SSE2, WRR, Udq, Udq, Ib)
    /* 66 0F 71 /2 ib: PSRLW xmm1, imm8 */
    OPCODE_GRPMEMB(grp12_LEG_66, psrlw, 2, SSE2, WRR, Udq, Udq, Ib)
    /* 66 0F 71 /4 ib: PSRAW xmm1,imm8 */
    OPCODE_GRPMEMB(grp12_LEG_66, psraw, 4, SSE2, WRR, Udq, Udq, Ib)
OPCODE_GRP_END(grp12_LEG_66)

OPCODE_GRP(grp12_LEG_NP, LEG(NP, 0F, 0, 0x71))
OPCODE_GRP_BEGIN(grp12_LEG_NP)
    /* NP 0F 71 /6 ib: PSLLW mm1, imm8 */
    OPCODE_GRPMEMB(grp12_LEG_NP, psllw, 6, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 71 /2 ib: PSRLW mm, imm8 */
    OPCODE_GRPMEMB(grp12_LEG_NP, psrlw, 2, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 71 /4 ib: PSRAW mm,imm8 */
    OPCODE_GRPMEMB(grp12_LEG_NP, psraw, 4, MMX, WRR, Nq, Nq, Ib)
OPCODE_GRP_END(grp12_LEG_NP)

OPCODE_GRP(grp13_LEG_66, LEG(66, 0F, 0, 0x72))
OPCODE_GRP_BEGIN(grp13_LEG_66)
    /* 66 0F 72 /6 ib: PSLLD xmm1, imm8 */
    OPCODE_GRPMEMB(grp13_LEG_66, pslld, 6, SSE2, WRR, Udq, Udq, Ib)
    /* 66 0F 72 /2 ib: PSRLD xmm1, imm8 */
    OPCODE_GRPMEMB(grp13_LEG_66, psrld, 2, SSE2, WRR, Udq, Udq, Ib)
    /* 66 0F 72 /4 ib: PSRAD xmm1,imm8 */
    OPCODE_GRPMEMB(grp13_LEG_66, psrad, 4, SSE2, WRR, Udq, Udq, Ib)
OPCODE_GRP_END(grp13_LEG_66)

OPCODE_GRP(grp13_LEG_NP, LEG(NP, 0F, 0, 0x72))
OPCODE_GRP_BEGIN(grp13_LEG_NP)
    /* NP 0F 72 /6 ib: PSLLD mm, imm8 */
    OPCODE_GRPMEMB(grp13_LEG_NP, pslld, 6, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 72 /2 ib: PSRLD mm, imm8 */
    OPCODE_GRPMEMB(grp13_LEG_NP, psrld, 2, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 72 /4 ib: PSRAD mm,imm8 */
    OPCODE_GRPMEMB(grp13_LEG_NP, psrad, 4, MMX, WRR, Nq, Nq, Ib)
OPCODE_GRP_END(grp13_LEG_NP)

OPCODE_GRP(grp14_LEG_66, LEG(66, 0F, 0, 0x73))
OPCODE_GRP_BEGIN(grp14_LEG_66)
    /* 66 0F 73 /6 ib: PSLLQ xmm1, imm8 */
    OPCODE_GRPMEMB(grp14_LEG_66, psllq, 6, SSE2, WRR, Udq, Udq, Ib)
    /* 66 0F 73 /7 ib: PSLLDQ xmm1, imm8 */
    OPCODE_GRPMEMB(grp14_LEG_66, pslldq, 7, SSE2, WRR, Udq, Udq, Ib)
    /* 66 0F 73 /2 ib: PSRLQ xmm1, imm8 */
    OPCODE_GRPMEMB(grp14_LEG_66, psrlq, 2, SSE2, WRR, Udq, Udq, Ib)
    /* 66 0F 73 /3 ib: PSRLDQ xmm1, imm8 */
    OPCODE_GRPMEMB(grp14_LEG_66, psrldq, 3, SSE2, WRR, Udq, Udq, Ib)
OPCODE_GRP_END(grp14_LEG_66)

OPCODE_GRP(grp14_LEG_NP, LEG(NP, 0F, 0, 0x73))
OPCODE_GRP_BEGIN(grp14_LEG_NP)
    /* NP 0F 73 /6 ib: PSLLQ mm, imm8 */
    OPCODE_GRPMEMB(grp14_LEG_NP, psllq, 6, MMX, WRR, Nq, Nq, Ib)
    /* NP 0F 73 /2 ib: PSRLQ mm, imm8 */
    OPCODE_GRPMEMB(grp14_LEG_NP, psrlq, 2, MMX, WRR, Nq, Nq, Ib)
OPCODE_GRP_END(grp14_LEG_NP)

OPCODE_GRP(grp15_LEG_NP, LEG(NP, 0F, 0, 0xae))
OPCODE_GRP_BEGIN(grp15_LEG_NP)
    /* NP 0F AE /7: SFENCE */
    /* NP 0F AE /7: CLFLUSH m8 */
    OPCODE_GRPMEMB(grp15_LEG_NP, sfence_clflush, 7, SSE, RR, modrm_mod, modrm)
    /* NP 0F AE /5: LFENCE */
    OPCODE_GRPMEMB(grp15_LEG_NP, lfence, 5, SSE2, )
    /* NP 0F AE /6: MFENCE */
    OPCODE_GRPMEMB(grp15_LEG_NP, mfence, 6, SSE2, )
    /* NP 0F AE /2: LDMXCSR m32 */
    OPCODE_GRPMEMB(grp15_LEG_NP, ldmxcsr, 2, SSE, R, Md)
    /* NP 0F AE /3: STMXCSR m32 */
    OPCODE_GRPMEMB(grp15_LEG_NP, stmxcsr, 3, SSE, W, Md)
OPCODE_GRP_END(grp15_LEG_NP)

OPCODE_GRP(grp16_LEG_NP, LEG(NP, 0F, 0, 0x18))
OPCODE_GRP_BEGIN(grp16_LEG_NP)
    /* 0F 18 /1: PREFETCHT0 m8 */
    OPCODE_GRPMEMB(grp16_LEG_NP, prefetcht0, 1, SSE, R, Mb)
    /* 0F 18 /2: PREFETCHT1 m8 */
    OPCODE_GRPMEMB(grp16_LEG_NP, prefetcht1, 2, SSE, R, Mb)
    /* 0F 18 /3: PREFETCHT2 m8 */
    OPCODE_GRPMEMB(grp16_LEG_NP, prefetcht2, 3, SSE, R, Mb)
    /* 0F 18 /0: PREFETCHNTA m8 */
    OPCODE_GRPMEMB(grp16_LEG_NP, prefetchnta, 0, SSE, R, Mb)
OPCODE_GRP_END(grp16_LEG_NP)

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
