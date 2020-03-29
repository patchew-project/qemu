/*
 * BCM2835 SOC MPHI emulation
 *
 * Very basic emulation, only providing the FIQ interrupt needed to
 * allow the dwc-otg USB host controller driver in the Raspbian kernel
 * to function.
 *
 * Copyright (c) 2020 Paul Zimmerman <pauldzim@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/misc/bcm2835_mphi.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

static inline void mphi_raise_irq(BCM2835MphiState *s)
{
    qemu_set_irq(s->irq, 1);
}

static inline void mphi_lower_irq(BCM2835MphiState *s)
{
    qemu_set_irq(s->irq, 0);
}

static uint64_t mphi_reg_read(void *ptr, hwaddr addr, unsigned size)
{
    BCM2835MphiState *s = ptr;
    uint32_t reg = s->regbase + addr;
    uint32_t val = 0;

    switch (reg) {
    case 0x28:  /* outdda */
        val = s->outdda;
        break;
    case 0x2c:  /* outddb */
        val = s->outddb;
        break;
    case 0x4c:  /* ctrl */
        val = s->ctrl;
        val |= 1 << 17;
        break;
    case 0x50:  /* intstat */
        val = s->intstat;
        break;
    case 0x1f0: /* swirq_set */
        val = s->swirq_set;
        break;
    case 0x1f4: /* swirq_clr */
        val = s->swirq_clr;
        break;
    default:
        break;
    }

    return val;
}

static void mphi_reg_write(void *ptr, hwaddr addr, uint64_t val, unsigned size)
{
    BCM2835MphiState *s = ptr;
    uint32_t reg = s->regbase + addr;
    uint32_t old;
    int do_irq = 0;

    val &= 0xffffffff;

    switch (reg) {
    case 0x28:  /* outdda */
        old = s->outdda;
        s->outdda = val;
        break;
    case 0x2c:  /* outddb */
        old = s->outddb;
        s->outddb = val;
        if (val & (1 << 29)) {
            do_irq = 1;
        }
        break;
    case 0x4c:  /* ctrl */
        old = s->ctrl;
        s->ctrl = val;
        if (val & (1 << 16)) {
            do_irq = -1;
        }
        break;
    case 0x50:  /* intstat */
        old = s->intstat;
        s->intstat = val;
        if (val & ((1 << 16) | (1 << 29))) {
            do_irq = -1;
        }
        break;
    case 0x1f0: /* swirq_set */
        old = s->swirq_set;
        s->swirq_set = val;
        do_irq = 1;
        break;
    case 0x1f4: /* swirq_clr */
        old = s->swirq_clr;
        s->swirq_clr = val;
        do_irq = -1;
        break;
    default:
        break;
    }

    old = old;
    if (do_irq > 0) {
        mphi_raise_irq(s);
    } else if (do_irq < 0) {
        mphi_lower_irq(s);
    }
}

static const MemoryRegionOps mphi_mmio_ops = {
    .read = mphi_reg_read,
    .write = mphi_reg_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mphi_realize(BCM2835MphiState *s, DeviceState *dev, Error **errp)
{
    s->device = dev;
}

static void mphi_init(BCM2835MphiState *s, DeviceState *dev)
{
    memory_region_init(&s->mem, OBJECT(dev), "mphi", MPHI_MMIO_SIZE);
    memory_region_init_io(&s->mem_reg, OBJECT(dev), &mphi_mmio_ops, s,
                          "global", 0x200);
    memory_region_add_subregion(&s->mem, s->regbase, &s->mem_reg);
}

static void mphi_sysbus_reset(DeviceState *dev)
{
}

static void mphi_sysbus_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    BCM2835MphiState *s = BCM2835_MPHI(dev);

    mphi_realize(s, dev, errp);
    sysbus_init_irq(d, &s->irq);
}

static void mphi_sysbus_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    BCM2835MphiState *s = BCM2835_MPHI(obj);

    s->regbase = 0;
    s->as = &address_space_memory;
    mphi_init(s, DEVICE(obj));
    sysbus_init_mmio(d, &s->mem);
}

static void mphi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mphi_sysbus_realize;
    dc->reset = mphi_sysbus_reset;
}

static const TypeInfo bcm2835_mphi_type_info = {
    .name          = TYPE_BCM2835_MPHI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835MphiState),
    .instance_init = mphi_sysbus_init,
    .class_init    = mphi_class_init,
};

static void bcm2835_mphi_register_types(void)
{
    type_register_static(&bcm2835_mphi_type_info);
}

type_init(bcm2835_mphi_register_types)
