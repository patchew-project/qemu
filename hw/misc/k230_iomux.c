/*
 * Kendryte K230 IOMUX
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides the Function IO configuration registers documented by the K230
 * Technical Reference Manual.
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * For more information, see <https://www.kendryte.com/en/proDetail/230>
 */

#include "qemu/osdep.h"
#include "hw/misc/k230_iomux.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "trace.h"

/* TRM V0.3.1 section 12.9.2.1 Function IO register list. */
static const uint32_t k230_iomux_reset_values[K230_IOMUX_NUM_REGS] = {
    0x944, 0x944, 0x929, 0x908, 0x888, 0x908, 0x948, 0x890,
    0x890, 0x890, 0x910, 0x890, 0x890, 0x8a9, 0xa9e, 0xabf,
    0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e,
    0xb1e, 0xa90, 0xabf, 0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e,
    0xbd0, 0xbd0, 0xbd0, 0xbd0, 0xbd0, 0xbd0, 0x890, 0x910,
    0x890, 0x910, 0x890, 0x910, 0x890, 0x910, 0x890, 0x910,
    0x890, 0x910, 0x890, 0x910, 0x890, 0x910, 0x89e, 0x89f,
    0x99e, 0x99e, 0x99e, 0x99e, 0x890, 0x890, 0x8a9, 0x8a9,
};

static void k230_iomux_reset(DeviceState *dev)
{
    K230IomuxState *s = K230_IOMUX(dev);

    memcpy(s->regs, k230_iomux_reset_values,
           sizeof(k230_iomux_reset_values));
}

static uint64_t k230_iomux_read(void *opaque, hwaddr offset, unsigned size)
{
    K230IomuxState *s = K230_IOMUX(opaque);
    uint32_t value = 0;

    if (offset < K230_IOMUX_REGS_SIZE) {
        value = s->regs[offset >> 2];
    }

    trace_k230_iomux_read(offset, value);
    return value;
}

static void k230_iomux_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    K230IomuxState *s = K230_IOMUX(opaque);

    trace_k230_iomux_write(offset, value);
    if (offset < K230_IOMUX_REGS_SIZE) {
        s->regs[offset >> 2] = value & K230_IOMUX_WRITABLE_MASK;
    }
}

static const MemoryRegionOps k230_iomux_ops = {
    .read = k230_iomux_read,
    .write = k230_iomux_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void k230_iomux_init(Object *obj)
{
    K230IomuxState *s = K230_IOMUX(obj);

    memory_region_init_io(&s->mmio, obj, &k230_iomux_ops, s,
                          TYPE_K230_IOMUX, K230_IOMUX_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const VMStateDescription vmstate_k230_iomux = {
    .name = "k230.iomux",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, K230IomuxState, K230_IOMUX_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void k230_iomux_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Kendryte K230 IOMUX";
    dc->vmsd = &vmstate_k230_iomux;
    device_class_set_legacy_reset(dc, k230_iomux_reset);
}

static const TypeInfo k230_iomux_info = {
    .name          = TYPE_K230_IOMUX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230IomuxState),
    .instance_init = k230_iomux_init,
    .class_init    = k230_iomux_class_init,
};

static void k230_iomux_register_types(void)
{
    type_register_static(&k230_iomux_info);
}

type_init(k230_iomux_register_types)
