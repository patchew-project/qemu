/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARM Generic Interrupt Controller using HVF platform support
 *
 * Copyright (c) 2025 Mohamed Mediouni
 * Based on vGICv3 KVM code by Pavel Fedin
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/arm_gicv3_common.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "system/runstate.h"
#include "system/hvf.h"
#include "system/hvf_int.h"
#include "gicv3_internal.h"
#include "vgic_common.h"
#include "qom/object.h"
#include "target/arm/cpregs.h"
#include <Hypervisor/Hypervisor.h>

struct HVFARMGICv3Class {
    ARMGICv3CommonClass parent_class;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

#define TYPE_HVF_GICV3 "hvf-arm-gicv3"
typedef struct HVFARMGICv3Class HVFARMGICv3Class;

/* This is reusing the GICv3State typedef from ARM_GICV3_ITS_COMMON */
DECLARE_OBJ_CHECKERS(GICv3State, HVFARMGICv3Class,
                     HVF_GICV3, TYPE_HVF_GICV3);

/*
 * Loop through each distributor IRQ related register; since bits
 * corresponding to SPIs and PPIs are RAZ/WI when affinity routing
 * is enabled, we skip those.
 */
#define for_each_dist_irq_reg(_irq, _max, _field_width) \
    for (_irq = GIC_INTERNAL; _irq < _max; _irq += (32 / _field_width))

static void hvf_dist_get_priority(GICv3State *s, hv_gic_distributor_reg_t offset
    , uint8_t *bmp)
{
    uint64_t reg;
    uint32_t *field;
    int irq;
    field = (uint32_t *)(bmp);

    for_each_dist_irq_reg(irq, s->num_irq, 8) {
        hv_gic_get_distributor_reg(offset, &reg);
        *field = reg;
        offset += 4;
        field++;
    }
}

static void hvf_dist_put_priority(GICv3State *s, hv_gic_distributor_reg_t offset
    , uint8_t *bmp)
{
    uint32_t reg, *field;
    int irq;
    field = (uint32_t *)(bmp);

    for_each_dist_irq_reg(irq, s->num_irq, 8) {
        reg = *field;
        hv_gic_set_distributor_reg(offset, reg);
        offset += 4;
        field++;
    }
}

static void hvf_dist_get_edge_trigger(GICv3State *s, hv_gic_distributor_reg_t offset,
                                      uint32_t *bmp)
{
    uint64_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 2) {
        hv_gic_get_distributor_reg(offset, &reg);
        reg = half_unshuffle32(reg >> 1);
        if (irq % 32 != 0) {
            reg = (reg << 16);
        }
        *gic_bmp_ptr32(bmp, irq) |= reg;
        offset += 4;
    }
}

static void hvf_dist_put_edge_trigger(GICv3State *s, hv_gic_distributor_reg_t offset,
                                      uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 2) {
        reg = *gic_bmp_ptr32(bmp, irq);
        if (irq % 32 != 0) {
            reg = (reg & 0xffff0000) >> 16;
        } else {
            reg = reg & 0xffff;
        }
        reg = half_shuffle32(reg) << 1;
        hv_gic_set_distributor_reg(offset, reg);
        offset += 4;
    }
}

/* Read a bitmap register group from the kernel VGIC. */
static void hvf_dist_getbmp(GICv3State *s, hv_gic_distributor_reg_t offset, uint32_t *bmp)
{
    uint64_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 1) {

        hv_gic_get_distributor_reg(offset, &reg);
        *gic_bmp_ptr32(bmp, irq) = reg;
        offset += 4;
    }
}

static void hvf_dist_putbmp(GICv3State *s, hv_gic_distributor_reg_t offset,
                            hv_gic_distributor_reg_t clroffset, uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 1) {
        /*
         * If this bitmap is a set/clear register pair, first write to the
         * clear-reg to clear all bits before using the set-reg to write
         * the 1 bits.
         */
        if (clroffset != 0) {
            reg = 0;
            hv_gic_set_distributor_reg(clroffset, reg);
            clroffset += 4;
        }
        reg = *gic_bmp_ptr32(bmp, irq);
        hv_gic_set_distributor_reg(offset, reg);
        offset += 4;
    }
}

static void hvf_gicv3_check(GICv3State *s)
{
    uint64_t reg;
    uint32_t num_irq;

    /* Sanity checking s->num_irq */
    hv_gic_get_distributor_reg(HV_GIC_DISTRIBUTOR_REG_GICD_TYPER, &reg);
    num_irq = ((reg & 0x1f) + 1) * 32;

    if (num_irq < s->num_irq) {
        error_report("Model requests %u IRQs, but HVF supports max %u",
                     s->num_irq, num_irq);
        abort();
    }
}

static void hvf_gicv3_put(GICv3State *s)
{
    uint32_t reg;
    uint64_t reg64, redist_typer;
    int ncpu, i;

    hvf_gicv3_check(s);

    hv_vcpu_t vcpu0 = s->cpu[0].cpu->accel->fd;
    hv_gic_get_redistributor_reg(vcpu0, HV_GIC_REDISTRIBUTOR_REG_GICR_TYPER
        , &redist_typer);

    reg = s->gicd_ctlr;
    hv_gic_set_distributor_reg(HV_GIC_DISTRIBUTOR_REG_GICD_CTLR, reg);

    if (redist_typer & GICR_TYPER_PLPIS) {
        error_report("ITS is not supported on HVF.");
        abort();
    }

    /* Redistributor state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];
        hv_vcpu_t vcpu = c->cpu->accel->fd;

        reg = c->gicr_waker;
        hv_gic_set_redistributor_reg(vcpu, HV_GIC_REDISTRIBUTOR_REG_GICR_IGROUPR0, reg);

        reg = c->gicr_igroupr0;
        hv_gic_set_redistributor_reg(vcpu, HV_GIC_REDISTRIBUTOR_REG_GICR_IGROUPR0, reg);

        reg = ~0;
        hv_gic_set_redistributor_reg(vcpu, HV_GIC_REDISTRIBUTOR_REG_GICR_ICENABLER0, reg);
        reg = c->gicr_ienabler0;
        hv_gic_set_redistributor_reg(vcpu, HV_GIC_REDISTRIBUTOR_REG_GICR_ISENABLER0, reg);

        /* Restore config before pending so we treat level/edge correctly */
        reg = half_shuffle32(c->edge_trigger >> 16) << 1;
        hv_gic_set_redistributor_reg(vcpu, HV_GIC_REDISTRIBUTOR_REG_GICR_ICFGR1, reg);

        reg = ~0;
        hv_gic_set_redistributor_reg(vcpu, HV_GIC_REDISTRIBUTOR_REG_GICR_ICPENDR0, reg);
        reg = c->gicr_ipendr0;
        hv_gic_set_redistributor_reg(vcpu, HV_GIC_REDISTRIBUTOR_REG_GICR_ISPENDR0, reg);

        reg = ~0;
        hv_gic_set_redistributor_reg(vcpu, HV_GIC_REDISTRIBUTOR_REG_GICR_ICACTIVER0, reg);
        reg = c->gicr_iactiver0;
        hv_gic_set_redistributor_reg(vcpu, HV_GIC_REDISTRIBUTOR_REG_GICR_ISACTIVER0, reg);

        for (i = 0; i < GIC_INTERNAL; i += 4) {
            reg = c->gicr_ipriorityr[i] |
                (c->gicr_ipriorityr[i + 1] << 8) |
                (c->gicr_ipriorityr[i + 2] << 16) |
                (c->gicr_ipriorityr[i + 3] << 24);
            hv_gic_set_redistributor_reg(vcpu,
                HV_GIC_REDISTRIBUTOR_REG_GICR_IPRIORITYR0 + i, reg);
        }
    }

    /* s->enable bitmap -> GICD_ISENABLERn */
    hvf_dist_putbmp(s, HV_GIC_DISTRIBUTOR_REG_GICD_ISENABLER0
        , HV_GIC_DISTRIBUTOR_REG_GICD_ICENABLER0, s->enabled);

    /* s->group bitmap -> GICD_IGROUPRn */
    hvf_dist_putbmp(s, HV_GIC_DISTRIBUTOR_REG_GICD_IGROUPR0
        , 0, s->group);

    /* Restore targets before pending to ensure the pending state is set on
     * the appropriate CPU interfaces in the kernel
     */

    /* s->gicd_irouter[irq] -> GICD_IROUTERn */
    for (i = GIC_INTERNAL; i < s->num_irq; i++) {
        uint32_t offset = HV_GIC_DISTRIBUTOR_REG_GICD_IROUTER32 + (8 * i)
            - (8 * GIC_INTERNAL);
        hv_gic_set_distributor_reg(offset, s->gicd_irouter[i]);
    }


    /*
     * s->trigger bitmap -> GICD_ICFGRn
     * (restore configuration registers before pending IRQs so we treat
     * level/edge correctly)
     */
    hvf_dist_put_edge_trigger(s, HV_GIC_DISTRIBUTOR_REG_GICD_ICFGR0, s->edge_trigger);

    /* s->pending bitmap -> GICD_ISPENDRn */
    hvf_dist_putbmp(s, HV_GIC_DISTRIBUTOR_REG_GICD_ISPENDR0,
        HV_GIC_DISTRIBUTOR_REG_GICD_ICPENDR0, s->pending);

    /* s->active bitmap -> GICD_ISACTIVERn */
    hvf_dist_putbmp(s, HV_GIC_DISTRIBUTOR_REG_GICD_ISACTIVER0,
        HV_GIC_DISTRIBUTOR_REG_GICD_ICACTIVER0, s->active);

    /* s->gicd_ipriority[] -> GICD_IPRIORITYRn */
    hvf_dist_put_priority(s, HV_GIC_DISTRIBUTOR_REG_GICD_IPRIORITYR0, s->gicd_ipriority);

    /* CPU interface state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];
        int num_pri_bits;
        hv_vcpu_t vcpu = c->cpu->accel->fd;
        hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_SRE_EL1, c->icc_sre_el1);

        hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_CTLR_EL1,
                        c->icc_ctlr_el1[GICV3_NS]);
        hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_IGRPEN0_EL1,
                        c->icc_igrpen[GICV3_G0]);
        hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_IGRPEN1_EL1,
                        c->icc_igrpen[GICV3_G1NS]);
        hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_PMR_EL1, c->icc_pmr_el1);
        hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_BPR0_EL1, c->icc_bpr[GICV3_G0]);
        hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_BPR1_EL1, c->icc_bpr[GICV3_G1NS]);

        num_pri_bits = ((c->icc_ctlr_el1[GICV3_NS] &
                        ICC_CTLR_EL1_PRIBITS_MASK) >>
                        ICC_CTLR_EL1_PRIBITS_SHIFT) + 1;

        switch (num_pri_bits) {
        case 7:
            reg64 = c->icc_apr[GICV3_G0][3];
            hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_AP0R0_EL1 + 3, reg64);
            reg64 = c->icc_apr[GICV3_G0][2];
            hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_AP0R0_EL1 + 2, reg64);
            /* fall through */
        case 6:
            reg64 = c->icc_apr[GICV3_G0][1];
            hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_AP0R0_EL1 + 1, reg64);
            /* fall through */
        default:
            reg64 = c->icc_apr[GICV3_G0][0];
            hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_AP0R0_EL1, reg64);
        }

        switch (num_pri_bits) {
        case 7:
            reg64 = c->icc_apr[GICV3_G1NS][3];
            hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_AP1R0_EL1 + 3, reg64);
            reg64 = c->icc_apr[GICV3_G1NS][2];
            hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_AP1R0_EL1 + 2, reg64);
            /* fall through */
        case 6:
            reg64 = c->icc_apr[GICV3_G1NS][1];
            hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_AP1R0_EL1 + 1, reg64);
            /* fall through */
        default:
            reg64 = c->icc_apr[GICV3_G1NS][0];
            hv_gic_set_icc_reg(vcpu, HV_GIC_ICC_REG_AP1R0_EL1, reg64);
        }
    }
}

static void hvf_gicv3_get(GICv3State *s)
{
    uint64_t reg, redist_typer;
    int ncpu, i;

    hvf_gicv3_check(s);

    hv_vcpu_t vcpu0 = s->cpu[0].cpu->accel->fd;
    hv_gic_get_redistributor_reg(vcpu0,
        HV_GIC_REDISTRIBUTOR_REG_GICR_TYPER, &redist_typer);

    hv_gic_get_distributor_reg(HV_GIC_DISTRIBUTOR_REG_GICD_CTLR, &reg);
    s->gicd_ctlr = reg;

    /* Redistributor state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];
        hv_vcpu_t vcpu = c->cpu->accel->fd;

        hv_gic_get_redistributor_reg(vcpu,
            HV_GIC_REDISTRIBUTOR_REG_GICR_IGROUPR0, &reg);
        c->gicr_igroupr0 = reg;
        hv_gic_get_redistributor_reg(vcpu,
            HV_GIC_REDISTRIBUTOR_REG_GICR_ISENABLER0, &reg);
        c->gicr_ienabler0 = reg;
        hv_gic_get_redistributor_reg(vcpu,
            HV_GIC_REDISTRIBUTOR_REG_GICR_ICFGR1, &reg);
        c->edge_trigger = half_unshuffle32(reg >> 1) << 16;
        hv_gic_get_redistributor_reg(vcpu,
            HV_GIC_REDISTRIBUTOR_REG_GICR_ISPENDR0, &reg);
        c->gicr_ipendr0 = reg;
        hv_gic_get_redistributor_reg(vcpu,
            HV_GIC_REDISTRIBUTOR_REG_GICR_ISACTIVER0, &reg);
        c->gicr_iactiver0 = reg;

        for (i = 0; i < GIC_INTERNAL; i += 4) {
            hv_gic_get_redistributor_reg(vcpu,
                HV_GIC_REDISTRIBUTOR_REG_GICR_IPRIORITYR0 + i, &reg);
            c->gicr_ipriorityr[i] = extract32(reg, 0, 8);
            c->gicr_ipriorityr[i + 1] = extract32(reg, 8, 8);
            c->gicr_ipriorityr[i + 2] = extract32(reg, 16, 8);
            c->gicr_ipriorityr[i + 3] = extract32(reg, 24, 8);
        }
    }

    if (redist_typer & GICR_TYPER_PLPIS) {
        error_report("ITS is not supported on HVF.");
        abort();
    }

    /* GICD_IGROUPRn -> s->group bitmap */
    hvf_dist_getbmp(s, HV_GIC_DISTRIBUTOR_REG_GICD_IGROUPR0, s->group);

    /* GICD_ISENABLERn -> s->enabled bitmap */
    hvf_dist_getbmp(s, HV_GIC_DISTRIBUTOR_REG_GICD_ISENABLER0, s->enabled);

    /* GICD_ISPENDRn -> s->pending bitmap */
    hvf_dist_getbmp(s, HV_GIC_DISTRIBUTOR_REG_GICD_ISPENDR0, s->pending);

    /* GICD_ISACTIVERn -> s->active bitmap */
    hvf_dist_getbmp(s, HV_GIC_DISTRIBUTOR_REG_GICD_ISACTIVER0, s->active);

    /* GICD_ICFGRn -> s->trigger bitmap */
    hvf_dist_get_edge_trigger(s, HV_GIC_DISTRIBUTOR_REG_GICD_ICFGR0
        , s->edge_trigger);

    /* GICD_IPRIORITYRn -> s->gicd_ipriority[] */
    hvf_dist_get_priority(s, HV_GIC_DISTRIBUTOR_REG_GICD_IPRIORITYR0
        , s->gicd_ipriority);

    /* GICD_IROUTERn -> s->gicd_irouter[irq] */
    for (i = GIC_INTERNAL; i < s->num_irq; i++) {
        uint32_t offset = HV_GIC_DISTRIBUTOR_REG_GICD_IROUTER32
            + (8 * i) - (8 * GIC_INTERNAL);
        hv_gic_get_distributor_reg(offset, &s->gicd_irouter[i]);
    }

    /* CPU interface state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];
        hv_vcpu_t vcpu = c->cpu->accel->fd;
        int num_pri_bits;

        hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_SRE_EL1, &c->icc_sre_el1);

        hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_CTLR_EL1,
                        &c->icc_ctlr_el1[GICV3_NS]);
        hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_IGRPEN0_EL1,
                        &c->icc_igrpen[GICV3_G0]);
        hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_IGRPEN1_EL1,
                        &c->icc_igrpen[GICV3_G1NS]);
        hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_PMR_EL1
            , &c->icc_pmr_el1);
        hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_BPR0_EL1
            , &c->icc_bpr[GICV3_G0]);
        hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_BPR1_EL1
            , &c->icc_bpr[GICV3_G1NS]);
        num_pri_bits = ((c->icc_ctlr_el1[GICV3_NS] &
                        ICC_CTLR_EL1_PRIBITS_MASK) >>
                        ICC_CTLR_EL1_PRIBITS_SHIFT) + 1;

        switch (num_pri_bits) {
        case 7:
            hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_AP0R0_EL1 + 3
                , &c->icc_apr[GICV3_G0][3]);
            hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_AP0R0_EL1 + 2
                , &c->icc_apr[GICV3_G0][2]);
            /* fall through */
        case 6:
            hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_AP0R0_EL1 + 1
                , &c->icc_apr[GICV3_G0][1]);
            /* fall through */
        default:
            hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_AP0R0_EL1
                , &c->icc_apr[GICV3_G0][0]);
        }

        switch (num_pri_bits) {
        case 7:
            hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_AP1R0_EL1 + 3
                , &c->icc_apr[GICV3_G1NS][3]);
            hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_AP1R0_EL1 + 2
                , &c->icc_apr[GICV3_G1NS][2]);
            /* fall through */
        case 6:
            hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_AP1R0_EL1 + 1
                , &c->icc_apr[GICV3_G1NS][1]);
            /* fall through */
        default:
            hv_gic_get_icc_reg(vcpu, HV_GIC_ICC_REG_AP1R0_EL1
                , &c->icc_apr[GICV3_G1NS][0]);
        }
    }
}

static void hvf_gicv3_set_irq(void *opaque, int irq, int level)
{
    GICv3State *s = (GICv3State *)opaque;
    if (irq > s->num_irq) {
        return;
    }
    hv_gic_set_spi(GIC_INTERNAL + irq, !!level);
}

static void hvf_gicv3_icc_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    GICv3State *s;
    GICv3CPUState *c;

    c = (GICv3CPUState *)env->gicv3state;
    s = c->gic;

    c->icc_pmr_el1 = 0;
    /*
     * Architecturally the reset value of the ICC_BPR registers
     * is UNKNOWN. We set them all to 0 here; when the kernel
     * uses these values to program the ICH_VMCR_EL2 fields that
     * determine the guest-visible ICC_BPR register values, the
     * hardware's "writing a value less than the minimum sets
     * the field to the minimum value" behaviour will result in
     * them effectively resetting to the correct minimum value
     * for the host GIC.
     */
    c->icc_bpr[GICV3_G0] = 0;
    c->icc_bpr[GICV3_G1] = 0;
    c->icc_bpr[GICV3_G1NS] = 0;

    c->icc_sre_el1 = 0x7;
    memset(c->icc_apr, 0, sizeof(c->icc_apr));
    memset(c->icc_igrpen, 0, sizeof(c->icc_igrpen));

    if (s->migration_blocker) {
        return;
    }

    /* Initialize to actual HW supported configuration */
    hv_gic_get_icc_reg(c->cpu->accel->fd,
        HV_GIC_ICC_REG_CTLR_EL1, &c->icc_ctlr_el1[GICV3_NS]);

    c->icc_ctlr_el1[GICV3_S] = c->icc_ctlr_el1[GICV3_NS];
}

static void hvf_gicv3_reset_hold(Object *obj, ResetType type)
{
    GICv3State *s = ARM_GICV3_COMMON(obj);
    HVFARMGICv3Class *kgc = HVF_GICV3_GET_CLASS(s);

    if (kgc->parent_phases.hold) {
        kgc->parent_phases.hold(obj, type);
    }

    hvf_gicv3_put(s);
}


/*
 * CPU interface registers of GIC needs to be reset on CPU reset.
 * For the calling arm_gicv3_icc_reset() on CPU reset, we register
 * below ARMCPRegInfo. As we reset the whole cpu interface under single
 * register reset, we define only one register of CPU interface instead
 * of defining all the registers.
 */
static const ARMCPRegInfo gicv3_cpuif_reginfo[] = {
    { .name = "ICC_CTLR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 12, .opc2 = 4,
      /*
       * If ARM_CP_NOP is used, resetfn is not called,
       * So ARM_CP_NO_RAW is appropriate type.
       */
      .type = ARM_CP_NO_RAW,
      .access = PL1_RW,
      .readfn = arm_cp_read_zero,
      .writefn = arm_cp_write_ignore,
      /*
       * We hang the whole cpu interface reset routine off here
       * rather than parcelling it out into one little function
       * per register
       */
      .resetfn = hvf_gicv3_icc_reset,
    },
};

static void hvf_gicv3_realize(DeviceState *dev, Error **errp)
{
    GICv3State *s = HVF_GICV3(dev);
    HVFARMGICv3Class *kgc = HVF_GICV3_GET_CLASS(s);
    Error *local_err = NULL;
    int i;

    kgc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (s->revision != 3) {
        error_setg(errp, "unsupported GIC revision %d for platform GIC",
                   s->revision);
    }

    if (s->security_extn) {
        error_setg(errp, "the platform vGICv3 does not implement the "
                   "security extensions");
        return;
    }

    if (s->nmi_support) {
        error_setg(errp, "NMI is not supported with the platform GIC");
        return;
    }

    if (s->nb_redist_regions > 1) {
        error_setg(errp, "Multiple VGICv3 redistributor regions are not "
                   "supported by HVF");
        error_append_hint(errp, "A maximum of %d VCPUs can be used",
                          s->redist_region_count[0]);
        return;
    }

    gicv3_init_irqs_and_mmio(s, hvf_gicv3_set_irq, NULL);

    for (i = 0; i < s->num_cpu; i++) {
        ARMCPU *cpu = ARM_CPU(qemu_get_cpu(i));

        define_arm_cp_regs(cpu, gicv3_cpuif_reginfo);
    }

    if (s->maint_irq && s->maint_irq != HV_GIC_INT_MAINTENANCE) {
        error_setg(errp, "vGIC maintenance IRQ mismatch with the hardcoded one in HVF.");
        return;
    }
}

static void hvf_gicv3_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    ARMGICv3CommonClass *agcc = ARM_GICV3_COMMON_CLASS(klass);
    HVFARMGICv3Class *kgc = HVF_GICV3_CLASS(klass);

    agcc->pre_save = hvf_gicv3_get;
    agcc->post_load = hvf_gicv3_put;

    device_class_set_parent_realize(dc, hvf_gicv3_realize,
                                    &kgc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, hvf_gicv3_reset_hold, NULL,
                                       &kgc->parent_phases);
}

static const TypeInfo hvf_arm_gicv3_info = {
    .name = TYPE_HVF_GICV3,
    .parent = TYPE_ARM_GICV3_COMMON,
    .instance_size = sizeof(GICv3State),
    .class_init = hvf_gicv3_class_init,
    .class_size = sizeof(HVFARMGICv3Class),
};

static void hvf_gicv3_register_types(void)
{
    type_register_static(&hvf_arm_gicv3_info);
}

type_init(hvf_gicv3_register_types)
