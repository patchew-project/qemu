/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * ASID2 Feature presence and enabled TCR2_EL1 bits test
 *
 * Copyright (c) 2025 Linaro Ltd
 *
 */

#include <stdint.h>
#include <minilib.h>
#include "sysregs.h"

#define ID_AA64MMFR3_EL1 S3_0_C0_C7_3
#define ID_AA64MMFR4_EL1 S3_0_C0_C7_4
#define TCR2_EL1 S3_0_C2_C0_3

int main()
{
    /*
     * Test for presence of ASID2 and three feature bits enabled by it:
     * https://developer.arm.com/documentation/109697/2025_09/Feature-descriptions/The-Armv9-5-architecture-extension
     * Bits added are FNG1, FNG0, and A2. These should be RES0 if A2 is
     * not enabled and read as the written value if A2 is enabled.
     */

    uint64_t read_tcr2;
    uint64_t idreg3;
    uint64_t idreg4;
    int tcr2_present;
    int asid2_present;

    /* Mask is FNG1, FNG0, and A2 */
    const uint64_t feature_mask = (1ULL << 18 | 1ULL << 17 | 1ULL << 16);

    idreg3 = read_sysreg(ID_AA64MMFR3_EL1);
    tcr2_present = ((idreg3 & 0xF) != 0);

    if (!tcr2_present) {
        ml_printf("TCR2 is not present, cannot perform test");
        return 0;
    }

    idreg4 = read_sysreg(ID_AA64MMFR4_EL1);
    asid2_present = ((idreg4 & 0xF00) != 0);

    /* write the feature mask and read back */
    write_sysreg(feature_mask, TCR2_EL1);
    read_tcr2 = read_sysreg(TCR2_EL1);

    if (asid2_present) {
        if ((read_tcr2 & feature_mask) == feature_mask) {
            ml_printf("OK\n");
            return 0;
        } else {
            ml_printf("FAIL: ASID2 present, but read value %lx != "
                      "written value %lx\n",
                      read_tcr2 & feature_mask, feature_mask);
            return 1;
        }
    } else {
        if (read_tcr2 == 0) {
            ml_printf("TCR2_EL1 reads as RES0 as expected\n");
            return 0;
        } else {
            ml_printf("FAIL: ASID2, missing but read value %lx != %lx\n",
                      read_tcr2 & feature_mask, feature_mask);
            return 1;
        }
    }
}
