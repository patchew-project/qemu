/*
 * RP2040 QSPI IO bank emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_ioqspi.h"
#include "hw/ssi/rp2040_xip.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define IOQSPI_INTR             0x30
#define IOQSPI_PROC0_INTE       0x34
#define IOQSPI_PROC0_INTF       0x38
#define IOQSPI_PROC0_INTS       0x3c
#define IOQSPI_PROC1_INTE       0x40
#define IOQSPI_PROC1_INTF       0x44
#define IOQSPI_PROC1_INTS       0x48
#define IOQSPI_DORMANT_INTE     0x4c
#define IOQSPI_DORMANT_INTF     0x50
#define IOQSPI_DORMANT_INTS     0x54

#define IOQSPI_CTRL_RESET       0x1f
#define IOQSPI_CTRL_RW_MASK     0x33333f
#define IOQSPI_CTRL_OUTOVER_MASK 0x300
#define IOQSPI_CTRL_OUTOVER_LOW  0x200
#define IOQSPI_CTRL_OUTOVER_HIGH 0x300
#define IOQSPI_SS_INDEX          1
#define IOQSPI_INTR_EDGE_MASK   0x00cccccc
#define IOQSPI_IRQ_MASK         0x00ffffff

#define ATOMIC_ALIAS_MASK       0x3000
#define ATOMIC_XOR              0x1000
#define ATOMIC_SET              0x2000
#define ATOMIC_CLR              0x3000

static uint32_t rp2040_ioqspi_apply_alias(uint32_t old, uint32_t value,
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

static bool rp2040_ioqspi_ctrl_offset(hwaddr offset, unsigned *index)
{
    if (offset > 0x2c || (offset & 0x7) != 0x4) {
        return false;
    }

    *index = offset / 8;
    return *index < ARRAY_SIZE(((RP2040IoQspiState *)0)->ctrl);
}

static bool rp2040_ioqspi_status_offset(hwaddr offset)
{
    return offset <= 0x28 && (offset & 0x7) == 0;
}

static uint32_t rp2040_ioqspi_ints(uint32_t intr, uint32_t inte,
                                   uint32_t intf)
{
    return (intr & inte) | intf;
}

static void rp2040_ioqspi_update_ss(RP2040IoQspiState *s)
{
    uint32_t outover;

    if (!s->xip) {
        return;
    }

    outover = s->ctrl[IOQSPI_SS_INDEX] & IOQSPI_CTRL_OUTOVER_MASK;
    if (outover == IOQSPI_CTRL_OUTOVER_LOW) {
        rp2040_xip_qspi_cs(s->xip, false);
    } else if (outover == IOQSPI_CTRL_OUTOVER_HIGH) {
        rp2040_xip_qspi_cs(s->xip, true);
    }
}

static uint64_t rp2040_ioqspi_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040IoQspiState *s = opaque;
    hwaddr offset = addr & 0xfff;
    unsigned index;
    uint64_t value;

    if (rp2040_ioqspi_status_offset(offset)) {
        value = 0;
    } else if (rp2040_ioqspi_ctrl_offset(offset, &index)) {
        value = s->ctrl[index];
    } else {
        switch (offset) {
        case IOQSPI_INTR:
            value = s->intr;
            break;
        case IOQSPI_PROC0_INTE:
            value = s->proc0_inte;
            break;
        case IOQSPI_PROC0_INTF:
            value = s->proc0_intf;
            break;
        case IOQSPI_PROC0_INTS:
            value = rp2040_ioqspi_ints(s->intr, s->proc0_inte,
                                       s->proc0_intf);
            break;
        case IOQSPI_PROC1_INTE:
            value = s->proc1_inte;
            break;
        case IOQSPI_PROC1_INTF:
            value = s->proc1_intf;
            break;
        case IOQSPI_PROC1_INTS:
            value = rp2040_ioqspi_ints(s->intr, s->proc1_inte,
                                       s->proc1_intf);
            break;
        case IOQSPI_DORMANT_INTE:
            value = s->dormant_wake_inte;
            break;
        case IOQSPI_DORMANT_INTF:
            value = s->dormant_wake_intf;
            break;
        case IOQSPI_DORMANT_INTS:
            value = rp2040_ioqspi_ints(s->intr, s->dormant_wake_inte,
                                       s->dormant_wake_intf);
            break;
        default:
            value = 0;
            rp2040_log_unimplemented_read("ioqspi", size,
                                          RP2040_IOQSPI_BASE + addr, offset,
                                          value);
            break;
        }
    }

    return value;
}

static void rp2040_ioqspi_write(void *opaque, hwaddr addr,
                                uint64_t value64, unsigned size)
{
    RP2040IoQspiState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    unsigned index;
    uint32_t value = value64;

    if (rp2040_ioqspi_ctrl_offset(offset, &index)) {
        s->ctrl[index] =
            rp2040_ioqspi_apply_alias(s->ctrl[index], value, alias) &
            IOQSPI_CTRL_RW_MASK;
        if (index == IOQSPI_SS_INDEX) {
            rp2040_ioqspi_update_ss(s);
        }
    } else {
        switch (offset) {
        case IOQSPI_INTR:
            s->intr &= ~(value & IOQSPI_INTR_EDGE_MASK);
            break;
        case IOQSPI_PROC0_INTE:
            s->proc0_inte =
                rp2040_ioqspi_apply_alias(s->proc0_inte, value, alias) &
                IOQSPI_IRQ_MASK;
            break;
        case IOQSPI_PROC0_INTF:
            s->proc0_intf =
                rp2040_ioqspi_apply_alias(s->proc0_intf, value, alias) &
                IOQSPI_IRQ_MASK;
            break;
        case IOQSPI_PROC1_INTE:
            s->proc1_inte =
                rp2040_ioqspi_apply_alias(s->proc1_inte, value, alias) &
                IOQSPI_IRQ_MASK;
            break;
        case IOQSPI_PROC1_INTF:
            s->proc1_intf =
                rp2040_ioqspi_apply_alias(s->proc1_intf, value, alias) &
                IOQSPI_IRQ_MASK;
            break;
        case IOQSPI_DORMANT_INTE:
            s->dormant_wake_inte =
                rp2040_ioqspi_apply_alias(s->dormant_wake_inte, value,
                                          alias) &
                IOQSPI_IRQ_MASK;
            break;
        case IOQSPI_DORMANT_INTF:
            s->dormant_wake_intf =
                rp2040_ioqspi_apply_alias(s->dormant_wake_intf, value,
                                          alias) &
                IOQSPI_IRQ_MASK;
            break;
        default:
            if (!rp2040_ioqspi_status_offset(offset)) {
                rp2040_log_unimplemented_write("ioqspi", size,
                                               RP2040_IOQSPI_BASE + addr,
                                               offset, value64);
            }
            break;
        }
    }
}

static const Property rp2040_ioqspi_properties[] = {
    DEFINE_PROP_LINK("xip", RP2040IoQspiState, xip, TYPE_RP2040_XIP,
                     RP2040XipState *),
};

static const MemoryRegionOps rp2040_ioqspi_ops = {
    .read = rp2040_ioqspi_read,
    .write = rp2040_ioqspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_ioqspi_reset(DeviceState *dev)
{
    RP2040IoQspiState *s = RP2040_IOQSPI(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->ctrl); i++) {
        s->ctrl[i] = IOQSPI_CTRL_RESET;
    }
    s->intr = 0;
    s->proc0_inte = 0;
    s->proc0_intf = 0;
    s->proc1_inte = 0;
    s->proc1_intf = 0;
    s->dormant_wake_inte = 0;
    s->dormant_wake_intf = 0;
}

static void rp2040_ioqspi_init(Object *obj)
{
    RP2040IoQspiState *s = RP2040_IOQSPI(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_ioqspi_ops, s,
                          "rp2040.ioqspi", RP2040_IOQSPI_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_ioqspi_vmstate = {
    .name = TYPE_RP2040_IOQSPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(ctrl, RP2040IoQspiState, 6),
        VMSTATE_UINT32(intr, RP2040IoQspiState),
        VMSTATE_UINT32(proc0_inte, RP2040IoQspiState),
        VMSTATE_UINT32(proc0_intf, RP2040IoQspiState),
        VMSTATE_UINT32(proc1_inte, RP2040IoQspiState),
        VMSTATE_UINT32(proc1_intf, RP2040IoQspiState),
        VMSTATE_UINT32(dormant_wake_inte, RP2040IoQspiState),
        VMSTATE_UINT32(dormant_wake_intf, RP2040IoQspiState),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_ioqspi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_ioqspi_reset);
    device_class_set_props(dc, rp2040_ioqspi_properties);
    dc->vmsd = &rp2040_ioqspi_vmstate;
}

static const TypeInfo rp2040_ioqspi_info = {
    .name          = TYPE_RP2040_IOQSPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040IoQspiState),
    .instance_init = rp2040_ioqspi_init,
    .class_init    = rp2040_ioqspi_class_init,
};

static void rp2040_ioqspi_register_types(void)
{
    type_register_static(&rp2040_ioqspi_info);
}
type_init(rp2040_ioqspi_register_types)
