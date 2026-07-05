/*
 * RP2040 pad control emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_pads.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define PADS_BANK0_VOLTAGE_SELECT 0x00
#define PADS_BANK0_GPIO0          0x04
#define PADS_BANK0_GPIO29         0x78
#define PADS_BANK0_SWCLK          0x7c
#define PADS_BANK0_SWD            0x80

#define PADS_QSPI_VOLTAGE_SELECT 0x00
#define PADS_QSPI_SCLK           0x04
#define PADS_QSPI_SS             0x18

#define PADS_VOLTAGE_SELECT_MASK BIT(0)
#define PADS_PAD_MASK            0xff
#define PADS_BANK0_GPIO_RESET     0x56
#define PADS_BANK0_SWD_RESET      0x5a
#define PADS_QSPI_SCLK_RESET     0x56
#define PADS_QSPI_SD_RESET       0x52
#define PADS_QSPI_SS_RESET       0x5a

#define ATOMIC_ALIAS_MASK        0x3000
#define ATOMIC_XOR               0x1000
#define ATOMIC_SET               0x2000
#define ATOMIC_CLR               0x3000

static uint32_t rp2040_pads_apply_alias(uint32_t old, uint32_t value,
                                        hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static bool rp2040_pads_bank0_gpio_offset(hwaddr offset, unsigned *index)
{
    if (offset < PADS_BANK0_GPIO0 || offset > PADS_BANK0_GPIO29 ||
        (offset & 0x3)) {
        return false;
    }

    *index = (offset - PADS_BANK0_GPIO0) / sizeof(uint32_t);
    return *index < ARRAY_SIZE(((RP2040PadsBank0State *)0)->gpio);
}

static bool rp2040_pads_qspi_pad_offset(hwaddr offset, unsigned *index)
{
    if (offset < PADS_QSPI_SCLK || offset > PADS_QSPI_SS ||
        (offset & 0x3)) {
        return false;
    }

    *index = (offset - PADS_QSPI_SCLK) / sizeof(uint32_t);
    return *index < ARRAY_SIZE(((RP2040PadsQspiState *)0)->pad);
}

static uint64_t rp2040_pads_bank0_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    RP2040PadsBank0State *s = opaque;
    hwaddr offset = addr & 0xfff;
    unsigned index;
    uint64_t value;

    switch (offset) {
    case PADS_BANK0_VOLTAGE_SELECT:
        value = s->voltage_select;
        break;
    case PADS_BANK0_SWCLK:
        value = s->swclk;
        break;
    case PADS_BANK0_SWD:
        value = s->swd;
        break;
    default:
        if (rp2040_pads_bank0_gpio_offset(offset, &index)) {
            value = s->gpio[index];
        } else {
            value = 0;
            rp2040_log_unimplemented_read("padsbank0", size,
                                          RP2040_PADS_BANK0_BASE + addr,
                                          offset, value);
        }
        break;
    }

    return value;
}

static void rp2040_pads_bank0_write(void *opaque, hwaddr addr,
                                    uint64_t value64, unsigned size)
{
    RP2040PadsBank0State *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    unsigned index;
    uint32_t value = value64;

    switch (offset) {
    case PADS_BANK0_VOLTAGE_SELECT:
        s->voltage_select =
            rp2040_pads_apply_alias(s->voltage_select, value, alias) &
            PADS_VOLTAGE_SELECT_MASK;
        break;
    case PADS_BANK0_SWCLK:
        s->swclk = rp2040_pads_apply_alias(s->swclk, value, alias) &
                   PADS_PAD_MASK;
        break;
    case PADS_BANK0_SWD:
        s->swd = rp2040_pads_apply_alias(s->swd, value, alias) &
                 PADS_PAD_MASK;
        break;
    default:
        if (rp2040_pads_bank0_gpio_offset(offset, &index)) {
            s->gpio[index] = rp2040_pads_apply_alias(s->gpio[index], value,
                                                     alias) & PADS_PAD_MASK;
        } else {
            rp2040_log_unimplemented_write("padsbank0", size,
                                           RP2040_PADS_BANK0_BASE + addr,
                                           offset, value64);
        }
        break;
    }
}

static uint64_t rp2040_pads_qspi_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    RP2040PadsQspiState *s = opaque;
    hwaddr offset = addr & 0xfff;
    unsigned index;
    uint64_t value;

    switch (offset) {
    case PADS_QSPI_VOLTAGE_SELECT:
        value = s->voltage_select;
        break;
    default:
        if (rp2040_pads_qspi_pad_offset(offset, &index)) {
            value = s->pad[index];
        } else {
            value = 0;
            rp2040_log_unimplemented_read("padsqspi", size,
                                          RP2040_PADS_QSPI_BASE + addr,
                                          offset, value);
        }
        break;
    }

    return value;
}

static void rp2040_pads_qspi_write(void *opaque, hwaddr addr,
                                   uint64_t value64, unsigned size)
{
    RP2040PadsQspiState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    unsigned index;
    uint32_t value = value64;

    switch (offset) {
    case PADS_QSPI_VOLTAGE_SELECT:
        s->voltage_select =
            rp2040_pads_apply_alias(s->voltage_select, value, alias) &
            PADS_VOLTAGE_SELECT_MASK;
        break;
    default:
        if (rp2040_pads_qspi_pad_offset(offset, &index)) {
            s->pad[index] = rp2040_pads_apply_alias(s->pad[index], value,
                                                    alias) & PADS_PAD_MASK;
        } else {
            rp2040_log_unimplemented_write("padsqspi", size,
                                           RP2040_PADS_QSPI_BASE + addr,
                                           offset, value64);
        }
        break;
    }
}

static const MemoryRegionOps rp2040_pads_bank0_ops = {
    .read = rp2040_pads_bank0_read,
    .write = rp2040_pads_bank0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps rp2040_pads_qspi_ops = {
    .read = rp2040_pads_qspi_read,
    .write = rp2040_pads_qspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_pads_bank0_reset(DeviceState *dev)
{
    RP2040PadsBank0State *s = RP2040_PADS_BANK0(dev);
    unsigned i;

    s->voltage_select = 0;
    for (i = 0; i < ARRAY_SIZE(s->gpio); i++) {
        s->gpio[i] = PADS_BANK0_GPIO_RESET;
    }
    s->swclk = PADS_BANK0_SWD_RESET;
    s->swd = PADS_BANK0_SWD_RESET;
}

static void rp2040_pads_qspi_reset(DeviceState *dev)
{
    RP2040PadsQspiState *s = RP2040_PADS_QSPI(dev);

    s->voltage_select = 0;
    s->pad[0] = PADS_QSPI_SCLK_RESET;
    s->pad[1] = PADS_QSPI_SD_RESET;
    s->pad[2] = PADS_QSPI_SD_RESET;
    s->pad[3] = PADS_QSPI_SD_RESET;
    s->pad[4] = PADS_QSPI_SD_RESET;
    s->pad[5] = PADS_QSPI_SS_RESET;
}

static void rp2040_pads_bank0_init(Object *obj)
{
    RP2040PadsBank0State *s = RP2040_PADS_BANK0(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_pads_bank0_ops, s,
                          "rp2040.padsbank0", RP2040_PADS_BANK0_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void rp2040_pads_qspi_init(Object *obj)
{
    RP2040PadsQspiState *s = RP2040_PADS_QSPI(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_pads_qspi_ops, s,
                          "rp2040.padsqspi", RP2040_PADS_QSPI_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_pads_bank0_vmstate = {
    .name = TYPE_RP2040_PADS_BANK0,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(voltage_select, RP2040PadsBank0State),
        VMSTATE_UINT32_ARRAY(gpio, RP2040PadsBank0State, 30),
        VMSTATE_UINT32(swclk, RP2040PadsBank0State),
        VMSTATE_UINT32(swd, RP2040PadsBank0State),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription rp2040_pads_qspi_vmstate = {
    .name = TYPE_RP2040_PADS_QSPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(voltage_select, RP2040PadsQspiState),
        VMSTATE_UINT32_ARRAY(pad, RP2040PadsQspiState, 6),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_pads_bank0_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_pads_bank0_reset);
    dc->vmsd = &rp2040_pads_bank0_vmstate;
}

static void rp2040_pads_qspi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_pads_qspi_reset);
    dc->vmsd = &rp2040_pads_qspi_vmstate;
}

static const TypeInfo rp2040_pads_bank0_info = {
    .name          = TYPE_RP2040_PADS_BANK0,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040PadsBank0State),
    .instance_init = rp2040_pads_bank0_init,
    .class_init    = rp2040_pads_bank0_class_init,
};

static const TypeInfo rp2040_pads_qspi_info = {
    .name          = TYPE_RP2040_PADS_QSPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040PadsQspiState),
    .instance_init = rp2040_pads_qspi_init,
    .class_init    = rp2040_pads_qspi_class_init,
};

static void rp2040_pads_register_types(void)
{
    type_register_static(&rp2040_pads_bank0_info);
    type_register_static(&rp2040_pads_qspi_info);
}
type_init(rp2040_pads_register_types)
