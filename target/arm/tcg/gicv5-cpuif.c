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

FIELD(GIC_CDDIS, ID, 0, 24)
FIELD(GIC_CDDIS, TYPE, 29, 3)

FIELD(GIC_CDEN, ID, 0, 24)
FIELD(GIC_CDEN, TYPE, 29, 3)

FIELD(GIC_CDAFF, ID, 0, 24)
FIELD(GIC_CDAFF, IRM, 28, 1)
FIELD(GIC_CDAFF, TYPE, 29, 3)
FIELD(GIC_CDAFF, IAFFID, 32, 16)

FIELD(GIC_CDPEND, ID, 0, 24)
FIELD(GIC_CDPEND, TYPE, 29, 3)
FIELD(GIC_CDPEND, PENDING, 32, 1)

FIELD(GIC_CDHM, ID, 0, 24)
FIELD(GIC_CDHM, TYPE, 29, 3)
FIELD(GIC_CDHM, HM, 32, 1)

FIELD(GIC_CDRCFG, ID, 0, 24)
FIELD(GIC_CDRCFG, TYPE, 29, 3)

FIELD(ICC_IDR0_EL1, ID_BITS, 0, 4)
FIELD(ICC_IDR0_EL1, PRI_BITS, 4, 4)
FIELD(ICC_IDR0_EL1, GCIE_LEGACY, 8, 4)

/*
 * We implement 24 bits of interrupt ID, the mandated 5 bits of priority,
 * and no legacy GICv3.3 vcpu interface (yet)
 */
#define QEMU_ICC_IDR0 \
    ((4 << R_ICC_IDR0_EL1_PRI_BITS_SHIFT) |     \
     (1 << R_ICC_IDR0_EL1_ID_BITS_SHIFT))

/*
 * PPI handling modes are fixed and not software configurable.
 * R_CFSKX defines them for the architected PPIs: they are all Level,
 * except that PPI 24 (CTIIRQ) is IMPDEF and PPI 3 (SW_PPI) is Edge.
 * For unimplemented PPIs the field is RES0.  The PPI register bits
 * are 1 for Level and 0 for Edge.
 */
#define PPI_HMR0_RESET (~(1ULL << GICV5_PPI_SW_PPI))
#define PPI_HMR1_RESET (~0ULL)

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

static void gic_cddis_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5IntType type = FIELD_EX64(value, GIC_CDDIS, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDDIS, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_enabled(gic, id, false, domain, type, virtual);
}

static void gic_cden_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5IntType type = FIELD_EX64(value, GIC_CDEN, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDEN, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_enabled(gic, id, true, domain, type, virtual);
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

static void gic_cdaff_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    uint32_t iaffid = FIELD_EX64(value, GIC_CDAFF, IAFFID);
    GICv5RoutingMode irm = FIELD_EX64(value, GIC_CDAFF, IRM);
    GICv5IntType type = FIELD_EX64(value, GIC_CDAFF, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDAFF, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_target(gic, id, iaffid, irm, domain, type, virtual);
}

static void gic_cdpend_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    bool pending = FIELD_EX64(value, GIC_CDPEND, PENDING);
    GICv5IntType type = FIELD_EX64(value, GIC_CDPEND, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDPEND, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_pending(gic, id, pending, domain, type, virtual);
}

static void gic_cdrcfg_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5IntType type = FIELD_EX64(value, GIC_CDRCFG, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDRCFG, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    env->gicv5_cpuif.icc_icsr_el1 =
        gicv5_request_config(gic, id, domain, type, virtual);
}

static void gic_cdhm_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5HandlingMode hm = FIELD_EX64(value, GIC_CDHM, HM);
    GICv5IntType type = FIELD_EX64(value, GIC_CDAFF, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDAFF, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_handling(gic, id, hm, domain, type, virtual);
}

static void gic_ppi_cactive_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    uint64_t old = raw_read(env, ri);
    raw_write(env, ri, old & ~value);
}

static void gic_ppi_sactive_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    uint64_t old = raw_read(env, ri);
    raw_write(env, ri, old | value);
}

static void gic_ppi_cpend_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    uint64_t old = raw_read(env, ri);
    /* If ICC_PPI_HMR_EL1[n].HM is 1, PEND bits are RO */
    uint64_t hm = env->gicv5_cpuif.ppi_hm[ri->opc2 & 1];
    value &= ~hm;
    raw_write(env, ri, old & ~value);
}

static void gic_ppi_spend_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    uint64_t old = raw_read(env, ri);
    /* If ICC_PPI_HMR_EL1[n].HM is 1, PEND bits are RO */
    uint64_t hm = env->gicv5_cpuif.ppi_hm[ri->opc2 & 1];
    value &= ~hm;
    raw_write(env, ri, old | value);
}

static void gic_ppi_enable_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    raw_write(env, ri, value);
}

static void gic_ppi_priority_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    raw_write(env, ri, value);
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
    {   .name = "GIC_CDDIS", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 0,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cddis_write,
    },
    {   .name = "GIC_CDEN", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 1,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cden_write,
    },
    {   .name = "GIC_CDPRI", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 2,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdpri_write,
    },
    {   .name = "GIC_CDAFF", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 3,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdaff_write,
    },
    {   .name = "GIC_CDPEND", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 4,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdpend_write,
    },
    {   .name = "GIC_CDRCFG", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 5,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdrcfg_write,
    },
    {   .name = "GIC_CDHM", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 2, .opc2 = 1,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdhm_write,
    },
    {   .name = "ICC_IDR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 2,
        .access = PL1_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
        .resetvalue = QEMU_ICC_IDR0,
    },
    {   .name = "ICC_ICSR_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 4,
        .access = PL1_RW, .type = ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.icc_icsr_el1),
        .resetvalue = 0,
    },
    {   .name = "ICC_IAFFIDR_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 5,
        .access = PL1_R, .type = ARM_CP_NO_RAW,
        /* ICC_IAFFIDR_EL1 holds the IAFFID only, in its low bits */
        .fieldoffset = offsetof(CPUARMState, gicv5_iaffid),
        /*
         * The field is a constant value set in gicv5_set_gicv5state(),
         * so don't allow it to be overwritten by reset.
         */
        .resetfn = arm_cp_reset_ignore,
    },
    {   .name = "ICC_PPI_CACTIVER0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 0,
        .access = PL1_RW, .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_active[0]),
        .writefn = gic_ppi_cactive_write,
    },
    {   .name = "ICC_PPI_CACTIVER1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 1,
        .access = PL1_RW, .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_active[1]),
        .writefn = gic_ppi_cactive_write,
    },
    {   .name = "ICC_PPI_SACTIVER0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 2,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_active[0]),
        .writefn = gic_ppi_sactive_write,
    },
    {   .name = "ICC_PPI_SACTIVER1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 3,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_active[1]),
        .writefn = gic_ppi_sactive_write,
    },
    {   .name = "ICC_PPI_HMR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 0,
        .access = PL1_R, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_hm[0]),
        .resetvalue = PPI_HMR0_RESET,
    },
    {   .name = "ICC_PPI_HMR1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 1,
        .access = PL1_R, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_hm[1]),
        .resetvalue = PPI_HMR1_RESET,
    },
    {   .name = "ICC_PPI_ENABLER0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 6,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_enable[0]),
        .writefn = gic_ppi_enable_write,
    },
    {   .name = "ICC_PPI_ENABLER1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 7,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_enable[1]),
        .writefn = gic_ppi_enable_write,
    },
    {   .name = "ICC_PPI_CPENDR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 4,
        .access = PL1_RW, .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_pend[0]),
        .writefn = gic_ppi_cpend_write,
    },
    {   .name = "ICC_PPI_CPENDR1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 5,
        .access = PL1_RW, .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_pend[1]),
        .writefn = gic_ppi_cpend_write,
    },
    {   .name = "ICC_PPI_SPENDR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 6,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_pend[0]),
        .writefn = gic_ppi_spend_write,
    },
    {   .name = "ICC_PPI_SPENDR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 7,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_pend[1]),
        .writefn = gic_ppi_spend_write,
    },
};

void define_gicv5_cpuif_regs(ARMCPU *cpu)
{
    if (cpu_isar_feature(aa64_gcie, cpu)) {
        define_arm_cp_regs(cpu, gicv5_cpuif_reginfo);

        /*
         * There are 16 ICC_PPI_PRIORITYR<n>_EL1 regs, so define them
         * programmatically rather than listing them all statically.
         */
        for (int i = 0; i < 16; i++) {
            g_autofree char *name = g_strdup_printf("ICC_PPI_PRIORITYR%d_EL1", i);
            ARMCPRegInfo ppi_prio = {
                .name = name, .state = ARM_CP_STATE_AA64,
                .opc0 = 3, .opc1 = 0, .crn = 12,
                .crm = 14 + (i >> 3), .opc2 = i & 7,
                .access = PL1_RW, .type = ARM_CP_IO,
                .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_priority[i]),
                .writefn = gic_ppi_priority_write, .raw_writefn = raw_write,
            };
            define_one_arm_cp_reg(cpu, &ppi_prio);
        }
    }
}
