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
#include "hw/intc/arm_gicv5_stream.h"

FIELD(GIC_CDPRI, ID, 0, 24)
FIELD(GIC_CDPRI, TYPE, 29, 3)
FIELD(GIC_CDPRI, PRIORITY, 35, 5)

static GICv5Common *gicv5_get_gic(CPUARMState *env)
{
    return env->gicv5state;
}

static GICv5Domain gicv5_logical_domain(CPUARMState *env)
{
    /*
     * Return the Logical Interrupt Domain, which is the one associated
     * with the security state selected by the SCR_EL3.{NS,NSE} bits
     */
    switch (arm_security_space_below_el3(env)) {
    case ARMSS_Secure:
        return GICV5_ID_S;
    case ARMSS_NonSecure:
        return GICV5_ID_NS;
    case ARMSS_Realm:
        return GICV5_ID_REALM;
    default:
        g_assert_not_reached();
    }
}

static GICv5Domain gicv5_current_phys_domain(CPUARMState *env)
{
    /*
     * Return the Current Physical Interrupt Domain as
     * defined by R_ZFCXM.
     */
    if (arm_current_el(env) == 3) {
        return GICV5_ID_EL3;
    }
    return gicv5_logical_domain(env);
}

static void gic_cdpri_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    uint8_t priority = FIELD_EX64(value, GIC_CDPRI, PRIORITY);
    GICv5IntType type = FIELD_EX64(value, GIC_CDPRI, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDPRI, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_priority(gic, id, priority, domain, type, virtual);
}

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
    {   .name = "GIC_CDPRI", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 2,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdpri_write,
    },
};

void define_gicv5_cpuif_regs(ARMCPU *cpu)
{
    if (cpu_isar_feature(aa64_gcie, cpu)) {
        define_arm_cp_regs(cpu, gicv5_cpuif_reginfo);
    }
}
