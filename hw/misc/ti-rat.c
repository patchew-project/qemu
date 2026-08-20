/*
 * TI RAT (Region Address Translation) SysBus device
 *
 * Copyright (c) 2025 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A RAT sits in front of a processor and translates accesses, which fall
 * into its address window, into 64-bit system addresses. Each entry maps
 * one aligned power-of-two region of the window onto a translated base;
 * enabled entries are modelled as MemoryRegion aliases into the target
 * address space.
 *
 * Where the window lies is not fixed. The "window-base" and "window-size"
 * properties place it, and the "window-root" resp. "target-root" links
 * select the address space the window is seen in and the one it
 * translates into. TI_RAT_NUM_ENTRIES gives the number of entries.
 */

#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "qemu/bitops.h"
#include "hw/core/qdev.h"
#include "hw/core/sysbus.h"
#include "system/address-spaces.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "hw/misc/ti-rat.h"
#include "hw/core/qdev-properties.h"
#include "trace.h"

/* Property defaults: the AM64x MCU R5F RAT window. */
#define TI_RAT_WINDOW_BASE 0x60000000ULL
#define TI_RAT_WINDOW_SIZE (2ULL * GiB)

#define RAT_PID 0x000
#define RAT_CONFIG 0x004

#define RAT_ENT_BASE 0x20
#define RAT_ENT_STRIDE 0x10

#define RAT_REG_CTRL 0x00
#define RAT_REG_BASE 0x04
#define RAT_REG_TRANS_L 0x08
#define RAT_REG_TRANS_H 0x0C

#define RAT_REGS_SIZE 0x1000

static void ti_rat_apply_entry(TIRATState *s, TIRATEntry *e)
{
    bool en = (e->ctrl_reg & BIT(31)) != 0;

    uint64_t shift = e->ctrl_reg & 0x3f;
    uint64_t size = (shift >= 63) ? 0 : (1ULL << shift);

    hwaddr source_addr = (hwaddr)e->base_reg;
    hwaddr dest_addr = (hwaddr)e->transl_reg | ((hwaddr)e->transh_reg << 32);

    /* Validate before aliases are changed. */
    if (!en) {
        trace_rat_disable_region(e->idx);
        if (e->inserted) {
            memory_region_transaction_begin();
            memory_region_del_subregion(&s->window_container, &e->alias);
            memory_region_set_enabled(&e->alias, false);
            memory_region_transaction_commit();
            e->inserted = false;
        }
        return;
    }

    if (size < 0x1000) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "RAT %u: size too small: 0x%" PRIx64 "\n", e->idx, size);
        return;
    }

    if (source_addr < s->window_base ||
        (source_addr - s->window_base) + size > s->window_size) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "RAT %u: source outside window: 0x%" HWADDR_PRIx "\n",
                      e->idx, source_addr);
        return;
    }

    hwaddr woff = source_addr - s->window_base;
    e->size = size;
    trace_rat_enable_region(e->idx, e->size, source_addr, dest_addr);

    memory_region_transaction_begin();

    /* Create or move the alias at programmed source offset. */
    if (!e->inserted) {
        memory_region_add_subregion(&s->window_container, woff, &e->alias);
        e->inserted = true;
    } else {
        /* Move existing alias, when the source offset changes. */
        memory_region_del_subregion(&s->window_container, &e->alias);
        memory_region_add_subregion(&s->window_container, woff, &e->alias);
    }
    /* Point the alias to the programmed translated address. */
    memory_region_set_alias_offset(&e->alias, dest_addr);
    memory_region_set_size(&e->alias, size);
    memory_region_set_enabled(&e->alias, true);
    memory_region_transaction_commit();
}

static uint64_t ti_rat_read(void *opaque, hwaddr off, unsigned size)
{
    TIRATState *s = opaque;
    int entry;
    int rel_offset;

    if (off == RAT_PID) {
        return 0x66804100;
    }

    if (off == RAT_CONFIG) {
        return 0x00300110;
    }

    if (off <= 4 || off > 0x800) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TI-RAT: invalid read offset 0x%" PRIx64 "\n", off);
        return 0;
    }

    entry = (off - RAT_ENT_BASE) / RAT_ENT_STRIDE;
    rel_offset = (off - RAT_ENT_BASE) % RAT_ENT_STRIDE;
    assert(entry < TI_RAT_NUM_ENTRIES);
    trace_rat_read_entry(entry, rel_offset, off);

    switch (rel_offset) {
    case RAT_REG_CTRL:
        return s->ent[entry].ctrl_reg;
    case RAT_REG_BASE:
        return s->ent[entry].base_reg;
    case RAT_REG_TRANS_L:
        return s->ent[entry].transl_reg;
    case RAT_REG_TRANS_H:
        return s->ent[entry].transh_reg;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TI-RAT: invalid entry read offset 0x%x\n", rel_offset);
        break;
    }

    return 0;
}

static void ti_rat_write(void *opaque, hwaddr off, uint64_t val, unsigned size)
{
    TIRATState *s = opaque;
    int entry;
    int rel_offset;

    if (off <= 4 || off > 0x800) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TI-RAT: invalid write offset 0x%" PRIx64 "\n", off);
        return;
    }

    entry = (off - RAT_ENT_BASE) / RAT_ENT_STRIDE;
    rel_offset = (off - RAT_ENT_BASE) % RAT_ENT_STRIDE;

    assert(entry < TI_RAT_NUM_ENTRIES);
    TIRATEntry *e = &s->ent[entry];

    switch (rel_offset) {
    case RAT_REG_CTRL:
        e->ctrl_reg = val;
        break;
    case RAT_REG_BASE:
        e->base_reg = val;
        break;
    case RAT_REG_TRANS_L:
        e->transl_reg = val;
        break;
    case RAT_REG_TRANS_H:
        e->transh_reg = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "TI-RAT: invalid entry write offset 0x%x\n", rel_offset);
        break;
    }

    ti_rat_apply_entry(s, e);
}

static const MemoryRegionOps ti_rat_ops = {
    .read = ti_rat_read,
    .write = ti_rat_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ti_rat_reset(DeviceState *dev)
{
    TIRATState *s = TI_RAT(dev);

    s->ctrl = 0;

    memory_region_transaction_begin();
    for (int i = 0; i < TI_RAT_NUM_ENTRIES; i++) {
        TIRATEntry *e = &s->ent[i];
        if (e->inserted) {
            memory_region_del_subregion(&s->window_container, &e->alias);
            e->inserted = false;
        }
        e->idx = i;
        e->ctrl_reg = 0;
        e->base_reg = 0;
        e->trans_base = 0;
        e->size = 0x0;
        memory_region_set_enabled(&e->alias, false);
    }
    memory_region_transaction_commit();
}

static void ti_rat_realize(DeviceState *dev, Error **errp)
{
    TIRATState *s = TI_RAT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!s->window_root) {
        error_setg(errp, "ti-rat: property 'window-root' must be set");
        return;
    }
    if (!s->target_root) {
        error_setg(errp, "ti-rat: property 'target-root' must be set");
        return;
    }
    /* Register block is separate from the translated window. */
    memory_region_init_io(&s->regs_mmio, OBJECT(s), &ti_rat_ops, s,
                          "ti-rat-regs", RAT_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->regs_mmio);

    memory_region_init(&s->window_container, OBJECT(s), "ti-rat-window",
                       s->window_size);
    memory_region_add_subregion(s->window_root, s->window_base,
                                &s->window_container);

    for (int i = 0; i < TI_RAT_NUM_ENTRIES; i++) {
        g_autofree char *name = g_strdup_printf("ti-rat-alias[%d]", i);
        memory_region_init_alias(&s->ent[i].alias, OBJECT(s), name,
                                 s->target_root, 0, 0x1000);
        memory_region_set_enabled(&s->ent[i].alias, false);
    }

    ti_rat_reset(dev);
}

static const Property ti_rat_props[] = {
    DEFINE_PROP_UINT64("window-base", TIRATState, window_base,
                       TI_RAT_WINDOW_BASE),
    DEFINE_PROP_UINT64("window-size", TIRATState, window_size,
                       TI_RAT_WINDOW_SIZE),
    DEFINE_PROP_LINK("target-root", TIRATState, target_root, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("window-root", TIRATState, window_root, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void ti_rat_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ti_rat_realize;
    device_class_set_legacy_reset(dc, ti_rat_reset);
    device_class_set_props(dc, ti_rat_props);
}

static const TypeInfo ti_rat_info = {
    .name = TYPE_TI_RAT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TIRATState),
    .class_init = ti_rat_class_init,
};

static void ti_rat_register_types(void)
{
    type_register_static(&ti_rat_info);
}

type_init(ti_rat_register_types);
