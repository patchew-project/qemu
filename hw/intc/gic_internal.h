/*
 * ARM GIC support - internal interfaces
 *
 * Copyright (c) 2012 Linaro Limited
 * Written by Peter Maydell
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

#ifndef QEMU_ARM_GIC_INTERNAL_H
#define QEMU_ARM_GIC_INTERNAL_H

#include "hw/intc/arm_gic.h"

#define ALL_CPU_MASK ((unsigned)(((1 << GIC_NCPU) - 1)))

#define GIC_BASE_IRQ 0

#define GIC_DIST_SET_ENABLED(irq, cm) (s->irq_state[irq].enabled |= (cm))
#define GIC_DIST_CLEAR_ENABLED(irq, cm) (s->irq_state[irq].enabled &= ~(cm))
#define GIC_DIST_TEST_ENABLED(irq, cm) ((s->irq_state[irq].enabled & (cm)) != 0)
#define GIC_DIST_SET_PENDING(irq, cm) (s->irq_state[irq].pending |= (cm))
#define GIC_DIST_CLEAR_PENDING(irq, cm) (s->irq_state[irq].pending &= ~(cm))
#define GIC_DIST_SET_ACTIVE(irq, cm) (s->irq_state[irq].active |= (cm))
#define GIC_DIST_CLEAR_ACTIVE(irq, cm) (s->irq_state[irq].active &= ~(cm))
#define GIC_DIST_TEST_ACTIVE(irq, cm) ((s->irq_state[irq].active & (cm)) != 0)
#define GIC_DIST_SET_MODEL(irq) (s->irq_state[irq].model = true)
#define GIC_DIST_CLEAR_MODEL(irq) (s->irq_state[irq].model = false)
#define GIC_DIST_TEST_MODEL(irq) (s->irq_state[irq].model)
#define GIC_DIST_SET_LEVEL(irq, cm) (s->irq_state[irq].level |= (cm))
#define GIC_DIST_CLEAR_LEVEL(irq, cm) (s->irq_state[irq].level &= ~(cm))
#define GIC_DIST_TEST_LEVEL(irq, cm) ((s->irq_state[irq].level & (cm)) != 0)
#define GIC_DIST_SET_EDGE_TRIGGER(irq) (s->irq_state[irq].edge_trigger = true)
#define GIC_DIST_CLEAR_EDGE_TRIGGER(irq) \
    (s->irq_state[irq].edge_trigger = false)
#define GIC_DIST_TEST_EDGE_TRIGGER(irq) (s->irq_state[irq].edge_trigger)
#define GIC_DIST_GET_PRIORITY(irq, cpu) (((irq) < GIC_INTERNAL) ?            \
                                    s->priority1[irq][cpu] :            \
                                    s->priority2[(irq) - GIC_INTERNAL])
#define GIC_DIST_TARGET(irq) (s->irq_target[irq])
#define GIC_DIST_CLEAR_GROUP(irq, cm) (s->irq_state[irq].group &= ~(cm))
#define GIC_DIST_SET_GROUP(irq, cm) (s->irq_state[irq].group |= (cm))
#define GIC_DIST_TEST_GROUP(irq, cm) ((s->irq_state[irq].group & (cm)) != 0)

#define GICD_CTLR_EN_GRP0 (1U << 0)
#define GICD_CTLR_EN_GRP1 (1U << 1)

#define GICC_CTLR_EN_GRP0    (1U << 0)
#define GICC_CTLR_EN_GRP1    (1U << 1)
#define GICC_CTLR_ACK_CTL    (1U << 2)
#define GICC_CTLR_FIQ_EN     (1U << 3)
#define GICC_CTLR_CBPR       (1U << 4) /* GICv1: SBPR */
#define GICC_CTLR_EOIMODE    (1U << 9)
#define GICC_CTLR_EOIMODE_NS (1U << 10)

#define GICH_HCR_EN       (1U << 0)
#define GICH_HCR_UIE      (1U << 1)
#define GICH_HCR_LRENPIE  (1U << 2)
#define GICH_HCR_NPIE     (1U << 3)
#define GICH_HCR_VGRP0EIE (1U << 4)
#define GICH_HCR_VGRP0DIE (1U << 5)
#define GICH_HCR_VGRP1EIE (1U << 6)
#define GICH_HCR_VGRP1DIE (1U << 7)
#define GICH_HCR_EOICOUNT (0x1fU << 27)

#define GICH_LR_STATE_INVALID         0
#define GICH_LR_STATE_PENDING         1
#define GICH_LR_STATE_ACTIVE          2
#define GICH_LR_STATE_ACTIVE_PENDING  3

#define GICH_LR_VIRT_ID(entry) (extract32(entry, 0, 10))
#define GICH_LR_PHYS_ID(entry) (extract32(entry, 10, 10))
#define GICH_LR_PRIORITY(entry) (extract32(entry, 23, 5) << 3)
#define GICH_LR_STATE(entry) (extract32(entry, 28, 2))
#define GICH_LR_GROUP(entry) (extract32(entry, 30, 1))
#define GICH_LR_HW(entry) (extract32(entry, 31, 1))
#define GICH_LR_EOI(entry) (extract32(entry, 19, 1))
#define GICH_LR_CPUID(entry) (extract32(entry, 10, 3))

#define GICH_LR_CLEAR_PENDING(entry) ((entry) &= ~(1 << 28))
#define GICH_LR_SET_ACTIVE(entry) ((entry) |= (1 << 29))
#define GICH_LR_CLEAR_ACTIVE(entry) ((entry) &= ~(1 << 29))

/* Valid bits for GICC_CTLR for GICv1, v1 with security extensions,
 * GICv2 and GICv2 with security extensions:
 */
#define GICC_CTLR_V1_MASK    0x1
#define GICC_CTLR_V1_S_MASK  0x1f
#define GICC_CTLR_V2_MASK    0x21f
#define GICC_CTLR_V2_S_MASK  0x61f

/* The special cases for the revision property: */
#define REV_11MPCORE 0

uint32_t gic_acknowledge_irq(GICState *s, int cpu, MemTxAttrs attrs);
void gic_dist_set_priority(GICState *s, int cpu, int irq, uint8_t val,
                           MemTxAttrs attrs);

static inline bool gic_test_pending(GICState *s, int irq, int cm)
{
    if (s->revision == REV_11MPCORE) {
        return s->irq_state[irq].pending & cm;
    } else {
        /* Edge-triggered interrupts are marked pending on a rising edge, but
         * level-triggered interrupts are either considered pending when the
         * level is active or if software has explicitly written to
         * GICD_ISPENDR to set the state pending.
         */
        return (s->irq_state[irq].pending & cm) ||
            (!GIC_DIST_TEST_EDGE_TRIGGER(irq) && GIC_DIST_TEST_LEVEL(irq, cm));
    }
}

static inline bool gic_is_vcpu(int cpu)
{
    return cpu >= GIC_NCPU;
}

static inline int gic_get_vcpu_real_id(int cpu)
{
    return (cpu >= GIC_NCPU) ? (cpu - GIC_NCPU) : cpu;
}

static inline bool gic_lr_entry_is_free(uint32_t entry)
{
    return (GICH_LR_STATE(entry) == GICH_LR_STATE_INVALID)
        && (GICH_LR_HW(entry) || !GICH_LR_EOI(entry));
}

static inline bool gic_lr_entry_is_eoi(uint32_t entry)
{
    return (GICH_LR_STATE(entry) == GICH_LR_STATE_INVALID)
        && !GICH_LR_HW(entry) && GICH_LR_EOI(entry);
}

static inline bool gic_virq_is_valid(GICState *s, int irq, int vcpu)
{
    int cpu = gic_get_vcpu_real_id(vcpu);
    return s->virq_lr_entry[irq][cpu] != GIC_NR_LR;
}

/* Return a pointer on the LR entry for a given (irq,vcpu) couple.
 * This function requires that the entry actually exists somewhere in
 * the LRs. */
static inline uint32_t *gic_get_lr_entry(GICState *s, int irq, int vcpu)
{
    int cpu = gic_get_vcpu_real_id(vcpu);
    int lr_num = s->virq_lr_entry[irq][cpu];

    assert(lr_num >= 0 && lr_num < GIC_NR_LR);
    return &s->h_lr[lr_num][cpu];
}

static inline void gic_set_virq_cache(GICState *s, int irq,
                                      int vcpu, int lr_num)
{
    int cpu = gic_get_vcpu_real_id(vcpu);
    s->virq_lr_entry[irq][cpu] = lr_num;
}

static inline void gic_clear_virq_cache(GICState *s, int irq, int vcpu)
{
    gic_set_virq_cache(s, irq, vcpu, GIC_NR_LR);
}

static inline bool gic_lr_update(GICState *s, int lr_num, int vcpu)
{
    int cpu = gic_get_vcpu_real_id(vcpu);

    assert(lr_num >= 0 && lr_num < GIC_NR_LR);
    uint32_t *entry = &s->h_lr[lr_num][cpu];

    int is_eoi = gic_lr_entry_is_eoi(*entry);
    int is_free = gic_lr_entry_is_free(*entry);
    int is_pending = (GICH_LR_STATE(*entry) == GICH_LR_STATE_PENDING);

    s->h_eisr[cpu] = deposit64(s->h_eisr[cpu], lr_num, 1, is_eoi);
    s->h_elrsr[cpu] = deposit64(s->h_elrsr[cpu], lr_num, 1, is_free);
    s->pending_lrs[cpu] = deposit64(s->pending_lrs[cpu], lr_num, 1, is_pending);

    return is_free;
}

static inline void gic_lr_update_by_irq(GICState *s, int irq, int vcpu)
{
    int cpu = gic_get_vcpu_real_id(vcpu);
    int lr_num = s->virq_lr_entry[irq][cpu];

    assert(lr_num != GIC_NR_LR);
    bool is_free = gic_lr_update(s, lr_num, vcpu);

    if (is_free) {
        gic_clear_virq_cache(s, irq, vcpu);
    }
}

/* Recompute the whole virt cache, including the vIRQ to LR mapping, the EISR
 * and ELRSR registers, and the LRs in the pending state.
 * This function is called after restoring the GIC state from a VMState. */
static inline void gic_recompute_virt_cache(GICState *s)
{
    int cpu, lr_num;

    for (cpu = 0; cpu < s->num_cpu; cpu++) {
        for (lr_num = 0; lr_num < GIC_NR_LR; lr_num++) {
            bool is_free = gic_lr_update(s, lr_num, cpu);
            uint32_t entry = s->h_lr[lr_num][cpu];

            if (!is_free) {
                int irq = GICH_LR_VIRT_ID(entry);
                gic_set_virq_cache(s, irq, cpu, lr_num);
            }
        }
    }
}

static inline bool gic_test_group(GICState *s, int irq, int cpu)
{
    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        return GICH_LR_GROUP(*entry);
    } else {
        return GIC_DIST_TEST_GROUP(irq, 1 << cpu);
    }
}

static inline void gic_clear_pending(GICState *s, int irq, int cpu)
{
    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        GICH_LR_CLEAR_PENDING(*entry);
        /* Don't recompute the LR cache yet as a clear pending request is
         * always followed by a set active one. */
    } else {
        /* Clear pending state for both level and edge triggered
         * interrupts. (level triggered interrupts with an active line
         * remain pending, see gic_test_pending)
         */
        GIC_DIST_CLEAR_PENDING(irq, GIC_DIST_TEST_MODEL(irq) ? ALL_CPU_MASK
                                                             : (1 << cpu));
    }
}

static inline void gic_set_active(GICState *s, int irq, int cpu)
{
    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        GICH_LR_SET_ACTIVE(*entry);
        gic_lr_update_by_irq(s, irq, cpu);
    } else {
        GIC_DIST_SET_ACTIVE(irq, 1 << cpu);
    }
}

static inline void gic_clear_active(GICState *s, int irq, int cpu)
{
    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        GICH_LR_CLEAR_ACTIVE(*entry);

        if (GICH_LR_HW(*entry)) {
            /* Hardware interrupt. We must forward the deactivation request to
             * the distributor */
            int phys_irq = GICH_LR_PHYS_ID(*entry);
            int rcpu = gic_get_vcpu_real_id(cpu);

            /* Group 0 IRQs deactivation requests are ignored. */
            if (GIC_DIST_TEST_GROUP(phys_irq, 1 << rcpu)) {
                GIC_DIST_CLEAR_ACTIVE(phys_irq, 1 << rcpu);
            }
        }

        gic_lr_update_by_irq(s, irq, cpu);
    } else {
        GIC_DIST_CLEAR_ACTIVE(irq, 1 << cpu);
    }
}

static inline int gic_get_priority(GICState *s, int irq, int cpu)
{
    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        return GICH_LR_PRIORITY(*entry);
    } else {
        return GIC_DIST_GET_PRIORITY(irq, cpu);
    }
}

#endif /* QEMU_ARM_GIC_INTERNAL_H */
