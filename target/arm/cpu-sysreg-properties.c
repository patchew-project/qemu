/*
 * QEMU ARM CPU SYSREG PROPERTIES
 * will be automatically generated
 *
 * Copyright (c) Red Hat, Inc. 2026
 *
 */

 /* SPDX-License-Identifier: GPL-2.0-or-later */

#include "cpu-idregs.h"

ARM64SysReg arm64_id_regs[NUM_ID_IDX];

void initialize_cpu_sysreg_properties(void)
{
    memset(arm64_id_regs, 0, sizeof(ARM64SysReg) * NUM_ID_IDX);
    /* CTR_EL0 */
    ARM64SysReg *CTR_EL0 = arm64_sysreg_get(CTR_EL0_IDX);
    CTR_EL0->name = "CTR_EL0";
    arm64_sysreg_add_field(CTR_EL0, "TminLine", 32, 37);
    arm64_sysreg_add_field(CTR_EL0, "DIC", 29, 29);
    arm64_sysreg_add_field(CTR_EL0, "IDC", 28, 28);
    arm64_sysreg_add_field(CTR_EL0, "CWG", 24, 27);
    arm64_sysreg_add_field(CTR_EL0, "ERG", 20, 23);
    arm64_sysreg_add_field(CTR_EL0, "DminLine", 16, 19);
    arm64_sysreg_add_field(CTR_EL0, "L1Ip", 14, 15);
    arm64_sysreg_add_field(CTR_EL0, "IminLine", 0, 3);
}

