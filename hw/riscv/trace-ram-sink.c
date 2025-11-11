/*
 * Emulation of a RISC-V Trace RAM Sink
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "trace-ram-sink.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"
#include "system/device_tree.h"
#include "hw/sysbus.h"
#include "hw/register.h"

#define R_TR_RAM_CONTROL_RSVP_BITS (MAKE_64BIT_MASK(32, 32) | \
                                    R_TR_RAM_CONTROL_RSVP1_MASK | \
                                    R_TR_RAM_CONTROL_RSVP2_MASK | \
                                    R_TR_RAM_CONTROL_RSVP3_MASK | \
                                    R_TR_RAM_CONTROL_RSVP4_MASK)

/* trRamEmpty is the only RO field and reset value */
#define R_TR_RAM_CONTROL_RESET R_TR_RAM_CONTROL_EMPTY_MASK
#define R_TR_RAM_CONTROL_RO_BITS R_TR_RAM_CONTROL_EMPTY_MASK

#define R_TR_RAM_IMPL_RSVP_BITS (MAKE_64BIT_MASK(32, 32) | \
                                 R_TR_RAM_IMPL_RSVP1_MASK)

#define R_TR_RAM_IMPL_RO_BITS (R_TR_RAM_IMPL_VER_MAJOR_MASK | \
                               R_TR_RAM_IMPL_VER_MINOR_MASK | \
                               R_TR_RAM_IMPL_COMP_TYPE_MASK | \
                               R_TR_RAM_IMPL_HAS_SRAM_MASK | \
                               R_TR_RAM_IMPL_HAS_SMEM_MASK)

#define R_TR_RAM_IMPL_RESET (BIT(0) | 0x9 << 8)

static RegisterAccessInfo tr_ramsink_regs_info[] = {
    {   .name = "TR_RAM_CONTROL", .addr = A_TR_RAM_CONTROL,
        .rsvd = R_TR_RAM_CONTROL_RSVP_BITS,
        .reset = R_TR_RAM_CONTROL_RESET,
        .ro = R_TR_RAM_CONTROL_RO_BITS,
    },
    {   .name = "TR_RAM_IMPL", .addr = A_TR_RAM_IMPL,
        .rsvd = R_TR_RAM_IMPL_RSVP_BITS,
        .reset = R_TR_RAM_IMPL_RESET,
        .ro = R_TR_RAM_IMPL_RO_BITS,
    },
    {   .name = "TR_RAM_START_LOW", .addr = A_TR_RAM_START_LOW,
    },
    {   .name = "TR_RAM_START_HIGH", .addr = A_TR_RAM_START_HIGH,
    },
    {   .name = "TR_RAM_LIMIT_LOW", .addr = A_TR_RAM_LIMIT_LOW,
    },
    {   .name = "TR_RAM_LIMIT_HIGH", .addr = A_TR_RAM_LIMIT_HIGH,
    },
    {   .name = "TR_RAM_WP_LOW", .addr = A_TR_RAM_WP_LOW,
    },
    {   .name = "TR_RAM_WP_HIGH", .addr = A_TR_RAM_WP_HIGH,
    },
};

static uint64_t tr_ramsink_regread(void *opaque, hwaddr addr, unsigned size)
{
    TraceRamSink *tram = TRACE_RAM_SINK(opaque);
    RegisterInfo *r = &tram->regs_info[addr / 4];

    if (!r->data) {
        trace_tr_ramsink_read_error(addr);
        return 0;
    }

    return register_read(r, ~0, NULL, false);
}

static void tr_ramsink_regwrite(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    TraceRamSink *tram = TRACE_RAM_SINK(opaque);
    RegisterInfo *r = &tram->regs_info[addr / 4];

    if (!r->data) {
        trace_tr_ramsink_write_error(addr, value);
        return;
    }

    register_write(r, value, ~0, NULL, false);
}

static const MemoryRegionOps tr_ramsink_regops = {
    .read = tr_ramsink_regread,
    .write = tr_ramsink_regwrite,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t tr_ramsink_msgread(void *opaque, hwaddr addr, unsigned size)
{
    TraceRamSink *tram = TRACE_RAM_SINK(opaque);

    switch (size) {
    case 1:
        return tram->msgs[addr];
    case 2:
        return (uint16_t)tram->msgs[addr];
    case 4:
        return (uint32_t)tram->msgs[addr];
    default:
        g_assert_not_reached();
    }
}

static void tr_ramsink_msgwrite(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    TraceRamSink *tram = TRACE_RAM_SINK(opaque);

    switch (size) {
    case 1:
        tram->msgs[addr] = value;
        break;
    case 2:
        tram->msgs[addr] = extract16(value, 0, 8);
        tram->msgs[addr + 1] = extract16(value, 8, 8);
        break;
    case 4:
        tram->msgs[addr] = extract32(value, 0, 8);
        tram->msgs[addr + 1] = extract32(value, 8, 8);
        tram->msgs[addr + 2] = extract32(value, 16, 8);
        tram->msgs[addr + 3] = extract32(value, 24, 8);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps tr_ramsink_smemops = {
    .read = tr_ramsink_msgread,
    .write = tr_ramsink_msgwrite,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void tr_ramsink_setup_regs(TraceRamSink *tram)
{
    hwaddr ramlimit = tram->smemaddr + tram->smemsize;

    ARRAY_FIELD_DP32(tram->regs, TR_RAM_START_LOW, ADDR,
                     extract64(tram->smemaddr, 2, 30));
    ARRAY_FIELD_DP32(tram->regs, TR_RAM_START_HIGH, ADDR,
                     extract64(tram->smemaddr, 32, 32));

    ARRAY_FIELD_DP32(tram->regs, TR_RAM_WP_LOW, ADDR,
                     extract64(tram->smemaddr, 2, 30));
    ARRAY_FIELD_DP32(tram->regs, TR_RAM_WP_HIGH, ADDR,
                     extract64(tram->smemaddr, 32, 32));

    ARRAY_FIELD_DP32(tram->regs, TR_RAM_LIMIT_LOW, ADDR,
                     extract64(ramlimit, 2, 30));
    ARRAY_FIELD_DP32(tram->regs, TR_RAM_LIMIT_HIGH, ADDR,
                     extract64(ramlimit, 32, 32));
}

static void tr_ramsink_reset(DeviceState *dev)
{
    TraceRamSink *tram = TRACE_RAM_SINK(dev);

    for (int i = 0; i < ARRAY_SIZE(tram->regs_info); i++) {
        register_reset(&tram->regs_info[i]);
    }

    tr_ramsink_setup_regs(tram);
}

static void tr_ramsink_realize(DeviceState *dev, Error **errp)
{
    TraceRamSink *tram = TRACE_RAM_SINK(dev);

    memory_region_init_io(&tram->reg_mem, OBJECT(dev),
                          &tr_ramsink_regops, tram,
                          "trace-ram-sink-regs",
                          tram->reg_mem_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(tram), &tram->reg_mem);
    sysbus_mmio_map(SYS_BUS_DEVICE(tram), 0, tram->baseaddr);

    g_assert(tram->smemsize > 0);
    tram->msgs = g_malloc0(tram->smemsize);

    memory_region_init_io(&tram->smem, OBJECT(dev),
                          &tr_ramsink_smemops, tram,
                          "trace-ram-sink-smem",
                          tram->smemsize);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &tram->smem);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, tram->smemaddr);

    /* RegisterInfo init taken from hw/dma/xlnx-zdma.c */
    for (int i = 0; i < ARRAY_SIZE(tr_ramsink_regs_info); i++) {
        uint32_t reg_idx = tr_ramsink_regs_info[i].addr / 4;
        RegisterInfo *r = &tram->regs_info[reg_idx];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&tram->regs[reg_idx],
            .data_size = sizeof(uint32_t),
            .access = &tr_ramsink_regs_info[i],
            .opaque = tram,
        };
    }
}

static const Property tr_ramsink_props[] = {
    DEFINE_PROP_UINT64("baseaddr", TraceRamSink, baseaddr, 0),
    DEFINE_PROP_UINT64("smemaddr", TraceRamSink, smemaddr, 0),
    DEFINE_PROP_UINT32("smemsize", TraceRamSink, smemsize, 0),
    DEFINE_PROP_UINT32("reg-mem-size", TraceRamSink,
                       reg_mem_size, TR_DEV_REGMAP_SIZE),
};

static const VMStateDescription vmstate_tr_ramsink = {
    .name = TYPE_TRACE_RAM_SINK,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TraceRamSink, TRACE_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void tr_ramsink_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, tr_ramsink_reset);
    device_class_set_props(dc, tr_ramsink_props);
    dc->realize = tr_ramsink_realize;
    dc->vmsd = &vmstate_tr_ramsink;
}

static const TypeInfo tr_ramsink_info = {
    .name          = TYPE_TRACE_RAM_SINK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TraceRamSink),
    .class_init    = tr_ramsink_class_init,
};

static void tr_ramsink_register_types(void)
{
    type_register_static(&tr_ramsink_info);
}

type_init(tr_ramsink_register_types)
