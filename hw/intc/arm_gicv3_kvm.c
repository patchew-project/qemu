/*
 * ARM Generic Interrupt Controller using KVM in-kernel support
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Pavel Fedin
 * Based on vGICv2 code by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/sysbus.h"
#include "migration/migration.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "gicv3_internal.h"
#include "vgic_common.h"
#include "migration/migration.h"

#ifdef DEBUG_GICV3_KVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "kvm_gicv3: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#define TYPE_KVM_ARM_GICV3 "kvm-arm-gicv3"
#define KVM_ARM_GICV3(obj) \
     OBJECT_CHECK(GICv3State, (obj), TYPE_KVM_ARM_GICV3)
#define KVM_ARM_GICV3_CLASS(klass) \
     OBJECT_CLASS_CHECK(KVMARMGICv3Class, (klass), TYPE_KVM_ARM_GICV3)
#define KVM_ARM_GICV3_GET_CLASS(obj) \
     OBJECT_GET_CLASS(KVMARMGICv3Class, (obj), TYPE_KVM_ARM_GICV3)

#define ICC_PMR_EL1     \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 4, 6, 0)
#define ICC_BPR0_EL1    \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 8, 3)
#define ICC_AP0R_EL1(n) \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 8, 4 | n)
#define ICC_AP1R_EL1(n) \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 9, n)
#define ICC_BPR1_EL1    \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 12, 3)
#define ICC_CTLR_EL1    \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 12, 4)
#define ICC_IGRPEN0_EL1 \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 12, 6)
#define ICC_IGRPEN1_EL1 \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 12, 7)

typedef struct KVMARMGICv3Class {
    ARMGICv3CommonClass parent_class;
    DeviceRealize parent_realize;
    void (*parent_reset)(DeviceState *dev);
} KVMARMGICv3Class;

static void kvm_arm_gicv3_set_irq(void *opaque, int irq, int level)
{
    GICv3State *s = (GICv3State *)opaque;

    kvm_arm_gic_set_irq(s->num_irq, irq, level);
}

#define KVM_VGIC_ATTR(reg, typer) \
    ((typer & KVM_DEV_ARM_VGIC_V3_CPUID_MASK) | (reg))

static inline void kvm_gicd_access(GICv3State *s, int offset,
                                   uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
                      KVM_VGIC_ATTR(offset, 0),
                      val, write);
}

static inline void kvm_gicr_access(GICv3State *s, int offset, int cpu,
                                   uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_REDIST_REGS,
                      KVM_VGIC_ATTR(offset, s->cpu[cpu].gicr_typer),
                      val, write);
}

static inline void kvm_gicc_access(GICv3State *s, uint64_t reg, int cpu,
                                   uint64_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
                      KVM_VGIC_ATTR(reg, s->cpu[cpu].gicr_typer),
                      val, write);
}

static inline void kvm_gic_line_level_access(GICv3State *s, int irq, int cpu,
                                             uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO,
                      KVM_VGIC_ATTR(irq, s->cpu[cpu].gicr_typer) |
                      (KVM_DEV_ARM_VGIC_LINE_LEVEL_INFO_VAL <<
                       KVM_DEV_ARM_VGIC_LINE_LEVEL_INFO_SHIFT),
                      val, write);
}

/* Translate from the in-kernel field for an IRQ value to/from the qemu
 * representation. Note that these are only expected to be used for
 * SPIs (that is, for interrupts whose state is in the distributor
 * rather than the redistributor).
 */
typedef void (*vgic_translate_fn)(GICv3State *s, int irq,
                                  uint32_t *field, bool to_kernel);

static void translate_edge_trigger(GICv3State *s, int irq,
    uint32_t *field, bool to_kernel)
{
    /*
     * s->edge_trigger stores only even bits value of an irq config.
     * Consider only even bits and translate accordingly.
     */
    if (to_kernel) {
        *field = gicv3_gicd_edge_trigger_test(s, irq);
        *field = (*field << 1) & 3;
    } else {
        *field = (*field >> 1) & 1;
        gicv3_gicd_edge_trigger_replace(s, irq, *field);
    }
}

static void translate_priority(GICv3State *s, int irq,
                               uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = s->gicd_ipriority[irq];
    } else {
        s->gicd_ipriority[irq] = *field;
    }
}

/* Loop through each distributor IRQ related register; since bits
 * corresponding to SPIs and PPIs are RAZ/WI when affinity routing
 * is enabled, we skip those.
 */
#define for_each_dist_irq_reg(_irq, _max, _field_width) \
    for (_irq = GIC_INTERNAL; _irq < _max; _irq += (32 / _field_width))

/* Read a register group from the kernel VGIC */
static void kvm_dist_get(GICv3State *s, uint32_t offset, int width,
                         vgic_translate_fn translate_fn)
{
    uint32_t reg;
    int j;
    int irq;
    uint32_t field;
    int regsz = 32 / width; /* irqs per kernel register */

    for_each_dist_irq_reg(irq, s->num_irq, width) {
        kvm_gicd_access(s, offset, &reg, false);

        for (j = 0; j < regsz; j++) {
            field = extract32(reg, j * width, width);
            translate_fn(s, irq + j, &field, false);
        }
        offset += 4;
    }
}

/* Write a register group to the kernel VGIC */
static void kvm_dist_put(GICv3State *s, uint32_t offset, int width,
                         vgic_translate_fn translate_fn)
{
    uint32_t reg;
    int j;
    int irq;
    uint32_t field;
    int regsz = 32 / width; /* irqs per kernel register */

    for_each_dist_irq_reg(irq, s->num_irq, width) {
        reg = 0;
        for (j = 0; j < regsz; j++) {
            translate_fn(s, irq + j, &field, true);
            reg = deposit32(reg, j * width, width, field);
        }
        kvm_gicd_access(s, offset, &reg, true);
        offset += 4;
    }
}

static void kvm_gic_get_line_level_bmp(GICv3State *s, uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 1) {
        kvm_gic_line_level_access(s, irq, 0, &reg, false);
        *gic_bmp_ptr32(bmp, irq) = reg;
    }
}

static void kvm_gic_put_line_level_bmp(GICv3State *s, uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 1) {
        reg = *gic_bmp_ptr32(bmp, irq);
        kvm_gic_line_level_access(s, irq, 0, &reg, true);
    }
}

/* Read a bitmap register group from the kernel VGIC. */
static void kvm_dist_getbmp(GICv3State *s, uint32_t offset, uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 1) {
        kvm_gicd_access(s, offset, &reg, false);
        *gic_bmp_ptr32(bmp, irq) = reg;
        offset += 4;
    }
}

static void kvm_dist_putbmp(GICv3State *s, uint32_t offset,
                            uint32_t clroffset, uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 1) {
        /* If this bitmap is a set/clear register pair, first write to the
         * clear-reg to clear all bits before using the set-reg to write
         * the 1 bits.
         */
        if (clroffset != 0) {
            reg = 0;
            kvm_gicd_access(s, clroffset, &reg, true);
        }
        reg = *gic_bmp_ptr32(bmp, irq);
        kvm_gicd_access(s, offset, &reg, true);
        offset += 4;
    }
}

static void kvm_arm_gicv3_check(GICv3State *s)
{
    uint32_t reg;
    uint32_t num_irq;

    /* Sanity checking s->num_irq */
    kvm_gicd_access(s, GICD_TYPER, &reg, false);
    num_irq = ((reg & 0x1f) + 1) * 32;

    if (num_irq < s->num_irq) {
        error_report("Model requests %u IRQs, but kernel supports max %u",
                     s->num_irq, num_irq);
        abort();
    }
}

static void kvm_arm_gicv3_put(GICv3State *s)
{
    uint32_t regl, regh, reg;
    uint64_t reg64, redist_typer;
    int ncpu, i;

    kvm_arm_gicv3_check(s);

    kvm_gicr_access(s, GICR_TYPER, 0, &regl, false);
    kvm_gicr_access(s, GICR_TYPER + 4, 0, &regh, false);
    redist_typer = ((uint64_t)regh << 32) | regl;

    reg = s->gicd_ctlr;
    kvm_gicd_access(s, GICD_CTLR, &reg, true);

    if (redist_typer & GICR_TYPER_PLPIS) {
        /* Set base addresses before LPIs are enabled by GICR_CTLR write */
        for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
            GICv3CPUState *c = &s->cpu[ncpu];

            reg64 = c->gicr_propbaser;
            regl = (uint32_t)reg64;
            kvm_gicr_access(s, GICR_PROPBASER, ncpu, &regl, true);
            regh = (uint32_t)(reg64 >> 32);
            kvm_gicr_access(s, GICR_PROPBASER + 4, ncpu, &regh, true);

            reg64 = c->gicr_pendbaser;
            if (!c->gicr_ctlr & GICR_CTLR_ENABLE_LPIS) {
                /* Setting PTZ is advised if LPIs are disabled, to reduce
                 * GIC initialization time.
                 */
                reg64 |= GICR_PENDBASER_PTZ;
            }
            regl = (uint32_t)reg64;
            kvm_gicr_access(s, GICR_PENDBASER, ncpu, &regl, true);
            regh = (uint32_t)(reg64 >> 32);
            kvm_gicr_access(s, GICR_PENDBASER + 4, ncpu, &regh, true);
        }
    }

    /* Redistributor state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        reg = c->gicr_ctlr;
        kvm_gicr_access(s, GICR_CTLR, ncpu, &reg, true);

        reg = c->gicr_statusr[GICV3_NS];
        kvm_gicr_access(s, GICR_STATUSR, ncpu, &reg, true);

        reg = c->gicr_waker;
        kvm_gicr_access(s, GICR_WAKER, ncpu, &reg, true);

        reg = c->gicr_igroupr0;
        kvm_gicr_access(s, GICR_IGROUPR0, ncpu, &reg, true);

        reg = ~0;
        kvm_gicr_access(s, GICR_ICENABLER0, ncpu, &reg, true);
        reg = c->gicr_ienabler0;
        kvm_gicr_access(s, GICR_ISENABLER0, ncpu, &reg, true);

        /* Restore config before pending so we treat level/edge correctly */
        reg = half_shuffle32(c->edge_trigger >> 16) << 1;
        kvm_gicr_access(s, GICR_ICFGR1, ncpu, &reg, true);

        reg = c->level;
        kvm_gic_line_level_access(s, 0, ncpu, &reg, true);

        reg = ~0;
        kvm_gicr_access(s, GICR_ICPENDR0, ncpu, &reg, true);
        reg = c->gicr_ipendr0;
        kvm_gicr_access(s, GICR_ISPENDR0, ncpu, &reg, true);

        reg = ~0;
        kvm_gicr_access(s, GICR_ICACTIVER0, ncpu, &reg, true);
        reg = c->gicr_iactiver0;
        kvm_gicr_access(s, GICR_ISACTIVER0, ncpu, &reg, true);

        for (i = 0; i < GIC_INTERNAL; i += 4) {
            reg = c->gicr_ipriorityr[i] |
                (c->gicr_ipriorityr[i + 1] << 8) |
                (c->gicr_ipriorityr[i + 2] << 16) |
                (c->gicr_ipriorityr[i + 3] << 24);
            kvm_gicr_access(s, GICR_IPRIORITYR + i, ncpu, &reg, true);
        }
    }

    /* Distributor state (shared between all CPUs */
    reg = s->gicd_statusr[GICV3_NS];
    kvm_gicd_access(s, GICD_STATUSR, &reg, true);

    /* s->enable bitmap -> GICD_ISENABLERn */
    kvm_dist_putbmp(s, GICD_ISENABLER, GICD_ICENABLER, s->enabled);

    /* s->group bitmap -> GICD_IGROUPRn */
    kvm_dist_putbmp(s, GICD_IGROUPR, 0, s->group);

    /* Restore targets before pending to ensure the pending state is set on
     * the appropriate CPU interfaces in the kernel
     */

    /* s->gicd_irouter[irq] -> GICD_IROUTERn
     * We can't use kvm_dist_put() here because the registers are 64-bit
     */
    for (i = GIC_INTERNAL; i < s->num_irq; i++) {
        uint32_t offset;

        offset = GICD_IROUTER + (sizeof(uint32_t) * i);
        reg = (uint32_t)s->gicd_irouter[i];
        kvm_gicd_access(s, offset, &reg, true);

        offset = GICD_IROUTER + (sizeof(uint32_t) * i) + 4;
        reg = (uint32_t)(s->gicd_irouter[i] >> 32);
        kvm_gicd_access(s, offset, &reg, true);
    }

    /* s->trigger bitmap -> GICD_ICFGRn
     * (restore configuration registers before pending IRQs so we treat
     * level/edge correctly)
     */
    kvm_dist_put(s, GICD_ICFGR, 2, translate_edge_trigger);

    /* s->level bitmap ->  line_level */
    kvm_gic_put_line_level_bmp(s, s->level);

    /* s->pending bitmap -> GICD_ISPENDRn */
    kvm_dist_putbmp(s, GICD_ISPENDR, GICD_ICPENDR, s->pending);

    /* s->active bitmap -> GICD_ISACTIVERn */
    kvm_dist_putbmp(s, GICD_ISACTIVER, GICD_ICACTIVER, s->active);

    /* s->gicd_ipriority[] -> GICD_IPRIORITYRn */
    kvm_dist_put(s, GICD_IPRIORITYR, 8, translate_priority);

    /* CPU Interface state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        kvm_gicc_access(s, ICC_CTLR_EL1, ncpu,
                        &c->icc_ctlr_el1[GICV3_NS], true);
        kvm_gicc_access(s, ICC_IGRPEN0_EL1, ncpu,
                        &c->icc_igrpen[GICV3_G0], true);
        kvm_gicc_access(s, ICC_IGRPEN1_EL1, ncpu,
                        &c->icc_igrpen[GICV3_G1NS], true);
        kvm_gicc_access(s, ICC_PMR_EL1, ncpu, &c->icc_pmr_el1, true);
        kvm_gicc_access(s, ICC_BPR0_EL1, ncpu, &c->icc_bpr[GICV3_G0], true);
        kvm_gicc_access(s, ICC_BPR1_EL1, ncpu, &c->icc_bpr[GICV3_G1NS], true);

        for (i = 0; i < 4; i++) {
            reg64 = c->icc_apr[GICV3_G0][i];
            kvm_gicc_access(s, ICC_AP0R_EL1(i), ncpu, &reg64, true);
        }

        for (i = 0; i < 4; i++) {
            reg64 = c->icc_apr[GICV3_G1NS][i];
            kvm_gicc_access(s, ICC_AP1R_EL1(i), ncpu, &reg64, true);
        }
    }
}

static void kvm_arm_gicv3_get(GICv3State *s)
{
    uint32_t regl, regh, reg;
    uint64_t reg64, redist_typer;
    int ncpu, i;

    kvm_arm_gicv3_check(s);

    kvm_gicr_access(s, GICR_TYPER, 0, &regl, false);
    kvm_gicr_access(s, GICR_TYPER + 4, 0, &regh, false);
    redist_typer = ((uint64_t)regh << 32) | regl;

    kvm_gicd_access(s, GICD_CTLR, &reg, false);
    s->gicd_ctlr = reg;

    /* Redistributor state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        kvm_gicr_access(s, GICR_CTLR, ncpu, &reg, false);
        c->gicr_ctlr = reg;

        kvm_gicr_access(s, GICR_STATUSR, ncpu, &reg, false);
        c->gicr_statusr[GICV3_NS] = reg;

        kvm_gicr_access(s, GICR_WAKER, ncpu, &reg, false);
        c->gicr_waker = reg;

        kvm_gicr_access(s, GICR_IGROUPR0, ncpu, &reg, false);
        c->gicr_igroupr0 = reg;
        kvm_gicr_access(s, GICR_ISENABLER0, ncpu, &reg, false);
        c->gicr_ienabler0 = reg;
        kvm_gicr_access(s, GICR_ICFGR1, ncpu, &reg, false);
        c->edge_trigger = half_unshuffle32(reg >> 1) << 16;
        kvm_gic_line_level_access(s, 0, ncpu, &reg, false);
        c->level = reg;
        kvm_gicr_access(s, GICR_ISPENDR0, ncpu, &reg, false);
        c->gicr_ipendr0 = reg;
        kvm_gicr_access(s, GICR_ISACTIVER0, ncpu, &reg, false);
        c->gicr_iactiver0 = reg;

        for (i = 0; i < GIC_INTERNAL; i += 4) {
            kvm_gicr_access(s, GICR_IPRIORITYR + i, ncpu, &reg, false);
            c->gicr_ipriorityr[i] = extract32(reg, 0, 8);
            c->gicr_ipriorityr[i + 1] = extract32(reg, 8, 8);
            c->gicr_ipriorityr[i + 2] = extract32(reg, 16, 8);
            c->gicr_ipriorityr[i + 3] = extract32(reg, 24, 8);
        }
    }

    if (redist_typer & GICR_TYPER_PLPIS) {
        for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
            GICv3CPUState *c = &s->cpu[ncpu];

            kvm_gicr_access(s, GICR_PROPBASER, ncpu, &regl, false);
            kvm_gicr_access(s, GICR_PROPBASER + 4, ncpu, &regh, false);
            c->gicr_propbaser = ((uint64_t)regh << 32) | regl;

            kvm_gicr_access(s, GICR_PENDBASER, ncpu, &regl, false);
            kvm_gicr_access(s, GICR_PENDBASER + 4, ncpu, &regh, false);
            c->gicr_pendbaser = ((uint64_t)regh << 32) | regl;
        }
    }

    /* Distributor state (shared between all CPUs */

    kvm_gicd_access(s, GICD_STATUSR, &reg, false);
    s->gicd_statusr[GICV3_NS] = reg;

    /* GICD_IGROUPRn -> s->group bitmap */
    kvm_dist_getbmp(s, GICD_IGROUPR, s->group);

    /* GICD_ISENABLERn -> s->enabled bitmap */
    kvm_dist_getbmp(s, GICD_ISENABLER, s->enabled);

    /* Line level of irq */
    kvm_gic_get_line_level_bmp(s, s->level);
    /* GICD_ISPENDRn -> s->pending bitmap */
    kvm_dist_getbmp(s, GICD_ISPENDR, s->pending);

    /* GICD_ISACTIVERn -> s->active bitmap */
    kvm_dist_getbmp(s, GICD_ISACTIVER, s->active);

    /* GICD_ICFGRn -> s->trigger bitmap */
    kvm_dist_get(s, GICD_ICFGR, 2, translate_edge_trigger);

    /* GICD_IPRIORITYRn -> s->gicd_ipriority[] */
    kvm_dist_get(s, GICD_IPRIORITYR, 8, translate_priority);

    /* GICD_IROUTERn -> s->gicd_irouter[irq] */
    for (i = GIC_INTERNAL; i < s->num_irq; i++) {
        uint32_t offset;

        offset = GICD_IROUTER + (sizeof(uint32_t) * i);
        kvm_gicd_access(s, offset, &regl, false);
        offset = GICD_IROUTER + (sizeof(uint32_t) * i) + 4;
        kvm_gicd_access(s, offset, &regh, false);
        s->gicd_irouter[i] = ((uint64_t)regh << 32) | regl;
    }

    /*****************************************************************
     * CPU Interface(s) State
     */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        kvm_gicc_access(s, ICC_CTLR_EL1, ncpu,
                        &c->icc_ctlr_el1[GICV3_NS], false);
        kvm_gicc_access(s, ICC_IGRPEN0_EL1, ncpu,
                        &c->icc_igrpen[GICV3_G0], false);
        kvm_gicc_access(s, ICC_IGRPEN1_EL1, ncpu,
                        &c->icc_igrpen[GICV3_G1NS], false);
        kvm_gicc_access(s, ICC_PMR_EL1, ncpu, &c->icc_pmr_el1, false);
        kvm_gicc_access(s, ICC_BPR0_EL1, ncpu, &c->icc_bpr[GICV3_G0], false);
        kvm_gicc_access(s, ICC_BPR1_EL1, ncpu, &c->icc_bpr[GICV3_G1NS], false);

        for (i = 0; i < 4; i++) {
            kvm_gicc_access(s, ICC_AP0R_EL1(i), ncpu, &reg64, false);
            c->icc_apr[0][i] = reg64;
        }

        for (i = 0; i < 4; i++) {
            kvm_gicc_access(s, ICC_AP1R_EL1(i), ncpu, &reg64, false);
            c->icc_apr[1][i] = reg64;
        }
    }
}

static void kvm_arm_gicv3_reset(DeviceState *dev)
{
    GICv3State *s = ARM_GICV3_COMMON(dev);
    KVMARMGICv3Class *kgc = KVM_ARM_GICV3_GET_CLASS(s);

    DPRINTF("Reset\n");

    kgc->parent_reset(dev);

    if (s->migration_blocker) {
        DPRINTF("Cannot put kernel gic state, no kernel interface\n");
        return;
    }

    kvm_arm_gicv3_put(s);
}

static void kvm_arm_gicv3_realize(DeviceState *dev, Error **errp)
{
    GICv3State *s = KVM_ARM_GICV3(dev);
    KVMARMGICv3Class *kgc = KVM_ARM_GICV3_GET_CLASS(s);
    Error *local_err = NULL;

    DPRINTF("kvm_arm_gicv3_realize\n");

    kgc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (s->security_extn) {
        error_setg(errp, "the in-kernel VGICv3 does not implement the "
                   "security extensions");
        return;
    }

    gicv3_init_irqs_and_mmio(s, kvm_arm_gicv3_set_irq, NULL);

    /* Try to create the device via the device control API */
    s->dev_fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_ARM_VGIC_V3, false);
    if (s->dev_fd < 0) {
        error_setg_errno(errp, -s->dev_fd, "error creating in-kernel VGIC");
        return;
    }

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
                      0, &s->num_irq, true);

    /* Tell the kernel to complete VGIC initialization now */
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                      KVM_DEV_ARM_VGIC_CTRL_INIT, NULL, true);

    kvm_arm_register_device(&s->iomem_dist, -1, KVM_DEV_ARM_VGIC_GRP_ADDR,
                            KVM_VGIC_V3_ADDR_TYPE_DIST, s->dev_fd);
    kvm_arm_register_device(&s->iomem_redist, -1, KVM_DEV_ARM_VGIC_GRP_ADDR,
                            KVM_VGIC_V3_ADDR_TYPE_REDIST, s->dev_fd);

    if (!kvm_device_check_attr(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
                               GICD_CTLR)) {
        error_setg(&s->migration_blocker, "This operating system kernel does "
                                          "not support vGICv3 migration");
        migrate_add_blocker(s->migration_blocker);
    }
}

static void kvm_arm_gicv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMGICv3CommonClass *agcc = ARM_GICV3_COMMON_CLASS(klass);
    KVMARMGICv3Class *kgc = KVM_ARM_GICV3_CLASS(klass);

    agcc->pre_save = kvm_arm_gicv3_get;
    agcc->post_load = kvm_arm_gicv3_put;
    kgc->parent_realize = dc->realize;
    kgc->parent_reset = dc->reset;
    dc->realize = kvm_arm_gicv3_realize;
    dc->reset = kvm_arm_gicv3_reset;
}

static const TypeInfo kvm_arm_gicv3_info = {
    .name = TYPE_KVM_ARM_GICV3,
    .parent = TYPE_ARM_GICV3_COMMON,
    .instance_size = sizeof(GICv3State),
    .class_init = kvm_arm_gicv3_class_init,
    .class_size = sizeof(KVMARMGICv3Class),
};

static void kvm_arm_gicv3_register_types(void)
{
    type_register_static(&kvm_arm_gicv3_info);
}

type_init(kvm_arm_gicv3_register_types)
