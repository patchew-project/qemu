/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU TriCore Interrupt Router (IR)
 *
 * Copyright (c) 2017 David Brenken <david.brenken@efs-auto.de>
 * Copyright (c) 2026 Parthiban Nallathambi <parthiban@linumiz.com>
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/registerfields.h"
#include "hw/core/sysbus.h"
#include "hw/intc/tricore_ir.h"
#include "qemu/log.h"

static void irq_evaluate(void *opaque)
{
    TriCoreIRState *s = opaque;
    uint16_t tos_irq[8] = {
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
    };
    uint8_t tos_priority[8] = { 0 };
    uint32_t srcnum;
    uint8_t tos_idx;

    for (srcnum = 0; srcnum < s->num_irqs; srcnum++) {
        uint32_t src_reg = s->src_regs[srcnum];
        uint8_t priority = FIELD_EX32(src_reg, SRC, SRPN);
        uint8_t tos = FIELD_EX32(src_reg, SRC_TC3X, TOS);

        if ((src_reg & R_SRC_SRR_MASK) &&
            (src_reg & R_SRC_TC3X_SRE_MASK)) {
            if (qemu_loglevel_mask(CPU_LOG_INT)) {
                qemu_log("tricore_ir: pending irq #%u"
                         " (priority %u, TOS %u)\n",
                         srcnum, priority, tos);
            }
            tos_priority[tos] = priority;
            tos_irq[tos] = srcnum;
        }
    }

    for (tos_idx = 0; tos_idx < s->num_isps; tos_idx++) {
        if (tos_irq[tos_idx] == 0xFFFF) {
            s->lwsr[tos_idx] = 0;
            qemu_irq_lower(s->isp_irqs[tos_idx]);
        } else {
            s->lwsr[tos_idx] =
                FIELD_DP32(0, LWSR, STAT, 1) |
                FIELD_DP32(0, LWSR, ID, tos_irq[tos_idx]) |
                FIELD_DP32(0, LWSR, VALID, 1) |
                FIELD_DP32(0, LWSR, PN, tos_priority[tos_idx]);

            if (qemu_loglevel_mask(CPU_LOG_INT)) {
                qemu_log("tricore_ir: raise TOS %u irq line"
                         " (irq: %u, priority: %u)\n",
                         tos_idx, tos_irq[tos_idx],
                         tos_priority[tos_idx]);
            }
            qemu_irq_raise(s->isp_irqs[tos_idx]);
        }
    }
}

static void irq_handler(void *opaque, int srcnum, int level)
{
    TriCoreIRState *s = opaque;
    uint32_t src_reg = s->src_regs[srcnum];

    if (level) {
        if (src_reg & R_SRC_SRR_MASK) {
            src_reg |= R_SRC_IOV_MASK;
        }
        src_reg |= R_SRC_SRR_MASK;
    }
    s->src_regs[srcnum] = src_reg;

    irq_evaluate(opaque);
}

void tricore_ir_irq_acknowledge(TriCoreIRState *s, uint16_t irq, uint8_t vm)
{
    uint32_t src_reg;

    if (!s || irq >= s->num_irqs || vm >= ARRAY_SIZE(s->lwsr)) {
        return;
    }

    /* Capture LWSR into LASR before clearing */
    s->lasr = s->lwsr[vm] | FIELD_DP32(0, LASR, ENTER, 1);

    src_reg = s->src_regs[irq];
    src_reg &= ~R_SRC_SRR_MASK;
    s->src_regs[irq] = src_reg;

    if (FIELD_EX32(s->lwsr[vm], LWSR, ID) == irq) {
        irq_evaluate(s);
    }
}

static uint64_t tricore_ir_src_regs_read(void *opaque, hwaddr offset,
                                         unsigned size)
{
    TriCoreIRState *s = opaque;
    hwaddr srcnum = offset >> 2;

    if (srcnum < s->num_irqs) {
        return s->src_regs[srcnum];
    }

    return 0;
}

static void tricore_ir_src_regs_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size)
{
    TriCoreIRState *s = opaque;
    hwaddr srcnum = offset >> 2;
    uint32_t srcc;
    bool setr;
    bool clrr;

    if (srcnum >= s->num_irqs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tricore_ir: write to unmapped SRC offset"
                      " 0x%" HWADDR_PRIx "\n", offset);
        return;
    }

    srcc = value & ~(R_SRC_SETR_MASK | R_SRC_CLRR_MASK);
    setr = value & R_SRC_SETR_MASK;
    clrr = value & R_SRC_CLRR_MASK;

    if (setr && !clrr) {
        srcc |= R_SRC_SRR_MASK;
    } else if (clrr && !setr) {
        srcc &= ~R_SRC_SRR_MASK;
    }

    s->src_regs[srcnum] = srcc;

    irq_evaluate(opaque);
}

static const MemoryRegionOps tricore_ir_src_regs_ops = {
    .read = tricore_ir_src_regs_read,
    .write = tricore_ir_src_regs_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*
 * IR INT registers (LWSR, LASR)
 *
 * TC3x LWSR offset: 0x200 + x * 0x10 (x = CPU index)
 * TC3x LASR offset: 0x200 + x * 0x10 + 4
 */
#define IR_INT_LWSR_BASE    0x200
#define IR_INT_CPU_STRIDE   0x10
#define IR_INT_ID_OFF       0x08

/* Module ID: IR module number 0x00B9, type 0xC0, rev 0x13 */
#define IR_INT_MOD_ID       0x00B9C013

static uint64_t tricore_ir_intregs_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    TriCoreIRState *s = opaque;
    uint32_t x;
    uint32_t sub;

    if (offset == IR_INT_ID_OFF) {
        return IR_INT_MOD_ID;
    }

    /* TC3x LWSR: 0x200 + x*0x10, LASR: 0x200 + x*0x10 + 4 */
    if (offset >= IR_INT_LWSR_BASE &&
        offset < IR_INT_LWSR_BASE + IR_INT_CPU_STRIDE * 8) {
        x = (offset - IR_INT_LWSR_BASE) / IR_INT_CPU_STRIDE;
        sub = (offset - IR_INT_LWSR_BASE) % IR_INT_CPU_STRIDE;
        if (sub == 0 && x < 8) {
            return s->lwsr[x];
        }
        if (sub == 4 && x < 8) {
            return s->lasr;
        }
    }

    return 0;
}

static void tricore_ir_intregs_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    /* LWSR/LASR are read-only from software side */
}

static const MemoryRegionOps tricore_ir_intregs_ops = {
    .read = tricore_ir_intregs_read,
    .write = tricore_ir_intregs_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void tricore_ir_init(Object *obj)
{
    TriCoreIRState *s = TRICORE_IR(obj);

    memory_region_init_io(&s->src_region, obj, &tricore_ir_src_regs_ops,
                          s, "tricore_ir.src", 0x4000);
    memory_region_init_io(&s->int_region, obj, &tricore_ir_intregs_ops,
                          s, "tricore_ir.int", 0x1000);
}

static void tricore_ir_realize(DeviceState *dev, Error **errp)
{
    TriCoreIRState *s = TRICORE_IR(dev);

    s->src_regs = g_malloc0_n(s->num_irqs, sizeof(uint32_t));
    s->isp_irqs = g_malloc_n(s->num_isps, sizeof(qemu_irq));

    qdev_init_gpio_in_named(DEVICE(s), irq_handler, "irq", s->num_irqs);
    qdev_init_gpio_out_named(DEVICE(s), s->isp_irqs, "isp", s->num_isps);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->int_region);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->src_region);
}

static const Property tricore_ir_properties[] = {
    DEFINE_PROP_UINT8("num-isps", TriCoreIRState, num_isps, 1),
    DEFINE_PROP_UINT16("num-irqs", TriCoreIRState, num_irqs, 256),
};

static void tricore_ir_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->user_creatable = false;
    dc->realize = tricore_ir_realize;
    device_class_set_props(dc, tricore_ir_properties);
}

static const TypeInfo tricore_ir_info = {
    .name = TYPE_TRICORE_IR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TriCoreIRState),
    .instance_init = tricore_ir_init,
    .class_init = tricore_ir_class_init,
};

static void tricore_ir_register(void)
{
    type_register_static(&tricore_ir_info);
}

type_init(tricore_ir_register)
