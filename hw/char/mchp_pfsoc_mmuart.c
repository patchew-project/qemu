/*
 * Microchip PolarFire SoC MMUART emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
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
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/char/mchp_pfsoc_mmuart.h"
#include "hw/qdev-properties.h"

static uint64_t mchp_pfsoc_mmuart_read(void *opaque, hwaddr addr, unsigned size)
{
    MchpPfSoCMMUartState *s = opaque;

    if (addr >= MCHP_PFSOC_MMUART_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read: addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    return s->reg[addr / sizeof(uint32_t)];
}

static void mchp_pfsoc_mmuart_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    MchpPfSoCMMUartState *s = opaque;
    uint32_t val32 = (uint32_t)value;

    if (addr >= MCHP_PFSOC_MMUART_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%" HWADDR_PRIx
                      " v=0x%x\n", __func__, addr, val32);
        return;
    }

    s->reg[addr / sizeof(uint32_t)] = val32;
}

static const MemoryRegionOps mchp_pfsoc_mmuart_ops = {
    .read = mchp_pfsoc_mmuart_read,
    .write = mchp_pfsoc_mmuart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void mchp_pfsoc_mmuart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MchpPfSoCMMUartState *s = MCHP_PFSOC_UART(obj);

    memory_region_init_io(&s->iomem, NULL, &mchp_pfsoc_mmuart_ops, s,
                          "mchp.pfsoc.mmuart", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    object_initialize_child(obj, "serial-mm", &s->serial_mm, TYPE_SERIAL_MM);
    object_property_add_alias(obj, "chardev", OBJECT(&s->serial_mm), "chardev");
}

static void mchp_pfsoc_mmuart_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCMMUartState *s = MCHP_PFSOC_UART(dev);

    qdev_prop_set_uint8(DEVICE(&s->serial_mm), "regshift", 2);
    qdev_prop_set_uint32(DEVICE(&s->serial_mm), "baudbase", 399193);
    qdev_prop_set_uint8(DEVICE(&s->serial_mm), "endianness",
                        DEVICE_LITTLE_ENDIAN);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->serial_mm), errp)) {
        return;
    }
    sysbus_pass_irq(SYS_BUS_DEVICE(dev), SYS_BUS_DEVICE(&s->serial_mm));
    memory_region_add_subregion(&s->iomem, 0x20,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->serial_mm), 0));
}

static void mchp_pfsoc_mmuart_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = mchp_pfsoc_mmuart_realize;
}

static const TypeInfo mchp_pfsoc_mmuart_info = {
    .name          = TYPE_MCHP_PFSOC_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCMMUartState),
    .instance_init = mchp_pfsoc_mmuart_init,
    .class_init    = mchp_pfsoc_mmuart_class_init,
};

static void mchp_pfsoc_mmuart_register_types(void)
{
    type_register_static(&mchp_pfsoc_mmuart_info);
}

type_init(mchp_pfsoc_mmuart_register_types)

MchpPfSoCMMUartState *mchp_pfsoc_mmuart_create(MemoryRegion *sysmem,
                                               hwaddr base,
                                               qemu_irq irq, Chardev *chr)
{
    DeviceState *dev = qdev_new(TYPE_MCHP_PFSOC_UART);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize(sbd, &error_fatal);

    memory_region_add_subregion(sysmem, base, sysbus_mmio_get_region(sbd, 0));
    sysbus_connect_irq(sbd, 0, irq);

    return MCHP_PFSOC_UART(dev);
}
