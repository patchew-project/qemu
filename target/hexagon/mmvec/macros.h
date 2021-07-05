/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_MMVEC_MACROS_H
#define HEXAGON_MMVEC_MACROS_H

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "arch.h"
#include "mmvec/system_ext_mmvec.h"

#ifndef QEMU_GENERATE
#define VdV      (*(MMVector *)(VdV_void))
#define VsV      (*(MMVector *)(VsV_void))
#define VuV      (*(MMVector *)(VuV_void))
#define VvV      (*(MMVector *)(VvV_void))
#define VwV      (*(MMVector *)(VwV_void))
#define VxV      (*(MMVector *)(VxV_void))
#define VyV      (*(MMVector *)(VyV_void))

#define VddV     (*(MMVectorPair *)(VddV_void))
#define VuuV     (*(MMVectorPair *)(VuuV_void))
#define VvvV     (*(MMVectorPair *)(VvvV_void))
#define VxxV     (*(MMVectorPair *)(VxxV_void))

#define QeV      (*(MMQReg *)(QeV_void))
#define QdV      (*(MMQReg *)(QdV_void))
#define QsV      (*(MMQReg *)(QsV_void))
#define QtV      (*(MMQReg *)(QtV_void))
#define QuV      (*(MMQReg *)(QuV_void))
#define QvV      (*(MMQReg *)(QvV_void))
#define QxV      (*(MMQReg *)(QxV_void))
#endif

#define NEW_WRITTEN(NUM) ((env->VRegs_select >> (NUM)) & 1)
#define TMP_WRITTEN(NUM) ((env->VRegs_updated_tmp >> (NUM)) & 1)

#define TYPE_VOID(X)   __builtin_types_compatible_p(typeof(X), void *)
#define TYPE_MMVECTOR(X)   __builtin_types_compatible_p(typeof(X), MMVector)

#define LOG_VREG_WRITE_FUNC(X) \
    __builtin_choose_expr(TYPE_VOID(X), \
        log_vreg_write, \
        __builtin_choose_expr(TYPE_MMVECTOR(X), \
            log_mmvector_write, (void)0))
#define LOG_VREG_WRITE(NUM, VAR, VNEW) \
    LOG_VREG_WRITE_FUNC(VAR)(env, NUM, VAR, VNEW)

#define READ_EXT_VREG(NUM, VAR, VTMP) \
    do { \
        VAR = ((NEW_WRITTEN(NUM)) ? env->future_VRegs[NUM] \
                                  : env->VRegs[NUM]); \
        VAR = ((TMP_WRITTEN(NUM)) ? env->tmp_VRegs[NUM] : VAR); \
        if (VTMP == EXT_TMP) { \
            if (env->VRegs_updated & ((VRegMask)1) << (NUM)) { \
                VAR = env->future_VRegs[NUM]; \
                env->VRegs_updated ^= ((VRegMask)1) << (NUM); \
            } \
        } \
    } while (0)


#define WRITE_EXT_VREG(NUM, VAR, VNEW)   LOG_VREG_WRITE(NUM, VAR, VNEW)

#define LOG_VTCM_BYTE(VA, MASK, VAL, IDX) \
    do { \
        env->vtcm_log.data.ub[IDX] = (VAL); \
        env->vtcm_log.mask.ub[IDX] = (MASK); \
        env->vtcm_log.va[IDX] = (VA); \
    } while (0)

#define fNOTQ(VAL) \
    ({ \
        MMQReg _ret;  \
        int _i_;  \
        for (_i_ = 0; _i_ < fVECSIZE() / 64; _i_++) { \
            _ret.ud[_i_] = ~VAL.ud[_i_]; \
        } \
        _ret;\
     })
#define fGETQBITS(REG, WIDTH, MASK, BITNO) \
    ((MASK) & (REG.w[(BITNO) >> 5] >> ((BITNO) & 0x1f)))
#define fGETQBIT(REG, BITNO) fGETQBITS(REG, 1, 1, BITNO)
#define fGENMASKW(QREG, IDX) \
    (((fGETQBIT(QREG, (IDX * 4 + 0)) ? 0xFF : 0x0) << 0)  | \
     ((fGETQBIT(QREG, (IDX * 4 + 1)) ? 0xFF : 0x0) << 8)  | \
     ((fGETQBIT(QREG, (IDX * 4 + 2)) ? 0xFF : 0x0) << 16) | \
     ((fGETQBIT(QREG, (IDX * 4 + 3)) ? 0xFF : 0x0) << 24))
#define fGETNIBBLE(IDX, SRC) (fSXTN(4, 8, (SRC >> (4 * IDX)) & 0xF))
#define fGETCRUMB(IDX, SRC) (fSXTN(2, 8, (SRC >> (2 * IDX)) & 0x3))
#define fGETCRUMB_SYMMETRIC(IDX, SRC) \
    ((fGETCRUMB(IDX, SRC) >= 0 ? (2 - fGETCRUMB(IDX, SRC)) \
                               : fGETCRUMB(IDX, SRC)))
#define fGENMASKH(QREG, IDX) \
    (((fGETQBIT(QREG, (IDX * 2 + 0)) ? 0xFF : 0x0) << 0) | \
     ((fGETQBIT(QREG, (IDX * 2 + 1)) ? 0xFF : 0x0) << 8))
#define fGETMASKW(VREG, QREG, IDX) (VREG.w[IDX] & fGENMASKW((QREG), IDX))
#define fGETMASKH(VREG, QREG, IDX) (VREG.h[IDX] & fGENMASKH((QREG), IDX))
#define fCONDMASK8(QREG, IDX, YESVAL, NOVAL) \
    (fGETQBIT(QREG, IDX) ? (YESVAL) : (NOVAL))
#define fCONDMASK16(QREG, IDX, YESVAL, NOVAL) \
    ((fGENMASKH(QREG, IDX) & (YESVAL)) | \
     (fGENMASKH(fNOTQ(QREG), IDX) & (NOVAL)))
#define fCONDMASK32(QREG, IDX, YESVAL, NOVAL) \
    ((fGENMASKW(QREG, IDX) & (YESVAL)) | \
     (fGENMASKW(fNOTQ(QREG), IDX) & (NOVAL)))
#define fSETQBITS(REG, WIDTH, MASK, BITNO, VAL) \
    do { \
        uint32_t __TMP = (VAL); \
        REG.w[(BITNO) >> 5] &= ~((MASK) << ((BITNO) & 0x1f)); \
        REG.w[(BITNO) >> 5] |= (((__TMP) & (MASK)) << ((BITNO) & 0x1f)); \
    } while (0)
#define fSETQBIT(REG, BITNO, VAL) fSETQBITS(REG, 1, 1, BITNO, VAL)
#define fVBYTES() (fVECSIZE())
#define fVALIGN(ADDR, LOG2_ALIGNMENT) (ADDR = ADDR & ~(LOG2_ALIGNMENT - 1))
#define fVLASTBYTE(ADDR, LOG2_ALIGNMENT) (ADDR = ADDR | (LOG2_ALIGNMENT - 1))
#define fVELEM(WIDTH) ((fVECSIZE() * 8) / WIDTH)
#define fVECLOGSIZE() (7)
#define fVECSIZE() (1 << fVECLOGSIZE())
#define fSWAPB(A, B) do { uint8_t tmp = A; A = B; B = tmp; } while (0)
static inline MMVector mmvec_zero_vector(void)
{
    MMVector ret;
    memset(&ret, 0, sizeof(ret));
    return ret;
}
#define fVZERO() mmvec_zero_vector()
#define fNEWVREG(VNUM) \
    ((env->VRegs_updated & (((VRegMask)1) << VNUM)) ? env->future_VRegs[VNUM] \
                                                    : mmvec_zero_vector())
#define fV_AL_CHECK(EA, MASK) \
    if ((EA) & (MASK)) { \
        warn("aligning misaligned vector. EA=%08x", (EA)); \
    }
#define fSCATTER_INIT(REGION_START, LENGTH, ELEMENT_SIZE) \
    mem_vector_scatter_init(env, slot, REGION_START, LENGTH, ELEMENT_SIZE)
#define fGATHER_INIT(REGION_START, LENGTH, ELEMENT_SIZE) \
    mem_vector_gather_init(env, slot, REGION_START, LENGTH, ELEMENT_SIZE)
#define fSCATTER_FINISH(OP)
#define fGATHER_FINISH()
#define fLOG_SCATTER_OP(SIZE) \
    do { \
        env->vtcm_log.op = true; \
        env->vtcm_log.op_size = SIZE; \
    } while (0)
#define fVLOG_VTCM_WORD_INCREMENT(EA, OFFSET, INC, IDX, ALIGNMENT, LEN) \
    do { \
        int log_byte = 0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        for (int i0 = 0; i0 < 4; i0++) { \
            log_byte = (va + i0) <= va_high; \
            LOG_VTCM_BYTE(va + i0, log_byte, INC. ub[4 * IDX + i0], \
                          4 * IDX + i0); \
        } \
    } while (0)
#define fVLOG_VTCM_HALFWORD_INCREMENT(EA, OFFSET, INC, IDX, ALIGNMENT, LEN) \
    do { \
        int log_byte = 0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        for (int i0 = 0; i0 < 2; i0++) { \
            log_byte = (va + i0) <= va_high; \
            LOG_VTCM_BYTE(va + i0, log_byte, INC.ub[2 * IDX + i0], \
                          2 * IDX + i0); \
        } \
    } while (0)

#define fVLOG_VTCM_HALFWORD_INCREMENT_DV(EA, OFFSET, INC, IDX, IDX2, IDX_H, \
                                         ALIGNMENT, LEN) \
    do { \
        int log_byte = 0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        for (int i0 = 0; i0 < 2; i0++) { \
            log_byte = (va + i0) <= va_high; \
            LOG_VTCM_BYTE(va + i0, log_byte, INC.ub[2 * IDX + i0], \
                          2 * IDX + i0); \
        } \
    } while (0)

/* NOTE - Will this always be tmp_VRegs[0]; */
#define GATHER_FUNCTION(EA, OFFSET, IDX, LEN, ELEMENT_SIZE, BANK_IDX, QVAL) \
    do { \
        int i0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        int log_bank = 0; \
        int log_byte = 0; \
        for (i0 = 0; i0 < ELEMENT_SIZE; i0++) { \
            log_byte = ((va + i0) <= va_high) && QVAL; \
            log_bank |= (log_byte << i0); \
            uint8_t B; \
            get_user_u8(B, EA + i0); \
            env->tmp_VRegs[0].ub[ELEMENT_SIZE * IDX + i0] = B; \
            LOG_VTCM_BYTE(va + i0, log_byte, B, ELEMENT_SIZE * IDX + i0); \
        } \
    } while (0)
#define fVLOG_VTCM_GATHER_WORD(EA, OFFSET, IDX, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 4, IDX, 1); \
    } while (0)
#define fVLOG_VTCM_GATHER_HALFWORD(EA, OFFSET, IDX, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 2, IDX, 1); \
    } while (0)
#define fVLOG_VTCM_GATHER_HALFWORD_DV(EA, OFFSET, IDX, IDX2, IDX_H, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 2, (2 * IDX2 + IDX_H), 1); \
    } while (0)
#define fVLOG_VTCM_GATHER_WORDQ(EA, OFFSET, IDX, Q, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 4, IDX, \
                        fGETQBIT(QsV, 4 * IDX + i0)); \
    } while (0)
#define fVLOG_VTCM_GATHER_HALFWORDQ(EA, OFFSET, IDX, Q, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 2, IDX, \
                        fGETQBIT(QsV, 2 * IDX + i0)); \
    } while (0)
#define fVLOG_VTCM_GATHER_HALFWORDQ_DV(EA, OFFSET, IDX, IDX2, IDX_H, Q, LEN) \
    do { \
        GATHER_FUNCTION(EA, OFFSET, IDX, LEN, 2, (2 * IDX2 + IDX_H), \
                        fGETQBIT(QsV, 2 * IDX + i0)); \
    } while (0)
#define SCATTER_OP_WRITE_TO_MEM(TYPE) \
    do { \
        for (int i = 0; i < env->vtcm_log.size; i += sizeof(TYPE)) { \
            if (env->vtcm_log.mask.ub[i] != 0) { \
                TYPE dst = 0; \
                TYPE inc = 0; \
                for (int j = 0; j < sizeof(TYPE); j++) { \
                    uint8_t val; \
                    get_user_u8(val, env->vtcm_log.va[i + j]); \
                    dst |= val << (8 * j); \
                    inc |= env->vtcm_log.data.ub[j + i] << (8 * j); \
                    env->vtcm_log.mask.ub[j + i] = 0; \
                    env->vtcm_log.data.ub[j + i] = 0; \
                } \
                dst += inc; \
                for (int j = 0; j < sizeof(TYPE); j++) { \
                    put_user_u8((dst >> (8 * j)) & 0xFF, \
                        env->vtcm_log.va[i + j]);  \
                } \
            } \
        } \
    } while (0)
#define SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, ELEM_SIZE, BANK_IDX, QVAL, IN) \
    do { \
        int i0; \
        target_ulong va = EA; \
        target_ulong va_high = EA + LEN; \
        int log_bank = 0; \
        int log_byte = 0; \
        for (i0 = 0; i0 < ELEM_SIZE; i0++) { \
            log_byte = ((va + i0) <= va_high) && QVAL; \
            log_bank |= (log_byte << i0); \
            LOG_VTCM_BYTE(va + i0, log_byte, IN.ub[ELEM_SIZE * IDX + i0], \
                          ELEM_SIZE * IDX + i0); \
        } \
    } while (0)
#define fVLOG_VTCM_HALFWORD(EA, OFFSET, IN, IDX, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 2, IDX, 1, IN); \
    } while (0)
#define fVLOG_VTCM_WORD(EA, OFFSET, IN, IDX, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 4, IDX, 1, IN); \
    } while (0)
#define fVLOG_VTCM_HALFWORDQ(EA, OFFSET, IN, IDX, Q, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 2, IDX, \
                         fGETQBIT(QsV, 2 * IDX + i0), IN); \
    } while (0)
#define fVLOG_VTCM_WORDQ(EA, OFFSET, IN, IDX, Q, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 4, IDX, \
                         fGETQBIT(QsV, 4 * IDX + i0), IN); \
    } while (0)
#define fVLOG_VTCM_HALFWORD_DV(EA, OFFSET, IN, IDX, IDX2, IDX_H, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 2, \
                         (2 * IDX2 + IDX_H), 1, IN); \
    } while (0)
#define fVLOG_VTCM_HALFWORDQ_DV(EA, OFFSET, IN, IDX, Q, IDX2, IDX_H, LEN) \
    do { \
        SCATTER_FUNCTION(EA, OFFSET, IDX, LEN, 2, (2 * IDX2 + IDX_H), \
                         fGETQBIT(QsV, 2 * IDX + i0), IN); \
    } while (0)
#define fSTORERELEASE(EA, TYPE) \
    do { \
        fV_AL_CHECK(EA, fVECSIZE() - 1); \
    } while (0)
#define fLOADMMV_AL(EA, ALIGNMENT, LEN, DST) \
    do { \
        fV_AL_CHECK(EA, ALIGNMENT - 1); \
        mem_load_vector(env, EA & ~(ALIGNMENT - 1), LEN, &DST.ub[0]); \
    } while (0)
#define fLOADMMV(EA, DST) fLOADMMV_AL(EA, fVECSIZE(), fVECSIZE(), DST)
#define fLOADMMVU_AL(EA, ALIGNMENT, LEN, DST) \
    do { \
        uint32_t size2 = (EA) & (ALIGNMENT - 1); \
        uint32_t size1 = LEN - size2; \
        mem_load_vector(env, EA + size1, size2, &DST.ub[size1]); \
        mem_load_vector(env, EA, size1, &DST.ub[0]); \
    } while (0)
#define fLOADMMVU(EA, DST) \
    do { \
        if ((EA & (fVECSIZE() - 1)) == 0) { \
            fLOADMMV_AL(EA, fVECSIZE(), fVECSIZE(), DST); \
        } else { \
            fLOADMMVU_AL(EA, fVECSIZE(), fVECSIZE(), DST); \
        } \
    } while (0)
#define fSTOREMMV_AL(EA, ALIGNMENT, LEN, SRC) \
    do  { \
        fV_AL_CHECK(EA, ALIGNMENT - 1); \
        mem_store_vector(env, EA & ~(ALIGNMENT - 1), slot, LEN, \
                         &SRC.ub[0], NULL, false); \
    } while (0)
#define fSTOREMMV(EA, SRC) fSTOREMMV_AL(EA, fVECSIZE(), fVECSIZE(), SRC)
#define fSTOREMMVQ_AL(EA, ALIGNMENT, LEN, SRC, MASK) \
    do { \
        MMVector maskvec; \
        int i; \
        for (i = 0; i < fVECSIZE(); i++) { \
            maskvec.ub[i] = fGETQBIT(MASK, i); \
        } \
        mem_store_vector(env, EA & ~(ALIGNMENT - 1), slot, LEN, \
                         &SRC.ub[0], &maskvec.ub[0], false); \
    } while (0)
#define fSTOREMMVQ(EA, SRC, MASK) \
    fSTOREMMVQ_AL(EA, fVECSIZE(), fVECSIZE(), SRC, MASK)
#define fSTOREMMVNQ_AL(EA, ALIGNMENT, LEN, SRC, MASK) \
    do { \
        MMVector maskvec; \
        int i; \
        for (i = 0; i < fVECSIZE(); i++) { \
            maskvec.ub[i] = fGETQBIT(MASK, i); \
        } \
        fV_AL_CHECK(EA, ALIGNMENT - 1); \
        mem_store_vector(env, EA & ~(ALIGNMENT - 1), slot, LEN, \
                         &SRC.ub[0], &maskvec.ub[0], true); \
    } while (0)
#define fSTOREMMVNQ(EA, SRC, MASK) \
    fSTOREMMVNQ_AL(EA, fVECSIZE(), fVECSIZE(), SRC, MASK)
#define fSTOREMMVU_AL(EA, ALIGNMENT, LEN, SRC) \
    do { \
        uint32_t size1 = ALIGNMENT - ((EA) & (ALIGNMENT - 1)); \
        uint32_t size2; \
        if (size1 > LEN) { \
            size1 = LEN; \
        } \
        size2 = LEN - size1; \
        mem_store_vector(env, EA + size1, 1, size2, \
                         &SRC.ub[size1], NULL, false); \
        mem_store_vector(env, EA, 0, size1, &SRC.ub[0], NULL, false); \
    } while (0)
#define fSTOREMMVU(EA, SRC) \
    do { \
        if ((EA & (fVECSIZE() - 1)) == 0) { \
            fSTOREMMV_AL(EA, fVECSIZE(), fVECSIZE(), SRC); \
        } else { \
            fSTOREMMVU_AL(EA, fVECSIZE(), fVECSIZE(), SRC); \
        } \
    } while (0)
#define fSTOREMMVQU_AL(EA, ALIGNMENT, LEN, SRC, MASK) \
    do { \
        uint32_t size1 = ALIGNMENT - ((EA) & (ALIGNMENT - 1)); \
        uint32_t size2; \
        MMVector maskvec; \
        int i; \
        for (i = 0; i < fVECSIZE(); i++) { \
            maskvec.ub[i] = fGETQBIT(MASK, i); \
        } \
        if (size1 > LEN) { \
            size1 = LEN; \
        } \
        size2 = LEN - size1; \
        mem_store_vector(env, EA + size1, 1, size2, \
                         &SRC.ub[size1], &maskvec.ub[size1], false); \
        mem_store_vector(env, EA, size1, &SRC.ub[0], &maskvec.ub[0], false); \
    } while (0)
#define fSTOREMMVNQU_AL(EA, ALIGNMENT, LEN, SRC, MASK) \
    do { \
        uint32_t size1 = ALIGNMENT - ((EA) & (ALIGNMENT - 1)); \
        uint32_t size2; \
        MMVector maskvec; \
        int i; \
        for (i = 0; i < fVECSIZE(); i++) { \
            maskvec.ub[i] = fGETQBIT(MASK, i); \
        } \
        if (size1 > LEN) { \
            size1 = LEN; \
        } \
        size2 = LEN - size1; \
        mem_store_vector(env, EA + size1, 1, size2, \
                         &SRC.ub[size1], &maskvec.ub[size1], true); \
        mem_store_vector(env, EA, 0, size1, &SRC.ub[0], \
                         &maskvec.ub[0], true); \
    } while (0)
#define fVFOREACH(WIDTH, VAR) for (VAR = 0; VAR < fVELEM(WIDTH); VAR++)
#define fVARRAY_ELEMENT_ACCESS(ARRAY, TYPE, INDEX) \
    ARRAY.v[(INDEX) / (fVECSIZE() / (sizeof(ARRAY.TYPE[0])))].TYPE[(INDEX) % \
    (fVECSIZE() / (sizeof(ARRAY.TYPE[0])))]

#ifndef QEMU_GENERATE
/* Grabs the .tmp data, wherever it is, and clears the .tmp status */
/* Used for vhist */
static inline MMVector mmvec_vtmp_data(CPUHexagonState *env)
{
    VRegMask vsel = env->VRegs_updated_tmp;
    MMVector ret;
    int idx = clo32(~revbit32(vsel));
    if (vsel == 0) {
        printf("[UNDEFINED] no .tmp load when implicitly required...");
    }
    ret = env->tmp_VRegs[idx];
    env->VRegs_updated_tmp = 0;
    return ret;
}
#define fTMPVDATA() mmvec_vtmp_data(env)
#endif
#define fVSATDW(U, V) fVSATW(((((long long)U) << 32) | fZXTN(32, 64, V)))
#define fVASL_SATHI(U, V) fVSATW(((U) << 1) | ((V) >> 31))
#define fVUADDSAT(WIDTH, U, V) \
    fVSATUN(WIDTH, fZXTN(WIDTH, 2 * WIDTH, U) + fZXTN(WIDTH, 2 * WIDTH, V))
#define fVSADDSAT(WIDTH, U, V) \
    fVSATN(WIDTH, fSXTN(WIDTH, 2 * WIDTH, U) + fSXTN(WIDTH, 2 * WIDTH, V))
#define fVUSUBSAT(WIDTH, U, V) \
    fVSATUN(WIDTH, fZXTN(WIDTH, 2 * WIDTH, U) - fZXTN(WIDTH, 2 * WIDTH, V))
#define fVSSUBSAT(WIDTH, U, V) \
    fVSATN(WIDTH, fSXTN(WIDTH, 2 * WIDTH, U) - fSXTN(WIDTH, 2 * WIDTH, V))
#define fVAVGU(WIDTH, U, V) \
    ((fZXTN(WIDTH, 2 * WIDTH, U) + fZXTN(WIDTH, 2 * WIDTH, V)) >> 1)
#define fVAVGURND(WIDTH, U, V) \
    ((fZXTN(WIDTH, 2 * WIDTH, U) + fZXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1)
#define fVNAVGU(WIDTH, U, V) \
    ((fZXTN(WIDTH, 2 * WIDTH, U) - fZXTN(WIDTH, 2 * WIDTH, V)) >> 1)
#define fVNAVGURNDSAT(WIDTH, U, V) \
    fVSATUN(WIDTH, ((fZXTN(WIDTH, 2 * WIDTH, U) - \
                     fZXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1))
#define fVAVGS(WIDTH, U, V) \
    ((fSXTN(WIDTH, 2 * WIDTH, U) + fSXTN(WIDTH, 2 * WIDTH, V)) >> 1)
#define fVAVGSRND(WIDTH, U, V) \
    ((fSXTN(WIDTH, 2 * WIDTH, U) + fSXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1)
#define fVNAVGS(WIDTH, U, V) \
    ((fSXTN(WIDTH, 2 * WIDTH, U) - fSXTN(WIDTH, 2 * WIDTH, V)) >> 1)
#define fVNAVGSRND(WIDTH, U, V) \
    ((fSXTN(WIDTH, 2 * WIDTH, U) - fSXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1)
#define fVNAVGSRNDSAT(WIDTH, U, V) \
    fVSATN(WIDTH, ((fSXTN(WIDTH, 2 * WIDTH, U) - \
                    fSXTN(WIDTH, 2 * WIDTH, V) + 1) >> 1))
#define fVNOROUND(VAL, SHAMT) VAL
#define fVNOSAT(VAL) VAL
#define fVROUND(VAL, SHAMT) \
    ((VAL) + (((SHAMT) > 0) ? (1LL << ((SHAMT) - 1)) : 0))
#define fCARRY_FROM_ADD32(A, B, C) \
    (((fZXTN(32, 64, A) + fZXTN(32, 64, B) + C) >> 32) & 1)
#define fUARCH_NOTE_PUMP_4X()
#define fUARCH_NOTE_PUMP_2X()

#define IV1DEAD()
#endif
