/*
 * GICv5 CPU interface
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpregs.h"

static const ARMCPRegInfo gicv5_cpuif_reginfo[] = {
    /*
     * Barrier: wait until the effects of a cpuif system register
     * write have definitely made it to the IRS (and will thus show up
     * in cpuif reads from the IRS by this or other CPUs and in the
     * status of IRQ, FIQ etc). For QEMU we do all interaction with
     * the IRS synchronously, so we can make this a nop.
     */
    {   .name = "GSB_SYS", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 0,
        .access = PL1_W, .type = ARM_CP_NOP,
    },
    /*
     * Barrier: wait until the effects of acknowledging an interrupt
     * (via GICR CDIA or GICR CDNMIA) are visible, including the
     * effect on the {IRQ,FIQ,vIRQ,vFIQ} pending state. This is
     * a weaker version of GSB SYS. Again, for QEMU this is a nop.
     */
    {   .name = "GSB_ACK", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 1,
        .access = PL1_W, .type = ARM_CP_NOP,
    },
};

void define_gicv5_cpuif_regs(ARMCPU *cpu)
{
    if (cpu_isar_feature(aa64_gcie, cpu)) {
        define_arm_cp_regs(cpu, gicv5_cpuif_reginfo);
    }
}
