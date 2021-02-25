/*
 * Copyright(c) 2019-2020 rev.ng Srls. All Rights Reserved.
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

/* Copy rules */
#define fLSBOLD(VAL) (fGETBIT(0, VAL))
#define fSATH(VAL) fSATN(16, VAL)
#define fSATUH(VAL) fSATUN(16, VAL)
#define fVSATH(VAL) fVSATN(16, VAL)
#define fVSATUH(VAL) fVSATUN(16, VAL)
#define fSATUB(VAL) fSATUN(8, VAL)
#define fSATB(VAL) fSATN(8, VAL)
#define fVSATUB(VAL) fVSATUN(8, VAL)
#define fVSATB(VAL) fVSATN(8, VAL)
#define fCALL(A) fWRITE_LR(fREAD_NPC()); fWRITE_NPC(A);
#define fCALLR(A) fWRITE_LR(fREAD_NPC()); fWRITE_NPC(A);
#define fCAST2_8s(A) fSXTN(16, 64, A)
#define fCAST2_8u(A) fZXTN(16, 64, A)
#define fCAST8S_16S(A) (fSXTN(64, 128, A))
#define fCAST16S_8S(A) (fSXTN(128, 64, A))
#define fVSATW(A) fVSATN(32, fCAST8_8s(A))
#define fSATW(A) fSATN(32, fCAST8_8s(A))
#define fVSAT(A) fVSATN(32, A)
#define fSAT(A) fSATN(32, A)

/* Ease parsing */
#define f8BITSOF(VAL) ((VAL) ? 0xff : 0x00)
#define fREAD_GP() (Constant_extended ? (0) : GP)
#define fCLIP(DST, SRC, U) (DST = fMIN((1 << U) - 1, fMAX(SRC, -(1 << U))))
#define fBIDIR_ASHIFTL(SRC, SHAMT, REGSTYPE)                            \
    ((SHAMT > 0) ?                                                      \
     (fCAST##REGSTYPE##s(SRC) << SHAMT) :                               \
     (fCAST##REGSTYPE##s(SRC) >> -SHAMT))

#define fBIDIR_LSHIFTL(SRC, SHAMT, REGSTYPE)    \
    ((SHAMT > 0) ?                              \
     (fCAST##REGSTYPE##u(SRC) << SHAMT) :       \
     (fCAST##REGSTYPE##u(SRC) >>> -SHAMT))

#define fBIDIR_ASHIFTR(SRC, SHAMT, REGSTYPE)    \
    ((SHAMT > 0) ?                              \
     (fCAST##REGSTYPE##s(SRC) >> SHAMT) :       \
     (fCAST##REGSTYPE##s(SRC) << -SHAMT))

#define fBIDIR_SHIFTR(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) < 0) ? ((fCAST##REGSTYPE(SRC) << ((-(SHAMT)) - 1)) << 1)  \
                   : (fCAST##REGSTYPE(SRC) >> (SHAMT)))

#define fBIDIR_LSHIFTR(SRC, SHAMT, REGSTYPE)                            \
    fBIDIR_SHIFTR(SRC, SHAMT, REGSTYPE##u)

#define fSATVALN(N, VAL)                                                \
    fSET_OVERFLOW(                                                      \
        ((VAL) < 0) ? (-(1LL << ((N) - 1))) : ((1LL << ((N) - 1)) - 1)  \
    )

#define fSAT_ORIG_SHL(A, ORIG_REG)                                      \
    (((fCAST4s((fSAT(A)) ^ (fCAST4s(ORIG_REG)))) < 0)                   \
        ? fSATVALN(32, (fCAST4s(ORIG_REG)))                             \
        : ((((ORIG_REG) > 0) && ((A) == 0)) ? fSATVALN(32, (ORIG_REG))  \
                                            : fSAT(A)))

#define fBIDIR_ASHIFTR_SAT(SRC, SHAMT, REGSTYPE)                        \
    (((SHAMT) < 0) ? fSAT_ORIG_SHL((fCAST##REGSTYPE##s(SRC)             \
                        << ((-(SHAMT)) - 1)) << 1, (SRC))               \
                   : (fCAST##REGSTYPE##s(SRC) >> (SHAMT)))

#define fBIDIR_ASHIFTL_SAT(SRC, SHAMT, REGSTYPE)                        \
    (((SHAMT) < 0)                                                      \
     ? ((fCAST##REGSTYPE##s(SRC) >> ((-(SHAMT)) - 1)) >> 1)             \
     : fSAT_ORIG_SHL(fCAST##REGSTYPE##s(SRC) << (SHAMT), (SRC)))

#define fEXTRACTU_BIDIR(INREG, WIDTH, OFFSET)                           \
    (fZXTN(WIDTH, 32, fBIDIR_LSHIFTR((INREG), (OFFSET), 4_8)))

#define fCARRY_FROM_ADD(A, B, C)                                        \
    fGETUWORD(1,                                                        \
              fGETUWORD(1, A) +                                         \
              fGETUWORD(1, B) +                                         \
              fGETUWORD(1,                                              \
                        fGETUWORD(0, A) +                               \
                        fGETUWORD(0, B) + C))

#define fADDSAT64(DST, A, B)                                            \
        __a = fCAST8u(A);                                               \
        __b = fCAST8u(B);                                               \
        __sum = __a + __b;                                              \
        __xor = __a ^ __b;                                              \
        __mask = 0x8000000000000000ULL;                                 \
        if (__xor & __mask) {                                           \
            DST = __sum;                                                \
        }                                                               \
        else if ((__a ^ __sum) & __mask) {                              \
            if (__sum & __mask) {                                       \
                DST = 0x7FFFFFFFFFFFFFFFLL;                             \
                fSET_OVERFLOW();                                        \
            } else {                                                    \
                DST = 0x8000000000000000ULL;                            \
                fSET_OVERFLOW();                                        \
            }                                                           \
        } else {                                                        \
            DST = __sum;                                                \
        }

/* Negation operator */
#define fLSBOLDNOT(VAL) (!fGETBIT(0, VAL))
#define fLSBNEWNOT(PNUM) (!fLSBNEW(PNUM))
#define fLSBNEW0NOT (!fLSBNEW0)

/* Assignments */
#define fPCALIGN(IMM) (IMM = IMM & ~3)
#define fWRITE_LR(A) (LR = A)
#define fWRITE_FP(A) (FP = A)
#define fWRITE_SP(A) (SP = A)
#define fBRANCH(LOC, TYPE) (PC = LOC)
#define fJUMPR(REGNO, TARGET, TYPE) (PC = TARGET)
#define fWRITE_LOOP_REGS0(START, COUNT) SA0 = START; (LC0 = COUNT)
#define fWRITE_LOOP_REGS1(START, COUNT) SA1 = START; (LC1 = COUNT)
#define fWRITE_LC0(VAL) (LC0 = VAL)
#define fWRITE_LC1(VAL) (LC1 = VAL)
#define fSET_LPCFG(VAL) (USR.LPCFG = VAL)
#define fWRITE_P0(VAL) P0 = VAL;
#define fWRITE_P1(VAL) P1 = VAL;
#define fWRITE_P3(VAL) P3 = VAL;
#define fEA_RI(REG, IMM) (EA = REG + IMM)
#define fEA_RRs(REG, REG2, SCALE) (EA = REG + (REG2 << SCALE))
#define fEA_IRs(IMM, REG, SCALE) (EA = IMM + (REG << SCALE))
#define fEA_IMM(IMM) (EA = IMM)
#define fEA_REG(REG) (EA = REG)
#define fEA_BREVR(REG) (EA = fbrev(REG))
#define fEA_GPI(IMM) (EA = fREAD_GP() + IMM)
#define fPM_I(REG, IMM) (REG = REG + IMM)
#define fPM_M(REG, MVAL) (REG = REG + MVAL)
#define fWRITE_NPC(VAL) (PC = VAL)

/* Unary operators */
#define fROUND(A) (A + 0x8000)

/* Binary operators */
#define fADD128(A, B) (A + B)
#define fSUB128(A, B) (A - B)
#define fSHIFTR128(A, B) (size8s_t) (A >> B)
#define fSHIFTL128(A, B) (A << B)
#define fAND128(A, B) (A & B)
#define fSCALE(N, A) (A << N)
#define fASHIFTR(SRC, SHAMT, REGSTYPE) (SRC >> SHAMT)
#define fLSHIFTR(SRC, SHAMT, REGSTYPE) (SRC >>> SHAMT)
#define fROTL(SRC, SHAMT, REGSTYPE) fROTL(SRC, SHAMT)
#define fASHIFTL(SRC, SHAMT, REGSTYPE) (fCAST##REGSTYPE##s(SRC) << SHAMT)

/* Purge non-relavant parts */
#define fHIDE(A)
#define fBRANCH_SPECULATE_STALL(A, B, C, D, E)
