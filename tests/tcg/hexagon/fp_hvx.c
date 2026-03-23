/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

int err;
#include "hvx_misc.h"

#if __HEXAGON_ARCH__ > 75
#error "After v75, compiler will replace some FP HVX instructions."
#endif

/******************************************************************************
 * NAN handling
 *****************************************************************************/

#define isnan(X) \
     (sizeof(X) == bytes_hf ? ((raw_hf(X) & ~0x8000) > 0x7c00) : \
                              ((raw_sf(X) & ~(1 << 31)) > 0x7f800000UL))

#define CHECK_NAN(A, DEF_NAN) (isnan(A) ? DEF_NAN : (A))
#define NAN_SF float_sf(0x7FFFFFFF)
#define NAN_HF float_hf(0x7FFF)

/******************************************************************************
 * Binary operations
 *****************************************************************************/

#define DEF_TEST_OP_2(vop, op, type_res, type_arg) \
    static void test_##vop##_##type_res##_##type_arg(void) \
    { \
        memset(expect, 0xff, sizeof(expect)); \
        memset(output, 0xff, sizeof(expect)); \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        HVX_Vector hvx_buffer0 = *(HVX_Vector *)&buffer0[0]; \
        HVX_Vector hvx_buffer1 = *(HVX_Vector *)&buffer1[0]; \
        \
        *hvx_output = \
            Q6_V##type_res##_##vop##_V##type_arg##V##type_arg(hvx_buffer0, \
                                                              hvx_buffer1); \
        \
        for (int i = 0; i < MAX_VEC_SIZE_BYTES / bytes_##type_res; i++) { \
            expect[0].type_res[i] = \
                raw_##type_res(op(float_##type_arg(buffer0[0].type_arg[i]), \
                                  float_##type_arg(buffer1[0].type_arg[i]))); \
        } \
        check_output_##type_res(__LINE__, 1); \
    }

#define SUM(X, Y, DEF_NAN) CHECK_NAN((X) + (Y), DEF_NAN)
#define SUB(X, Y, DEF_NAN) CHECK_NAN((X) - (Y), DEF_NAN)
#define MULT(X, Y, DEF_NAN) CHECK_NAN((X) * (Y), DEF_NAN)

#define SUM_SF(X, Y) SUM(X, Y, NAN_SF)
#define SUM_HF(X, Y) SUM(X, Y, NAN_HF)
#define SUB_SF(X, Y) SUB(X, Y, NAN_SF)
#define SUB_HF(X, Y) SUB(X, Y, NAN_HF)
#define MULT_SF(X, Y) MULT(X, Y, NAN_SF)
#define MULT_HF(X, Y) MULT(X, Y, NAN_HF)

DEF_TEST_OP_2(vadd, SUM_SF, sf, sf);
DEF_TEST_OP_2(vadd, SUM_HF, hf, hf);
DEF_TEST_OP_2(vsub, SUB_SF, sf, sf);
DEF_TEST_OP_2(vsub, SUB_HF, hf, hf);
DEF_TEST_OP_2(vmpy, MULT_SF, sf, sf);
DEF_TEST_OP_2(vmpy, MULT_HF, hf, hf);

#define MIN(X, Y, DEF_NAN) \
    ((isnan(X) || isnan(Y)) ? DEF_NAN : ((X) < (Y) ? (X) : (Y)))
#define MAX(X, Y, DEF_NAN) \
    ((isnan(X) || isnan(Y)) ? DEF_NAN : ((X) > (Y) ? (X) : (Y)))

#define MIN_HF(X, Y) MIN(X, Y, NAN_HF)
#define MAX_HF(X, Y) MAX(X, Y, NAN_HF)
#define MIN_SF(X, Y) MIN(X, Y, NAN_SF)
#define MAX_SF(X, Y) MAX(X, Y, NAN_SF)

DEF_TEST_OP_2(vfmin, MIN_SF, sf, sf);
DEF_TEST_OP_2(vfmax, MAX_SF, sf, sf);
DEF_TEST_OP_2(vfmin, MIN_HF, hf, hf);
DEF_TEST_OP_2(vfmax, MAX_HF, hf, hf);

/******************************************************************************
 * Other tests
 *****************************************************************************/

void test_vdmpy_sf_hf(bool acc)
{
    HVX_Vector *hvx_output = (HVX_Vector *)&output[0];
    HVX_Vector hvx_buffer0 = *(HVX_Vector *)&buffer0[0];
    HVX_Vector hvx_buffer1 = *(HVX_Vector *)&buffer1[0];

    uint32_t PREFIL_VAL = 0x111222;
    memset(expect, 0xff, sizeof(expect));
    *hvx_output = Q6_V_vsplat_R(PREFIL_VAL);

    if (!acc) {
        *hvx_output = Q6_Vsf_vdmpy_VhfVhf(hvx_buffer0, hvx_buffer1);
    } else {
        *hvx_output = Q6_Vsf_vdmpyacc_VsfVhfVhf(*hvx_output, hvx_buffer0,
                                                hvx_buffer1);
    }

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        float a1 = float_hf_to_sf(float_hf(buffer0[0].hf[2 * i + 1]));
        float a2 = float_hf_to_sf(float_hf(buffer0[0].hf[2 * i]));
        float a3 = float_hf_to_sf(float_hf(buffer1[0].hf[2 * i + 1]));
        float a4 = float_hf_to_sf(float_hf(buffer1[0].hf[2 * i]));
        float prev = acc ? float_sf(PREFIL_VAL) : 0;
        expect[0].sf[i] = raw_sf(CHECK_NAN((a1 * a3) + (a2 * a4) + prev, NAN_SF));
    }

    check_output_sf(__LINE__, 1);
}

int main(void)
{
    init_buffers();

    /* add/sub */
    test_vadd_sf_sf();
    test_vadd_hf_hf();
    test_vsub_sf_sf();
    test_vsub_hf_hf();

    /* multiply */
    test_vmpy_sf_sf();
    test_vmpy_hf_hf();

    /* dot product */
    test_vdmpy_sf_hf(false);
    test_vdmpy_sf_hf(true);

    /* min/max */
    test_vfmin_sf_sf();
    test_vfmin_hf_hf();
    test_vfmax_sf_sf();
    test_vfmax_hf_hf();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
