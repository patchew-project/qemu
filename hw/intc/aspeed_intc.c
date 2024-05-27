/*
 * ASPEED INTC Controller
 *
 * Copyright (C) 2024 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/intc/aspeed_intc.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/intc/arm_gicv3.h"
#include "trace.h"
#include "hw/registerfields.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"

/* INTC Registers */
REG32(GICINT128_EN,         0x1000)
REG32(GICINT128_STATUS,     0x1004)
REG32(GICINT129_EN,         0x1100)
REG32(GICINT129_STATUS,     0x1104)
REG32(GICINT130_EN,         0x1200)
REG32(GICINT130_STATUS,     0x1204)
REG32(GICINT131_EN,         0x1300)
REG32(GICINT131_STATUS,     0x1304)
REG32(GICINT132_EN,         0x1400)
REG32(GICINT132_STATUS,     0x1404)
REG32(GICINT133_EN,         0x1500)
REG32(GICINT133_STATUS,     0x1504)
REG32(GICINT134_EN,         0x1600)
REG32(GICINT134_STATUS,     0x1604)
REG32(GICINT135_EN,         0x1700)
REG32(GICINT135_STATUS,     0x1704)
REG32(GICINT136_EN,         0x1800)
REG32(GICINT136_STATUS,     0x1804)

#define GICINT_STATUS_BASE     R_GICINT128_STATUS

static void aspeed_intc_update(AspeedINTCState *s, int irq, int level)
{
    trace_aspeed_intc_update_irq(irq, level);
    qemu_set_irq(s->output_pins[irq], level);
}

/*
 * The address of GICINT128 to GICINT136 are from 0x1000 to 0x1804.
 * Utilize "address & 0x0f00" to get the irq and irq output pin index
 * The value of irq should be 0 to num_ints.
 * The irq 0 indicates GICINT128, irq 1 indicates GICINT129 and so on.
 */
static void aspeed_intc_set_irq(void *opaque, int irq, int level)
{
    AspeedINTCState *s = (AspeedINTCState *)opaque;
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    uint32_t status_addr = GICINT_STATUS_BASE + ((0x100 * irq) >> 2);
    uint32_t enable = s->enable[irq];
    uint32_t select = 0;
    int i;

    if (irq > aic->num_ints) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid interrupt number: %d\n",
                      __func__, irq);
        return;
    }

    trace_aspeed_intc_set_irq(irq, level);

    if (!level) {
        return;
    }

    for (i = 0; i < aic->num_lines; i++) {
        if (s->orgates[irq].levels[i]) {
            if (enable & BIT(i)) {
                select |= BIT(i);
            }
        }
    }

    if (!select) {
        return;
    }

    trace_aspeed_intc_debug("select", select);
    trace_aspeed_intc_debug("mask", s->mask[irq]);
    trace_aspeed_intc_debug("status", s->regs[status_addr]);
    if (s->mask[irq] || s->regs[status_addr]) {
        /*
         * a. mask is not 0 means in ISR mode
         * sources interrupt routine are executing.
         * b. status register value is not 0 means previous
         * source interrupt does not be executed, yet.
         *
         * save source interrupt to pending variable.
         */
        s->pending[irq] |= select;
        trace_aspeed_intc_debug("pending source interrupt", s->pending[irq]);
    } else {
        /*
         * notify firmware which source interrupt are coming
         * by setting status register
         */
        s->regs[status_addr] = select;
        trace_aspeed_intc_debug("trigger source interrupt",
                                s->regs[status_addr]);
        aspeed_intc_update(s, irq, 1);
    }
}

static uint64_t aspeed_intc_read(void *opaque, hwaddr offset, unsigned int size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    uint32_t addr = offset >> 2;
    uint32_t value = 0;

    if (addr >= ASPEED_INTC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    value = s->regs[addr];
    trace_aspeed_intc_read(offset, size, value);

    return value;
}

static void aspeed_intc_write(void *opaque, hwaddr offset, uint64_t data,
                                        unsigned size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    uint32_t irq = (offset & 0x0f00) >> 8;
    uint32_t addr = offset >> 2;
    uint32_t old_enable;
    uint32_t change;


    if (addr >= ASPEED_INTC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    trace_aspeed_intc_write(offset, size, data);

    switch (addr) {
    case R_GICINT128_EN:
    case R_GICINT129_EN:
    case R_GICINT130_EN:
    case R_GICINT131_EN:
    case R_GICINT132_EN:
    case R_GICINT133_EN:
    case R_GICINT134_EN:
    case R_GICINT135_EN:
    case R_GICINT136_EN:
        /*
         * These registers are used for enable sources interrupt and
         * mask and unmask source interrupt while executing source ISR.
         */

        /* disable all source interrupt */
        if (!data && !s->enable[irq]) {
            s->regs[addr] = data;
            trace_aspeed_intc_debug("disable all source interrupt", irq);
            return;
        }

        old_enable = s->enable[irq];
        s->enable[irq] |= data;

        /* enable new source interrupt */
        if (old_enable != s->enable[irq]) {
            trace_aspeed_intc_debug("enable new source interrupt",
                                    s->enable[irq]);
            s->regs[addr] = data;
            return;
        }

        /* mask and unmask source interrupt */
        change = s->regs[addr] ^ data;
        trace_aspeed_intc_debug("enable change bit", change);
        if (change & data) {
            s->mask[irq] &= ~change;
            trace_aspeed_intc_debug("enable umask", s->mask[irq]);
        } else {
            s->mask[irq] |= change;
            trace_aspeed_intc_debug("enable mask", s->mask[irq]);
        }
        s->regs[addr] = data;
        break;
    case R_GICINT128_STATUS:
    case R_GICINT129_STATUS:
    case R_GICINT130_STATUS:
    case R_GICINT131_STATUS:
    case R_GICINT132_STATUS:
    case R_GICINT133_STATUS:
    case R_GICINT134_STATUS:
    case R_GICINT135_STATUS:
    case R_GICINT136_STATUS:
        /* clear status */
        s->regs[addr] &= ~data;

        /*
         * These status registers are used for notify sources ISR are executed.
         * If one source ISR is executed, it will clear one bit.
         * If it clear all bits, it means to initialize this register status
         * rather than sources ISR are executed.
         */
        if (data == 0xffffffff) {
            trace_aspeed_intc_debug("clear all source interrupt status",
                                    s->regs[addr]);
            return;
        }

        /* All source ISR execution are done */
        if (!s->regs[addr]) {
            trace_aspeed_intc_debug("all source ISR execution are done",
                                    s->regs[addr]);
            if (s->pending[irq]) {
                /*
                 * handle pending source interrupt
                 * notify firmware which source interrupt are pending
                 * by setting status register
                 */
                s->regs[addr] = s->pending[irq];
                s->pending[irq] = 0;
                trace_aspeed_intc_debug("trigger pending interrupt",
                                        s->regs[addr]);
                aspeed_intc_update(s, irq, 1);
            } else {
                /* clear irq */
                aspeed_intc_update(s, irq, 0);
            }
        }
        break;
    default:
        s->regs[addr] = data;
        break;
    }

    return;
}

static const MemoryRegionOps aspeed_intc_ops = {
    .read = aspeed_intc_read,
    .write = aspeed_intc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void aspeed_intc_instance_init(Object *obj)
{
    AspeedINTCState *s = ASPEED_INTC(obj);
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    int i;

    for (i = 0; i < aic->num_ints; i++) {
        object_initialize_child(obj, "intc-orgates[*]", &s->orgates[i],
                                TYPE_OR_IRQ);
        object_property_set_int(OBJECT(&s->orgates[i]), "num-lines",
                                aic->num_lines, &error_abort);
    }

    s->num_ints = aic->num_ints;
}

static void aspeed_intc_reset(DeviceState *dev)
{
    AspeedINTCState *s = ASPEED_INTC(dev);

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->enable, 0, sizeof(s->enable));
    memset(s->mask, 0, sizeof(s->mask));
    memset(s->pending, 0, sizeof(s->pending));
}

static void aspeed_intc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedINTCState *s = ASPEED_INTC(dev);
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    int i;

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_intc_ops, s,
                          TYPE_ASPEED_INTC ".regs", ASPEED_INTC_NR_REGS << 2);

    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(dev, aspeed_intc_set_irq, aic->num_ints);

    for (i = 0; i < aic->num_ints; i++) {
        if (!qdev_realize(DEVICE(&s->orgates[i]), NULL, errp)) {
            return;
        }
        sysbus_init_irq(sbd, &s->output_pins[i]);
    }
}

static Property aspeed_intc_properties[] = {
    DEFINE_PROP_UINT32("num-ints", AspeedINTCState, num_ints, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_intc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "ASPEED INTC Controller";
    dc->realize = aspeed_intc_realize;
    dc->reset = aspeed_intc_reset;
    device_class_set_props(dc, aspeed_intc_properties);
    dc->vmsd = NULL;
}

static const TypeInfo aspeed_intc_info = {
    .name = TYPE_ASPEED_INTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = aspeed_intc_instance_init,
    .instance_size = sizeof(AspeedINTCState),
    .class_init = aspeed_intc_class_init,
    .abstract = true,
};

static void aspeed_2700_intc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedINTCClass *aic = ASPEED_INTC_CLASS(klass);

    dc->desc = "ASPEED 2700 INTC Controller";
    aic->num_lines = 32;
    aic->num_ints = 9;
}

static const TypeInfo aspeed_2700_intc_info = {
    .name = TYPE_ASPEED_2700_INTC,
    .parent = TYPE_ASPEED_INTC,
    .class_init = aspeed_2700_intc_class_init,
};

static void aspeed_intc_register_types(void)
{
    type_register_static(&aspeed_intc_info);
    type_register_static(&aspeed_2700_intc_info);
}

type_init(aspeed_intc_register_types);
