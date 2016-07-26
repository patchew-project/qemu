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
    KVM_DEV_ARM_VGIC_SYSREG(0b11, 0b000, 0b0100, 0b0110, 0b000)
#define ICC_BPR0_EL1    \
    KVM_DEV_ARM_VGIC_SYSREG(0b11, 0b000, 0b1100, 0b1000, 0b011)
#define ICC_APR0_EL1(n) \
    KVM_DEV_ARM_VGIC_SYSREG(0b11, 0b000, 0b1100, 0b1000, 0b100 | n)
#define ICC_APR1_EL1(n) \
    KVM_DEV_ARM_VGIC_SYSREG(0b11, 0b000, 0b1100, 0b1001, 0b000 | n)
#define ICC_BPR1_EL1    \
    KVM_DEV_ARM_VGIC_SYSREG(0b11, 0b000, 0b1100, 0b1100, 0b011)
#define ICC_CTLR_EL1    \
    KVM_DEV_ARM_VGIC_SYSREG(0b11, 0b000, 0b1100, 0b1100, 0b100)
#define ICC_IGRPEN0_EL1 \
    KVM_DEV_ARM_VGIC_SYSREG(0b11, 0b000, 0b1100, 0b1100, 0b110)
#define ICC_IGRPEN1_EL1 \
    KVM_DEV_ARM_VGIC_SYSREG(0b11, 0b000, 0b1100, 0b1100, 0b111)

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

#define KVM_VGIC_ATTR(reg, cpuaff) \
    ((cpuaff << KVM_DEV_ARM_VGIC_CPUID_SHIFT) | (reg))

static inline void kvm_gicd_access(GICv3State *s, int offset, int cpu,
                                   uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
           KVM_VGIC_ATTR(offset, ((s->cpu[cpu].gicr_typer >> 32) & 0xffffffff)),
           val, write);
}

static inline void kvm_gicr_access(GICv3State *s, int offset, int cpu,
                                   uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_REDIST_REGS,
           KVM_VGIC_ATTR(offset, ((s->cpu[cpu].gicr_typer >> 32) & 0xffffffff)),
           val, write);
}

static inline void kvm_gicc_access(GICv3State *s, uint64_t reg, int cpu,
                                   uint64_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
           KVM_VGIC_ATTR(reg, ((s->cpu[cpu].gicr_typer >> 32) & 0xffffffff)),
           val, write);
}

/*
 * Translate from the in-kernel field for an IRQ value to/from the qemu
 * representation.
 */
typedef void (*vgic_translate_fn)(GICv3State *s, int irq, int cpu,
                                  uint32_t *field, bool to_kernel);

/* synthetic translate function used for clear/set registers to completely
 * clear a setting using a clear-register before setting the remaining bits
 * using a set-register */
static void translate_clear(GICv3State *s, int irq, int cpu,
                            uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = ~0;
    } else {
        /* does not make sense: qemu model doesn't use set/clear regs */
        abort();
    }
}

static void translate_enabled(GICv3State *s, int irq, int cpu,
                              uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = GIC_TEST_ENABLED(irq, cpu);
    } else {
        GIC_REPLACE_ENABLED(irq, cpu, *field);
    }
}

static void translate_group(GICv3State *s, int irq, int cpu,
                            uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = GIC_TEST_GROUP(irq, cpu);
    } else {
        GIC_REPLACE_GROUP(irq, cpu, *field);
    }
}

static void translate_trigger(GICv3State *s, int irq, int cpu,
                              uint32_t *field, bool to_kernel)
{
    bool val;
    if (to_kernel) {
        val = GIC_TEST_EDGE_TRIGGER(irq, cpu);
        *field = val ? 2 : 0;
    } else {
        GIC_REPLACE_EDGE_TRIGGER(irq, cpu, *field & 2);
    }
}

static void translate_pending(GICv3State *s, int irq, int cpu,
                              uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = gic_test_pending(s, irq, cpu);
    } else {
        GIC_REPLACE_PENDING(irq, cpu, *field);
        /* TODO: Capture if level-line is held high in the kernel */
    }
}

static void translate_active(GICv3State *s, int irq, int cpu,
                             uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = GIC_TEST_ACTIVE(irq, cpu);
    } else {
        GIC_REPLACE_ACTIVE(irq, cpu, *field);
    }
}

static void translate_priority(GICv3State *s, int irq, int cpu,
                               uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = GIC_GET_PRIORITY(irq, cpu);
    } else {
        GIC_SET_PRIORITY(irq, cpu, *field);
    }
}

#define for_each_irq_reg(_irq, _max, _field_width) \
    for (_irq = 0; _irq < _max; _irq += (32 / _field_width))

/* Read a register group from the kernel VGIC */
static void kvm_dist_get(GICv3State *s, uint32_t offset, int width,
                         vgic_translate_fn translate_fn)
{
    uint32_t reg;
    int j;
    int irq, cpu, maxcpu;
    uint32_t field;
    int regsz = 32 / width; /* irqs per kernel register */

    for_each_irq_reg(irq, s->num_irq, width) {
        maxcpu = irq < GIC_INTERNAL ? s->num_cpu : 1;
        for (cpu = 0; cpu < maxcpu; cpu++) {
            /* In GICv3 SGI/PPIs are stored in redistributor
             * Offsets in SGI area are the same as in distributor
             */
            if (irq < GIC_INTERNAL) {
                kvm_gicr_access(s, offset, cpu, &reg, false);
            } else {
                kvm_gicd_access(s, offset, cpu, &reg, false);
            }
            for (j = 0; j < regsz; j++) {
                field = extract32(reg, j * width, width);
                translate_fn(s, irq + j, cpu, &field, false);
            }
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
    int irq, cpu, maxcpu;
    uint32_t field;
    int regsz = 32 / width; /* irqs per kernel register */

    for_each_irq_reg(irq, s->num_irq, width) {
        maxcpu = irq < GIC_INTERNAL ? s->num_cpu : 1;
        for (cpu = 0; cpu < maxcpu; cpu++) {
            reg = 0;
            for (j = 0; j < regsz; j++) {
                translate_fn(s, irq + j, cpu, &field, true);
                reg = deposit32(reg, j * width, width, field);
            }
            /* In GICv3 SGI/PPIs are stored in redistributor
             * Offsets in SGI area are the same as in distributor
             */
            if (irq < GIC_INTERNAL) {
                kvm_gicr_access(s, offset, cpu, &reg, true);
            } else {
                kvm_gicd_access(s, offset, cpu, &reg, true);
            }
        }
        offset += 4;
    }
}

static void kvm_arm_gicv3_check(GICv3State *s)
{
    uint32_t reg;
    uint32_t num_irq;

    /* Sanity checking s->num_irq */
    kvm_gicd_access(s, GICD_TYPER, 0, &reg, false);
    num_irq = ((reg & 0x1f) + 1) * 32;

    if (num_irq < s->num_irq) {
        error_report("Model requests %u IRQs, but kernel supports max %u\n",
                     s->num_irq, num_irq);
        abort();
    }

    /* TODO: Consider checking compatibility with the IIDR ? */
}

static void kvm_arm_gicv3_put(GICv3State *s)
{
    uint32_t regl, regh, reg32;
    uint64_t reg64, redist_typer;
    int ncpu, i;

    kvm_arm_gicv3_check(s);

    kvm_gicr_access(s, GICR_TYPER, 0, &regl, false);
    kvm_gicr_access(s, GICR_TYPER + 4, 0, &regh, false);

    redist_typer = ((uint64_t)regh << 32) | regl;
    /*
     * (Re)distributor State
     */

    reg32 = s->gicd_ctlr;
    kvm_gicd_access(s, GICD_CTLR, 0, &reg32, true);

    if (redist_typer & GICR_TYPER_PLPIS) {
        /* Set base addresses before LPIs are enabled by GICR_CTLR write */
        for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
            GICv3CPUState *c = &s->cpu[ncpu];

            reg64 = c->gicr_propbaser &
                                 (GICR_PROPBASER_OUTER_CACHEABILITY_MASK |
                                  GICR_PROPBASER_ADDR_MASK |
                                  GICR_PROPBASER_SHAREABILITY_MASK |
                                  GICR_PROPBASER_CACHEABILITY_MASK |
                                  GICR_PROPBASER_IDBITS_MASK);
            regl = (uint32_t)reg64;
            kvm_gicr_access(s, GICR_PROPBASER, ncpu, &regl, true);
            regh = (uint32_t)(reg64 >> 32);
            kvm_gicr_access(s, GICR_PROPBASER + 4, ncpu, &regh, true);

            reg64 = c->gicr_pendbaser &
                                 (GICR_PENDBASER_OUTER_CACHEABILITY_MASK |
                                  GICR_PENDBASER_ADDR_MASK |
                                  GICR_PENDBASER_SHAREABILITY_MASK |
                                  GICR_PENDBASER_CACHEABILITY_MASK);
            if (!c->gicr_ctlr & GICR_CTLR_ENABLE_LPIS) {
                reg64 |= GICR_PENDBASER_PTZ;
            }
            regl = (uint32_t)reg64;
            kvm_gicr_access(s, GICR_PENDBASER, ncpu, &regl, true);
            regh = (uint32_t)(reg64 >> 32);
            kvm_gicr_access(s, GICR_PENDBASER + 4, ncpu, &regh, true);
        }
    }

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        reg32 = c->gicr_ctlr & (GICR_CTLR_ENABLE_LPIS | GICR_CTLR_DPG0 |
                                GICR_CTLR_DPG1NS | GICR_CTLR_DPG1S);
        kvm_gicr_access(s, GICR_CTLR, ncpu, &reg32, true);

        reg32 = c->cpu_enabled ? 0 : GICR_WAKER_ProcessorSleep;
        kvm_gicr_access(s, GICR_WAKER, ncpu, &reg32, true);
    }

    /* irq_state[n].enabled -> GICD_ISENABLERn */
    kvm_dist_put(s, GICD_ICENABLER, 1, translate_clear);
    kvm_dist_put(s, GICD_ISENABLER, 1, translate_enabled);

    /* irq_state[n].group -> GICD_IGROUPRn */
    kvm_dist_put(s, GICD_IGROUPR, 1, translate_group);

    /* Restore targets before pending to ensure the pending state is set on
     * the appropriate CPU interfaces in the kernel */

    /* s->route[irq] -> GICD_IROUTERn */
    for (i = GIC_INTERNAL; i < s->num_irq; i++) {
        uint32_t offset;

        offset = GICD_IROUTER + (sizeof(reg32) * i);
        reg32 = (uint32_t)s->gicd_irouter[i - GIC_INTERNAL];
        kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
            KVM_VGIC_ATTR(offset, ((s->cpu[0].gicr_typer >> 32) & 0xffffffff)),
            &reg32, true);
        offset = GICD_IROUTER + (sizeof(reg32) * i) + 4;
        reg32 = (uint32_t)(s->gicd_irouter[i - GIC_INTERNAL] >> 32);
        kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
            KVM_VGIC_ATTR(offset, ((s->cpu[0].gicr_typer >> 32) & 0xffffffff)),
            &reg32, true);
    }

    /* irq_state[n].trigger -> GICD_ICFGRn
     * (restore configuration registers before pending IRQs so we treat
     * level/edge correctly) */
    kvm_dist_put(s, GICD_ICFGR, 2, translate_trigger);

    /* irq_state[n].pending + irq_state[n].level -> GICD_ISPENDRn */
    kvm_dist_put(s, GICD_ICPENDR, 1, translate_clear);
    kvm_dist_put(s, GICD_ISPENDR, 1, translate_pending);

    /* irq_state[n].active -> GICD_ISACTIVERn */
    kvm_dist_put(s, GICD_ICACTIVER, 1, translate_clear);
    kvm_dist_put(s, GICD_ISACTIVER, 1, translate_active);

    /* s->priorityX[irq] -> ICD_IPRIORITYRn */
    kvm_dist_put(s, GICD_IPRIORITYR, 8, translate_priority);

    /*
     * CPU Interface(s) State
     */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        reg64 = c->icc_ctlr_el1[GICV3_NS] & (ICC_CTLR_EL1_CBPR |
                ICC_CTLR_EL1_EOIMODE | ICC_CTLR_EL1_PMHE);
        kvm_gicc_access(s, ICC_CTLR_EL1, ncpu, &reg64, true);

        reg64 = c->icc_igrpen[GICV3_G0];
        kvm_gicc_access(s, ICC_IGRPEN0_EL1, ncpu, &reg64, true);

        reg64 = c->icc_igrpen[GICV3_G1NS];
        kvm_gicc_access(s, ICC_IGRPEN1_EL1, ncpu, &reg64, true);

        reg64 = c->icc_pmr_el1;
        kvm_gicc_access(s, ICC_PMR_EL1, ncpu, &reg64, true);

        reg64 = c->icc_bpr[0];
        kvm_gicc_access(s, ICC_BPR0_EL1, ncpu, &reg64, true);

        reg64 = c->icc_bpr[1];
        kvm_gicc_access(s, ICC_BPR1_EL1, ncpu, &reg64, true);

        for (i = 0; i < 4; i++) {
            reg64 = c->icc_apr[i][0];
            kvm_gicc_access(s, ICC_APR0_EL1(i), ncpu, &reg64, true);
        }

        for (i = 0; i < 4; i++) {
            reg64 = c->icc_apr[i][1];
            kvm_gicc_access(s, ICC_APR1_EL1(i), ncpu, &reg64, true);
        }
    }
}

static void kvm_arm_gicv3_get(GICv3State *s)
{
    uint32_t regl, regh, reg32;
    uint64_t reg64, redist_typer;
    int ncpu, i;

    kvm_arm_gicv3_check(s);

    kvm_gicr_access(s, GICR_TYPER, 0, &regl, false);
    kvm_gicr_access(s, GICR_TYPER + 4, 0, &regh, false);

    redist_typer = ((uint64_t)regh << 32) | regl;
    /*
     * (Re)distributor State
     */

    /* GICD_CTLR -> s->ctlr */
    kvm_gicd_access(s, GICD_CTLR, 0, &reg32, false);
    s->gicd_ctlr = reg32;

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        kvm_gicr_access(s, GICR_CTLR, ncpu, &reg32, false);
        c->gicr_ctlr = reg32 & (GICR_CTLR_ENABLE_LPIS | GICR_CTLR_DPG0 |
                                GICR_CTLR_DPG1NS | GICR_CTLR_DPG1S);

        kvm_gicr_access(s, GICR_WAKER, ncpu, &reg32, false);
        c->cpu_enabled = !(reg32 & GICR_WAKER_ProcessorSleep);
    }

    if (redist_typer & GICR_TYPER_PLPIS) {
        for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
            GICv3CPUState *c = &s->cpu[ncpu];

            kvm_gicr_access(s, GICR_PROPBASER, ncpu, &regl, false);
            kvm_gicr_access(s, GICR_PROPBASER + 4, ncpu, &regh, false);
            reg64 = ((uint64_t)regh << 32) | regl;
            c->gicr_propbaser = reg64 &
                                 (GICR_PROPBASER_OUTER_CACHEABILITY_MASK |
                                  GICR_PROPBASER_ADDR_MASK |
                                  GICR_PROPBASER_SHAREABILITY_MASK |
                                  GICR_PROPBASER_CACHEABILITY_MASK |
                                  GICR_PROPBASER_IDBITS_MASK);

            kvm_gicr_access(s, GICR_PENDBASER, ncpu, &regl, false);
            kvm_gicr_access(s, GICR_PENDBASER + 4, ncpu, &regh, false);
            reg64 = ((uint64_t)regh << 32) | regl;
            c->gicr_pendbaser = reg64 &
                                 (GICR_PENDBASER_OUTER_CACHEABILITY_MASK |
                                  GICR_PENDBASER_ADDR_MASK |
                                  GICR_PENDBASER_SHAREABILITY_MASK |
                                  GICR_PENDBASER_CACHEABILITY_MASK);
        }
    }

    /* GICD_IIDR -> ? */
    /* kvm_gicd_access(s, GICD_IIDR, 0, &reg, false); */

    /* GICD_IGROUPRn -> irq_state[n].group */
    kvm_dist_get(s, GICD_IGROUPR, 1, translate_group);

    /* GICD_ISENABLERn -> irq_state[n].enabled */
    kvm_dist_get(s, GICD_ISENABLER, 1, translate_enabled);

    /* GICD_ISPENDRn -> irq_state[n].pending + irq_state[n].level */
    kvm_dist_get(s, GICD_ISPENDR, 1, translate_pending);

    /* GICD_ISACTIVERn -> irq_state[n].active */
    kvm_dist_get(s, GICD_ISACTIVER, 1, translate_active);

    /* GICD_ICFRn -> irq_state[n].trigger */
    kvm_dist_get(s, GICD_ICFGR, 2, translate_trigger);

    /* GICD_IPRIORITYRn -> s->priorityX[irq] */
    kvm_dist_get(s, GICD_IPRIORITYR, 8, translate_priority);

    /* GICD_IROUTERn -> s->route[irq] */
    for (i = GIC_INTERNAL; i < s->num_irq; i++) {

        uint32_t offset;

        offset = GICD_IROUTER + (sizeof(reg32) * i);
        kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
            KVM_VGIC_ATTR(offset, ((s->cpu[0].gicr_typer >> 32) & 0xffffffff)),
            &regl, false);
        offset = GICD_IROUTER + (sizeof(reg32) * i) + 4;
        kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
            KVM_VGIC_ATTR(offset, ((s->cpu[0].gicr_typer >> 32) & 0xffffffff)),
            &regh, false);
        s->gicd_irouter[i - GIC_INTERNAL] = ((uint64_t)regh << 32) | regl;
    }

    /*
     * CPU Interface(s) State
     */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        kvm_gicc_access(s, ICC_CTLR_EL1, ncpu, &reg64, false);
        c->icc_ctlr_el1[GICV3_NS] = reg64 & (ICC_CTLR_EL1_CBPR |
                             ICC_CTLR_EL1_EOIMODE | ICC_CTLR_EL1_PMHE);

        kvm_gicc_access(s, ICC_IGRPEN0_EL1, ncpu, &reg64, false);
        c->icc_igrpen[GICV3_G0] = reg64;

        kvm_gicc_access(s, ICC_IGRPEN1_EL1, ncpu, &reg64, false);
        c->icc_igrpen[GICV3_G1NS] = reg64;

        kvm_gicc_access(s, ICC_PMR_EL1, ncpu, &reg64, false);
        c->icc_pmr_el1 = reg64 & ICC_PMR_PRIORITY_MASK;

        kvm_gicc_access(s, ICC_BPR0_EL1, ncpu, &reg64, false);
        c->icc_bpr[0] = reg64 & ICC_BPR_BINARYPOINT_MASK;

        kvm_gicc_access(s, ICC_BPR1_EL1, ncpu, &reg64, false);
        c->icc_bpr[1] = reg64 & ICC_BPR_BINARYPOINT_MASK;

        for (i = 0; i < 4; i++) {
            kvm_gicc_access(s, ICC_APR0_EL1(i), ncpu, &reg64, false);
            c->icc_apr[i][0] = reg64;
        }

        for (i = 0; i < 4; i++) {
            kvm_gicc_access(s, ICC_APR1_EL1(i), ncpu, &reg64, false);
            c->icc_apr[i][1] = reg64;
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

    /* Block migration of a KVM GICv3 device: the API for saving and restoring
     * the state in the kernel is not yet finalised in the kernel or
     * implemented in QEMU.
     */

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
