/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

#if __HEXAGON_ARCH__ > 75
#error "After v75, compiler will replace some FP HVX instructions."
#endif

int err;
#include "hvx_misc.h"
#include "hex_test.h"

#define MAX_TESTS_hf (MAX_VEC_SIZE_BYTES / 2)
#define MAX_TESTS_sf (MAX_VEC_SIZE_BYTES / 4)

#define TRUE_MASK_sf 0xffffffff
#define TRUE_MASK_hf 0xffff

static const char *comparisons[MAX_TESTS_sf][2];
static HVX_Vector *hvx_output = (HVX_Vector *)&output[0];
static HVX_Vector buffers[2], true_vec, false_vec;
static int exp_index;

#define ADD_TEST_CMP(TYPE, VAL1, VAL2, EXP) do { \
    ((MMVector *)&buffers[0])->TYPE[exp_index] = VAL1; \
    ((MMVector *)&buffers[1])->TYPE[exp_index] = VAL2; \
    expect[0].TYPE[exp_index] = EXP ? TRUE_MASK_##TYPE : 0; \
    comparisons[exp_index][0] = #VAL1; \
    comparisons[exp_index][1] = #VAL2; \
    assert(exp_index < MAX_TESTS_##TYPE); \
    exp_index++; \
} while (0)

#define TEST_CMP_GT(TYPE, VAL1, VAL2) do { \
    ADD_TEST_CMP(TYPE, VAL1, VAL2, true); \
    ADD_TEST_CMP(TYPE, VAL2, VAL1, false); \
} while (0)

#define PREP_TEST() do { \
    memset(&buffers, 0, sizeof(buffers)); \
    memset(expect, 0, sizeof(expect)); \
    exp_index = 0; \
} while (0)

#define CHECK(TYPE, TYPESZ) do { \
    HVX_VectorPred pred = Q6_Q_vcmp_gt_V##TYPE##V##TYPE(buffers[0], buffers[1]); \
    *hvx_output = Q6_V_vmux_QVV(pred, true_vec, false_vec); \
    for (int j = 0; j < MAX_VEC_SIZE_BYTES / TYPESZ; j++) { \
        if (output[0].TYPE[j] != expect[0].TYPE[j]) { \
            printf("ERROR: expected %s %s %s\n", comparisons[j][0], \
                   (expect[0].TYPE[j] != 0 ? ">" : "<="), comparisons[j][1]); \
            err++; \
        } \
    } \
} while (0)

static void test_cmp_sf(void)
{
    /*
     * General ordering for sf:
     * QNaN > SNaN > +Inf > numbers > -Inf > SNaN_neg > QNaN_neg
     */

    /* Test equality */
    PREP_TEST();
    ADD_TEST_CMP(sf, raw_sf(2.2),  raw_sf(2.2),  false);
    ADD_TEST_CMP(sf, SF_SNaN,      SF_SNaN,      false);
    CHECK(sf, 4);

    /* Common numbers */
    PREP_TEST();
    TEST_CMP_GT(sf, raw_sf(2.2),  raw_sf(2.1));
    TEST_CMP_GT(sf, raw_sf(0),    raw_sf(-2.2));
    CHECK(sf, 4);

    /* Infinity vs Infinity/NaN */
    PREP_TEST();
    TEST_CMP_GT(sf, SF_QNaN,      SF_INF);
    TEST_CMP_GT(sf, SF_SNaN,      SF_INF);
    TEST_CMP_GT(sf, SF_INF,       SF_INF_neg);
    TEST_CMP_GT(sf, SF_INF,       SF_SNaN_neg);
    TEST_CMP_GT(sf, SF_INF,       SF_QNaN_neg);
    TEST_CMP_GT(sf, SF_INF_neg,   SF_SNaN_neg);
    TEST_CMP_GT(sf, SF_INF_neg,   SF_QNaN_neg);
    TEST_CMP_GT(sf, SF_SNaN,      SF_INF_neg);
    TEST_CMP_GT(sf, SF_QNaN,      SF_INF_neg);
    CHECK(sf, 4);

    /* NaN vs NaN */
    PREP_TEST();
    TEST_CMP_GT(sf, SF_QNaN,      SF_SNaN);
    TEST_CMP_GT(sf, SF_SNaN,      SF_SNaN_neg);
    TEST_CMP_GT(sf, SF_SNaN_neg,  SF_QNaN_neg);
    CHECK(sf, 4);

    /* NaN vs non-NaN */
    PREP_TEST();
    TEST_CMP_GT(sf, SF_QNaN,      SF_one);
    TEST_CMP_GT(sf, SF_SNaN,      SF_one);
    TEST_CMP_GT(sf, SF_one,       SF_QNaN_neg);
    TEST_CMP_GT(sf, SF_one,       SF_SNaN_neg);
    CHECK(sf, 4);
}

static void test_cmp_hf(void)
{
    /*
     * General ordering for hf:
     * QNaN > SNaN > +Inf > numbers > -Inf > QSNaN_neg > QNaN_neg
     */

    /* Test equality */
    PREP_TEST();
    ADD_TEST_CMP(hf, raw_hf((_Float16)2.2),  raw_hf((_Float16)2.2),  false);
    ADD_TEST_CMP(hf, HF_SNaN,                HF_SNaN,      false);
    CHECK(hf, 2);

    /* Common numbers */
    PREP_TEST();
    TEST_CMP_GT(hf, raw_hf((_Float16)2.2),  raw_hf((_Float16)2.1));
    TEST_CMP_GT(hf, raw_hf((_Float16)0),    raw_hf((_Float16)-2.2));
    CHECK(hf, 2);

    /* Infinity vs Infinity/NaN */
    PREP_TEST();
    TEST_CMP_GT(hf, HF_QNaN,      HF_INF);
    TEST_CMP_GT(hf, HF_SNaN,      HF_INF);
    TEST_CMP_GT(hf, HF_INF,       HF_INF_neg);
    TEST_CMP_GT(hf, HF_INF,       HF_SNaN_neg);
    TEST_CMP_GT(hf, HF_INF,       HF_QNaN_neg);
    TEST_CMP_GT(hf, HF_INF_neg,   HF_SNaN_neg);
    TEST_CMP_GT(hf, HF_INF_neg,   HF_QNaN_neg);
    TEST_CMP_GT(hf, HF_SNaN,      HF_INF_neg);
    TEST_CMP_GT(hf, HF_QNaN,      HF_INF_neg);
    CHECK(hf, 2);

    /* NaN vs NaN */
    PREP_TEST();
    TEST_CMP_GT(hf, HF_QNaN,      HF_SNaN);
    TEST_CMP_GT(hf, HF_SNaN,      HF_SNaN_neg);
    TEST_CMP_GT(hf, HF_SNaN_neg,  HF_QNaN_neg);
    CHECK(hf, 2);

    /* NaN vs non-NaN */
    PREP_TEST();
    TEST_CMP_GT(hf, HF_QNaN,      HF_one);
    TEST_CMP_GT(hf, HF_SNaN,      HF_one);
    TEST_CMP_GT(hf, HF_one,       HF_QNaN_neg);
    TEST_CMP_GT(hf, HF_one,       HF_SNaN_neg);
    CHECK(hf, 2);
}

static void test_cmp_variants(void)
{
    HVX_VectorPred true_pred, false_pred, pred;
    memset(&true_pred, 0xff, sizeof(true_pred));
    memset(&false_pred, 0, sizeof(false_pred));

    PREP_TEST();
    ADD_TEST_CMP(sf, SF_one,  SF_zero, true);
    ADD_TEST_CMP(sf, SF_zero, SF_one,  false);
    ADD_TEST_CMP(sf, SF_one,  SF_zero, true);
    ADD_TEST_CMP(sf, SF_zero, SF_one,  false);

    /* greater and */
    pred = Q6_Q_vcmp_gtand_QVsfVsf(true_pred, buffers[0], buffers[1]);
    *hvx_output = Q6_V_vmux_QVV(pred, true_vec, false_vec);
    for (int j = 0; j < 4; j++) {
        int exp = j % 2 ? 0 : 0xffffffff;
        if (output[0].sf[j] != exp) {
            printf("ERROR line %d: gtand %d: expected 0x%x got 0x%x\n",
                   __LINE__, j, exp, output[0].sf[j]);
            err++;
        }
    }
    pred = Q6_Q_vcmp_gtand_QVsfVsf(false_pred, buffers[0], buffers[1]);
    *hvx_output = Q6_V_vmux_QVV(pred, true_vec, false_vec);
    for (int j = 0; j < 4; j++) {
        if (output[0].sf[j]) {
            printf("ERROR line %d: gtand %d: expected false\n", __LINE__, j);
            err++;
        }
    }

    /* greater or */
    pred = Q6_Q_vcmp_gtor_QVsfVsf(false_pred, buffers[0], buffers[1]);
    *hvx_output = Q6_V_vmux_QVV(pred, true_vec, false_vec);
    for (int j = 0; j < 4; j++) {
        int exp = j % 2 ? 0 : 0xffffffff;
        if (output[0].sf[j] != exp) {
            printf("ERROR line %d: gtor %d: expected 0x%x got 0x%x\n",
                   __LINE__, j, exp, output[0].sf[j]);
            err++;
        }
    }
    pred = Q6_Q_vcmp_gtor_QVsfVsf(true_pred, buffers[0], buffers[1]);
    *hvx_output = Q6_V_vmux_QVV(pred, true_vec, false_vec);
    for (int j = 0; j < 4; j++) {
        if (!output[0].sf[j]) {
            printf("ERROR line %d: gtor %d: expected true\n", __LINE__, j);
            err++;
        }
    }
}

int main(void)
{
    memset(&true_vec, 0xff, sizeof(true_vec));
    memset(&false_vec, 0, sizeof(false_vec));

    test_cmp_sf();
    test_cmp_hf();
    test_cmp_variants();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
