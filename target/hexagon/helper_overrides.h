/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_HELPER_OVERRIDES_H
#define HEXAGON_HELPER_OVERRIDES_H

/*
 * Here is a primer to understand the tag names for load/store instructions
 *
 * Data types
 *      b        signed byte                       r0 = memb(r2+#0)
 *     ub        unsigned byte                     r0 = memub(r2+#0)
 *      h        signed half word (16 bits)        r0 = memh(r2+#0)
 *     uh        unsigned half word                r0 = memuh(r2+#0)
 *      i        integer (32 bits)                 r0 = memw(r2+#0)
 *      d        double word (64 bits)             r1:0 = memd(r2+#0)
 *
 * Addressing modes
 *     _io       indirect with offset              r0 = memw(r1+#4)
 *     _ur       absolute with register offset     r0 = memw(r1<<#4+##variable)
 *     _rr       indirect with register offset     r0 = memw(r1+r4<<#2)
 *     gp        global pointer relative           r0 = memw(gp+#200)
 *     _sp       stack pointer relative            r0 = memw(r29+#12)
 *     _ap       absolute set                      r0 = memw(r1=##variable)
 *     _pr       post increment register           r0 = memw(r1++m1)
 *     _pbr      post increment bit reverse        r0 = memw(r1++m1:brev)
 *     _pi       post increment immediate          r0 = memb(r1++#1)
 *     _pci      post increment circular immediate r0 = memw(r1++#4:circ(m0))
 *     _pcr      post increment circular register  r0 = memw(r1++I:circ(m0))
 */

/* Macros for complex addressing modes */
#define GET_EA_ap \
    do { \
        fEA_IMM(UiV); \
        tcg_gen_movi_tl(ReV, UiV); \
    } while (0)
#define GET_EA_pr \
    do { \
        fEA_REG(RxV); \
        fPM_M(RxV, MuV); \
    } while (0)
#define GET_EA_pbr \
    do { \
        fEA_BREVR(RxV); \
        fPM_M(RxV, MuV); \
    } while (0)
#define GET_EA_pi \
    do { \
        fEA_REG(RxV); \
        fPM_I(RxV, siV); \
    } while (0)
#define GET_EA_pci \
    do { \
        fEA_REG(RxV); \
        fPM_CIRI(RxV, siV, MuV); \
    } while (0)
#define GET_EA_pcr(SHIFT) \
    do { \
        fEA_REG(RxV); \
        fPM_CIRR(RxV, fREAD_IREG(MuV, (SHIFT)), MuV); \
    } while (0)

/*
 * Many instructions will work with just macro redefinitions
 * with the caveat that they need a tmp variable to carry a
 * value between them.
 */
#define fWRAP_tmp(SHORTCODE) \
    do { \
        TCGv tmp = tcg_temp_new(); \
        SHORTCODE; \
        tcg_temp_free(tmp); \
    } while (0)

/* Byte load instructions */
#define fWRAP_L2_loadrub_io(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L2_loadrb_io(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L4_loadrub_ur(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L4_loadrb_ur(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L4_loadrub_rr(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L4_loadrb_rr(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadrubgp(GENHLPR, SHORTCODE)       fWRAP_tmp(SHORTCODE)
#define fWRAP_L2_loadrbgp(GENHLPR, SHORTCODE)        fWRAP_tmp(SHORTCODE)
#define fWRAP_SL1_loadrub_io(GENHLPR, SHORTCODE)     SHORTCODE
#define fWRAP_SL2_loadrb_io(GENHLPR, SHORTCODE)      SHORTCODE

/* Half word load instruction */
#define fWRAP_L2_loadruh_io(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L2_loadrh_io(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L4_loadruh_ur(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L4_loadrh_ur(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L4_loadruh_rr(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L4_loadrh_rr(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadruhgp(GENHLPR, SHORTCODE)       fWRAP_tmp(SHORTCODE)
#define fWRAP_L2_loadrhgp(GENHLPR, SHORTCODE)        fWRAP_tmp(SHORTCODE)
#define fWRAP_SL2_loadruh_io(GENHLPR, SHORTCODE)     SHORTCODE
#define fWRAP_SL2_loadrh_io(GENHLPR, SHORTCODE)      SHORTCODE

/* Word load instructions */
#define fWRAP_L2_loadri_io(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L4_loadri_ur(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L4_loadri_rr(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadrigp(GENHLPR, SHORTCODE)        fWRAP_tmp(SHORTCODE)
#define fWRAP_SL1_loadri_io(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_SL2_loadri_sp(GENHLPR, SHORTCODE)      fWRAP_tmp(SHORTCODE)

/* Double word load instructions */
#define fWRAP_L2_loadrd_io(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L4_loadrd_ur(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L4_loadrd_rr(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadrdgp(GENHLPR, SHORTCODE)        fWRAP_tmp(SHORTCODE)
#define fWRAP_SL2_loadrd_sp(GENHLPR, SHORTCODE)      fWRAP_tmp(SHORTCODE)

/* Instructions with multiple definitions */
#define fWRAP_LOAD_AP(RES, SIZE, SIGN) \
    do { \
        fMUST_IMMEXT(UiV); \
        fEA_IMM(UiV); \
        fLOAD(1, SIZE, SIGN, EA, RES); \
        tcg_gen_movi_tl(ReV, UiV); \
    } while (0)

#define fWRAP_L4_loadrub_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 1, u)
#define fWRAP_L4_loadrb_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 1, s)
#define fWRAP_L4_loadruh_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 2, u)
#define fWRAP_L4_loadrh_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 2, s)
#define fWRAP_L4_loadri_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 4, u)
#define fWRAP_L4_loadrd_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RddV, 8, u)

#define fWRAP_L2_loadrub_pci(GENHLPR, SHORTCODE) \
      fWRAP_tmp(SHORTCODE)
#define fWRAP_L2_loadrb_pci(GENHLPR, SHORTCODE) \
      fWRAP_tmp(SHORTCODE)
#define fWRAP_L2_loadruh_pci(GENHLPR, SHORTCODE) \
      fWRAP_tmp(SHORTCODE)
#define fWRAP_L2_loadrh_pci(GENHLPR, SHORTCODE) \
      fWRAP_tmp(SHORTCODE)
#define fWRAP_L2_loadri_pci(GENHLPR, SHORTCODE) \
      fWRAP_tmp(SHORTCODE)
#define fWRAP_L2_loadrd_pci(GENHLPR, SHORTCODE) \
      fWRAP_tmp(SHORTCODE)

#define fWRAP_PCR(SHIFT, LOAD) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        TCGv tmp = tcg_temp_new(); \
        fEA_REG(RxV); \
        fREAD_IREG(MuV, SHIFT); \
        gen_fcircadd(RxV, ireg, MuV, fREAD_CSREG(MuN)); \
        LOAD; \
        tcg_temp_free(tmp); \
        tcg_temp_free(ireg); \
    } while (0)

#define fWRAP_L2_loadrub_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(0, fLOAD(1, 1, u, EA, RdV))
#define fWRAP_L2_loadrb_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(0, fLOAD(1, 1, s, EA, RdV))
#define fWRAP_L2_loadruh_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(1, fLOAD(1, 2, u, EA, RdV))
#define fWRAP_L2_loadrh_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(1, fLOAD(1, 2, s, EA, RdV))
#define fWRAP_L2_loadri_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(2, fLOAD(1, 4, u, EA, RdV))
#define fWRAP_L2_loadrd_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(3, fLOAD(1, 8, u, EA, RddV))

#define fWRAP_L2_loadrub_pr(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L2_loadrub_pbr(GENHLPR, SHORTCODE)     SHORTCODE
#define fWRAP_L2_loadrub_pi(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L2_loadrb_pr(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadrb_pbr(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L2_loadrb_pi(GENHLPR, SHORTCODE)       SHORTCODE;
#define fWRAP_L2_loadruh_pr(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L2_loadruh_pbr(GENHLPR, SHORTCODE)     SHORTCODE
#define fWRAP_L2_loadruh_pi(GENHLPR, SHORTCODE)      SHORTCODE;
#define fWRAP_L2_loadrh_pr(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadrh_pbr(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L2_loadrh_pi(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadri_pr(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadri_pbr(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L2_loadri_pi(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadrd_pr(GENHLPR, SHORTCODE)       SHORTCODE
#define fWRAP_L2_loadrd_pbr(GENHLPR, SHORTCODE)      SHORTCODE
#define fWRAP_L2_loadrd_pi(GENHLPR, SHORTCODE)       SHORTCODE

/*
 * These instructions load 2 bytes and places them in
 * two halves of the destination register.
 * The GET_EA macro determines the addressing mode.
 * The fGB macro determines whether to zero-extend or
 * sign-extend.
 */
#define fWRAP_loadbXw2(GET_EA, fGB) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        TCGv tmp = tcg_temp_new(); \
        TCGv tmpV = tcg_temp_new(); \
        TCGv BYTE = tcg_temp_new(); \
        int i; \
        GET_EA; \
        fLOAD(1, 2, u, EA, tmpV); \
        tcg_gen_movi_tl(RdV, 0); \
        for (i = 0; i < 2; i++) { \
            fSETHALF(i, RdV, fGB(i, tmpV)); \
        } \
        tcg_temp_free(ireg); \
        tcg_temp_free(tmp); \
        tcg_temp_free(tmpV); \
        tcg_temp_free(BYTE); \
    } while (0)

#define fWRAP_L2_loadbzw2_io(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fEA_RI(RsV, siV), fGETUBYTE)
#define fWRAP_L4_loadbzw2_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fEA_IRs(UiV, RtV, uiV), fGETUBYTE)
#define fWRAP_L2_loadbsw2_io(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fEA_RI(RsV, siV), fGETBYTE)
#define fWRAP_L4_loadbsw2_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fEA_IRs(UiV, RtV, uiV), fGETBYTE)
#define fWRAP_L4_loadbzw2_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_ap, fGETUBYTE)
#define fWRAP_L2_loadbzw2_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pr, fGETUBYTE)
#define fWRAP_L2_loadbzw2_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pbr, fGETUBYTE)
#define fWRAP_L2_loadbzw2_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pi, fGETUBYTE)
#define fWRAP_L4_loadbsw2_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_ap, fGETBYTE)
#define fWRAP_L2_loadbsw2_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pr, fGETBYTE)
#define fWRAP_L2_loadbsw2_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pbr, fGETBYTE)
#define fWRAP_L2_loadbsw2_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pi, fGETBYTE)
#define fWRAP_L2_loadbzw2_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pci, fGETUBYTE)
#define fWRAP_L2_loadbsw2_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pci, fGETBYTE)
#define fWRAP_L2_loadbzw2_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pcr(1), fGETUBYTE)
#define fWRAP_L2_loadbsw2_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(GET_EA_pcr(1), fGETBYTE)

/*
 * These instructions load 4 bytes and places them in
 * four halves of the destination register pair.
 * The GET_EA macro determines the addressing mode.
 * The fGB macro determines whether to zero-extend or
 * sign-extend.
 */
#define fWRAP_loadbXw4(GET_EA, fGB) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        TCGv tmp = tcg_temp_new(); \
        TCGv tmpV = tcg_temp_new(); \
        TCGv BYTE = tcg_temp_new(); \
        int i; \
        GET_EA; \
        fLOAD(1, 4, u, EA, tmpV);  \
        tcg_gen_movi_i64(RddV, 0); \
        for (i = 0; i < 4; i++) { \
            fSETHALF(i, RddV, fGB(i, tmpV));  \
        }  \
        tcg_temp_free(ireg); \
        tcg_temp_free(tmp); \
        tcg_temp_free(tmpV); \
        tcg_temp_free(BYTE); \
    } while (0)

#define fWRAP_L2_loadbzw4_io(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fEA_RI(RsV, siV), fGETUBYTE)
#define fWRAP_L4_loadbzw4_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fEA_IRs(UiV, RtV, uiV), fGETUBYTE)
#define fWRAP_L2_loadbsw4_io(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fEA_RI(RsV, siV), fGETBYTE)
#define fWRAP_L4_loadbsw4_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fEA_IRs(UiV, RtV, uiV), fGETBYTE)
#define fWRAP_L2_loadbzw4_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pci, fGETUBYTE)
#define fWRAP_L2_loadbsw4_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pci, fGETBYTE)
#define fWRAP_L2_loadbzw4_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pcr(2), fGETUBYTE)
#define fWRAP_L2_loadbsw4_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pcr(2), fGETBYTE)
#define fWRAP_L4_loadbzw4_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_ap, fGETUBYTE)
#define fWRAP_L2_loadbzw4_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pr, fGETUBYTE)
#define fWRAP_L2_loadbzw4_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pbr, fGETUBYTE)
#define fWRAP_L2_loadbzw4_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pi, fGETUBYTE)
#define fWRAP_L4_loadbsw4_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_ap, fGETBYTE)
#define fWRAP_L2_loadbsw4_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pr, fGETBYTE)
#define fWRAP_L2_loadbsw4_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pbr, fGETBYTE)
#define fWRAP_L2_loadbsw4_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(GET_EA_pi, fGETBYTE)

/*
 * These instructions load a half word, shift the destination right by 16 bits
 * and place the loaded value in the high half word of the destination pair.
 * The GET_EA macro determines the addressing mode.
 */
#define fWRAP_loadalignh(GET_EA) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        TCGv tmp = tcg_temp_new(); \
        TCGv tmpV = tcg_temp_new(); \
        TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
        READ_REG_PAIR(RyyV, RyyN); \
        GET_EA;  \
        fLOAD(1, 2, u, EA, tmpV);  \
        tcg_gen_extu_i32_i64(tmp_i64, tmpV); \
        tcg_gen_shli_i64(tmp_i64, tmp_i64, 48); \
        tcg_gen_shri_i64(RyyV, RyyV, 16); \
        tcg_gen_or_i64(RyyV, RyyV, tmp_i64); \
        tcg_temp_free(ireg); \
        tcg_temp_free(tmp); \
        tcg_temp_free(tmpV); \
        tcg_temp_free_i64(tmp_i64); \
    } while (0)

#define fWRAP_L4_loadalignh_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(fEA_IRs(UiV, RtV, uiV))
#define fWRAP_L2_loadalignh_io(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(fEA_RI(RsV, siV))
#define fWRAP_L2_loadalignh_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(GET_EA_pci)
#define fWRAP_L2_loadalignh_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(GET_EA_pcr(1))
#define fWRAP_L4_loadalignh_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(GET_EA_ap)
#define fWRAP_L2_loadalignh_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(GET_EA_pr)
#define fWRAP_L2_loadalignh_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(GET_EA_pbr)
#define fWRAP_L2_loadalignh_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(GET_EA_pi)

/* Same as above, but loads a byte instead of half word */
#define fWRAP_loadalignb(GET_EA) \
    do { \
        TCGv ireg = tcg_temp_new(); \
        TCGv tmp = tcg_temp_new(); \
        TCGv tmpV = tcg_temp_new(); \
        TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
        READ_REG_PAIR(RyyV, RyyN); \
        GET_EA;  \
        fLOAD(1, 1, u, EA, tmpV);  \
        tcg_gen_extu_i32_i64(tmp_i64, tmpV); \
        tcg_gen_shli_i64(tmp_i64, tmp_i64, 56); \
        tcg_gen_shri_i64(RyyV, RyyV, 8); \
        tcg_gen_or_i64(RyyV, RyyV, tmp_i64); \
        tcg_temp_free(ireg); \
        tcg_temp_free(tmp); \
        tcg_temp_free(tmpV); \
        tcg_temp_free_i64(tmp_i64); \
    } while (0)

#define fWRAP_L2_loadalignb_io(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(fEA_RI(RsV, siV))
#define fWRAP_L4_loadalignb_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(fEA_IRs(UiV, RtV, uiV))
#define fWRAP_L2_loadalignb_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(GET_EA_pci)
#define fWRAP_L2_loadalignb_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(GET_EA_pcr(0))
#define fWRAP_L4_loadalignb_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(GET_EA_ap)
#define fWRAP_L2_loadalignb_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(GET_EA_pr)
#define fWRAP_L2_loadalignb_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(GET_EA_pbr)
#define fWRAP_L2_loadalignb_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(GET_EA_pi)

#endif
