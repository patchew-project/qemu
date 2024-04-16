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

#define GICINT_EN_BASE     R_GICINT128_EN

/*
 * The address of GICINT128 to GICINT136 are from 0x1000 to 0x1804.
 * Utilize "address & 0x0f00" to get the gicint_out index and
 * its gic irq.
 */
static void aspeed_intc_update(AspeedINTCState *s, int irq, int level)
{
    uint32_t gicint_enable_addr = GICINT_EN_BASE + ((0x100 * irq) >> 2);
    uint32_t gicint_status_addr = gicint_enable_addr + (0x4 >> 2);

    if (s->trigger[irq]) {
        if (!level && !s->regs[gicint_status_addr]) {
            /* clear irq */
            trace_aspeed_intc_update_irq(irq, 0);
            qemu_set_irq(s->gicint_out[irq], 0);
            s->trigger[irq] = false;
        }
    } else {
        if (s->new_gicint_status[irq]) {
            /* set irq */
            trace_aspeed_intc_update_irq(irq, 1);
            s->regs[gicint_status_addr] = s->new_gicint_status[irq];
            s->new_gicint_status[irq] = 0;
            qemu_set_irq(s->gicint_out[irq], 1);
            s->trigger[irq] = true;
        }
    }
}

/*
 * The value of irq should be 0 to ASPEED_INTC_NR_GICS.
 * The irq 0 indicates GICINT128, irq 1 indicates GICINT129 and so on.
 */
static void aspeed_intc_set_irq(void *opaque, int irq, int level)
{
    AspeedINTCState *s = (AspeedINTCState *)opaque;
    uint32_t gicint_enable_addr = GICINT_EN_BASE + ((0x100 * irq) >> 2);
    uint32_t enable = s->regs[gicint_enable_addr];
    int i;

    if (irq > ASPEED_INTC_NR_GICS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid interrupt number: %d\n",
                      __func__, irq);
        return;
    }

    trace_aspeed_intc_set_irq(irq, level);

    for (i = 0; i < 32; i++) {
        if (s->gicint_orgate[irq].levels[i]) {
            if (enable & BIT(i)) {
                s->new_gicint_status[irq] |= BIT(i);
            }
        }
    }

    aspeed_intc_update(s, irq, level);
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
    case R_GICINT128_STATUS:
    case R_GICINT129_STATUS:
    case R_GICINT130_STATUS:
    case R_GICINT131_STATUS:
    case R_GICINT132_STATUS:
    case R_GICINT133_STATUS:
    case R_GICINT134_STATUS:
    case R_GICINT135_STATUS:
    case R_GICINT136_STATUS:
        value = s->regs[addr];
        break;
    default:
        value = s->regs[addr];
        break;
    }

    trace_aspeed_intc_read(offset, size, value);

    return value;
}

static void aspeed_intc_write(void *opaque, hwaddr offset, uint64_t data,
                                        unsigned size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    uint32_t irq = (offset & 0x0f00) >> 8;
    uint32_t addr = offset >> 2;


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
        if (!s->regs[addr]) {
            aspeed_intc_update(s, irq, 0);
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
    int i;

    for (i = 0; i < ASPEED_INTC_NR_GICS; i++) {
        object_initialize_child(obj, "gic-orgate[*]", &s->gicint_orgate[i],
                                TYPE_OR_IRQ);
        object_property_set_int(OBJECT(&s->gicint_orgate[i]), "num-lines",
                                32, &error_abort);
    }
}

static void aspeed_intc_reset(DeviceState *dev)
{
    AspeedINTCState *s = ASPEED_INTC(dev);
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->trigger, 0, sizeof(s->trigger));
    memset(s->new_gicint_status, 0, sizeof(s->new_gicint_status));
}

static void aspeed_intc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedINTCState *s = ASPEED_INTC(dev);
    int i;

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_intc_ops, s,
                          TYPE_ASPEED_INTC ".regs", ASPEED_INTC_NR_REGS << 2);

    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_in(dev, aspeed_intc_set_irq, ASPEED_INTC_NR_GICS);

    for (i = 0; i < ASPEED_INTC_NR_GICS; i++) {
        sysbus_init_irq(sbd, &s->gicint_out[i]);
    }
}

static void aspeed_intc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_intc_realize;
    dc->reset = aspeed_intc_reset;
    dc->desc = "ASPEED INTC Controller";
    dc->vmsd = NULL;
}

static const TypeInfo aspeed_intc_info = {
    .name = TYPE_ASPEED_INTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = aspeed_intc_instance_init,
    .instance_size = sizeof(AspeedINTCState),
    .class_init = aspeed_intc_class_init,
};

static void aspeed_intc_register_types(void)
{
    type_register_static(&aspeed_intc_info);
}

type_init(aspeed_intc_register_types);
