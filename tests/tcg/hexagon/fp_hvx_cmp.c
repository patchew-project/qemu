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

#if __HEXAGON_ARCH__ > 75
#error "After v75, compiler will replace some FP HVX instructions."
#endif

int err;
#include "hvx_misc.h"
#include "hex_test.h"

#define TEST_CMP(VAL1, VAL2, EXP) do { \
    ((MMVector *)&buffers[0])->sf[index] = VAL1; \
    ((MMVector *)&buffers[1])->sf[index] = VAL2; \
    expect[0].w[index] = EXP ? 0xffffffff : 0; \
    index++; \
} while (0)

int main(void)
{
    HVX_Vector *hvx_output = (HVX_Vector *)&output[0];
    HVX_Vector buffers[2], true_vec, false_vec;
    HVX_VectorPred pred;
    int index = 0;

    memset(&buffers, 0, sizeof(buffers));
    memset(expect, 0, sizeof(expect));
    memset(&true_vec, 0xff, sizeof(true_vec));
    memset(&false_vec, 0, sizeof(false_vec));

    TEST_CMP(raw_sf(2.2),  raw_sf(2.1),  true);
    TEST_CMP(raw_sf(2.2),  raw_sf(2.2),  false);
    TEST_CMP(raw_sf(0),    raw_sf(-2.2), true);
    TEST_CMP(SF_SNaN,      SF_SNaN,      false);
    TEST_CMP(SF_INF,       SF_INF_neg,   true);
    TEST_CMP(SF_INF_neg,   SF_INF,       false);
    TEST_CMP(SF_SNaN,      SF_QNaN,      false);
    TEST_CMP(SF_QNaN,      SF_SNaN,      true);
    TEST_CMP(SF_QNaN,      SF_QNaN_neg,  true);

    pred = Q6_Q_vcmp_gt_VsfVsf(buffers[0], buffers[1]);
    *hvx_output = Q6_V_vmux_QVV(pred, true_vec, false_vec);

    check_output_sf(__LINE__, 1);

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
