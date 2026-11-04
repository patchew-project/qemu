/*
 * SiFive L2 cache controller
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/register.h"
#include "hw/core/registerfields.h"
#include "hw/misc/sifive_l2cc.h"
#include "migration/vmstate.h"

REG64(L2_CONFIG, 0x000)
    FIELD(L2_CONFIG, BANKS, 0, 8)
    FIELD(L2_CONFIG, WAYS, 8, 8)
    FIELD(L2_CONFIG, LG_SETS, 16, 8)
    FIELD(L2_CONFIG, LG_BLOCK_BYTES, 24, 8)
REG64(L2_WAY_ENABLE, 0x008)
    FIELD(L2_WAY_ENABLE, VALUE, 0, 8)
REG64(L2_WAY_MASK, 0x800)

#define L2_CONFIG_RESET         0x06091004
#define L2_LIM_WAY_COUNT        15
#define L2_WAY_SIZE             (128 * KiB)
#define L2_CACHE_SIZE           (2 * MiB)
#define L2_ZERO_MAX_SIZE        (32 * MiB)

static void sifive_l2cc_update_l2lim(SiFiveL2CCState *s, uint64_t way_enable)
{
    uint64_t l2lim_size;

    if (way_enable >= L2_LIM_WAY_COUNT) {
        l2lim_size = 0;
    } else {
        l2lim_size = (L2_LIM_WAY_COUNT - way_enable) * L2_WAY_SIZE;
    }

    memory_region_transaction_begin();
    memory_region_set_size(&s->lim, l2lim_size);
    memory_region_set_enabled(&s->lim, l2lim_size != 0);
    memory_region_transaction_commit();
}

static uint64_t sifive_l2cc_way_enable_pre_write(RegisterInfo *reg,
                                                 uint64_t value)
{
    uint64_t current = *(uint64_t *)reg->data;

    return MAX(current, value);
}

static void sifive_l2cc_way_enable_post_write(RegisterInfo *reg,
                                              uint64_t value)
{
    SiFiveL2CCState *s = SIFIVE_L2CC(reg->opaque);

    sifive_l2cc_update_l2lim(s, value);
}

#define WAY_MASK_REGISTER(_id)                  \
    {                                           \
        .name = "WAY_MASK_" #_id,               \
        .addr = A_L2_WAY_MASK + 8 * (_id),       \
        .reset = UINT64_MAX,                    \
    }

static const RegisterAccessInfo sifive_l2cc_regs_info[] = {
    {
        .name = "CONFIG",
        .addr = A_L2_CONFIG,
        .reset = L2_CONFIG_RESET,
        .ro = UINT64_MAX,
    }, {
        .name = "WAY_ENABLE",
        .addr = A_L2_WAY_ENABLE,
        .rsvd = ~R_L2_WAY_ENABLE_VALUE_MASK,
        .pre_write = sifive_l2cc_way_enable_pre_write,
        .post_write = sifive_l2cc_way_enable_post_write,
    },
    WAY_MASK_REGISTER(0),
    WAY_MASK_REGISTER(1),
    WAY_MASK_REGISTER(2),
    WAY_MASK_REGISTER(3),
    WAY_MASK_REGISTER(4),
    WAY_MASK_REGISTER(5),
    WAY_MASK_REGISTER(6),
    WAY_MASK_REGISTER(7),
    WAY_MASK_REGISTER(8),
    WAY_MASK_REGISTER(9),
    WAY_MASK_REGISTER(10),
    WAY_MASK_REGISTER(11),
    WAY_MASK_REGISTER(12),
    WAY_MASK_REGISTER(13),
    WAY_MASK_REGISTER(14),
    WAY_MASK_REGISTER(15),
    WAY_MASK_REGISTER(16),
    WAY_MASK_REGISTER(17),
    WAY_MASK_REGISTER(18),
    WAY_MASK_REGISTER(19),
    WAY_MASK_REGISTER(20),
};

static bool sifive_l2cc_register_implemented(SiFiveL2CCState *s,
                                             hwaddr offset)
{
    if (offset == A_L2_CONFIG || offset == A_L2_WAY_ENABLE) {
        return true;
    }

    return offset >= A_L2_WAY_MASK &&
           offset < A_L2_WAY_MASK + 8 * s->num_masters && !(offset & 7);
}

static uint64_t sifive_l2cc_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    SiFiveL2CCState *s = SIFIVE_L2CC(reg_array->r[0]->opaque);

    if (sifive_l2cc_register_implemented(s, offset)) {
        return register_read_memory(opaque, offset, size);
    }

    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                  "(size %u, offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, offset);
    return 0;
}

static void sifive_l2cc_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    SiFiveL2CCState *s = SIFIVE_L2CC(reg_array->r[0]->opaque);

    if (sifive_l2cc_register_implemented(s, offset)) {
        register_write_memory(opaque, offset, value, size);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                  "(size %u, value 0x%" PRIx64
                  ", offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, value, offset);
}

static const MemoryRegionOps sifive_l2cc_ops = {
    .read = sifive_l2cc_read,
    .write = sifive_l2cc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl = { .min_access_size = 1, .max_access_size = 8 },
};

static void sifive_l2cc_reset_hold(Object *obj, ResetType type)
{
    SiFiveL2CCState *s = SIFIVE_L2CC(obj);
    size_t i;

    memset(s->regs, 0, sizeof(s->regs));
    for (i = 0; i < 2 + s->num_masters; i++) {
        hwaddr addr = sifive_l2cc_regs_info[i].addr;

        register_reset(&s->regs_info[addr / sizeof(uint64_t)]);
    }
}

static void sifive_l2cc_init(Object *obj)
{
    SiFiveL2CCState *s = SIFIVE_L2CC(obj);
    RegisterInfoArray *reg_array;

    reg_array = register_init_block64(
        DEVICE(obj), sifive_l2cc_regs_info,
        ARRAY_SIZE(sifive_l2cc_regs_info), s->regs_info, s->regs,
        &sifive_l2cc_ops, false, SIFIVE_L2CC_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &reg_array->mem);
}

static void sifive_l2cc_realize(DeviceState *dev, Error **errp)
{
    SiFiveL2CCState *s = SIFIVE_L2CC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (!s->num_masters || s->num_masters > SIFIVE_L2CC_MAX_MASTERS) {
        error_setg(errp, "num-masters must be between 1 and %u",
                   SIFIVE_L2CC_MAX_MASTERS);
        return;
    }
    if (!s->zero_size || s->zero_size > L2_ZERO_MAX_SIZE) {
        error_setg(errp, "zero-size must be between 1 byte and 32 MiB");
        return;
    }

    /* Keep the SRAM backing fixed while WayEnable changes the LIM alias */
    if (!memory_region_init_ram(&s->lim_ram, OBJECT(dev),
                                TYPE_SIFIVE_L2CC ".lim-ram",
                                L2_CACHE_SIZE, errp)) {
        return;
    }
    memory_region_init_alias(&s->lim, OBJECT(dev), TYPE_SIFIVE_L2CC ".lim",
                             &s->lim_ram, 0, L2_LIM_WAY_COUNT * L2_WAY_SIZE);
    sysbus_init_mmio(sbd, &s->lim);

    /*
     * L2 Zero allocates into enabled cache ways without external memory
     * backing. Use a separate RAM stand-in because QEMU does not model
     * cache allocation, replacement, or coherence. Its aperture size is
     * independent of the cache/LIM way partition.
     */
    if (!memory_region_init_ram(&s->zero, OBJECT(dev),
                                TYPE_SIFIVE_L2CC ".zero",
                                s->zero_size, errp)) {
        return;
    }
    sysbus_init_mmio(sbd, &s->zero);

    sifive_l2cc_update_l2lim(s, s->regs[R_L2_WAY_ENABLE]);
}

static const Property sifive_l2cc_properties[] = {
    DEFINE_PROP_UINT32("num-masters", SiFiveL2CCState, num_masters,
                       SIFIVE_L2CC_MAX_MASTERS),
    DEFINE_PROP_SIZE("zero-size", SiFiveL2CCState, zero_size,
                     L2_ZERO_MAX_SIZE),
};

static int sifive_l2cc_post_load(void *opaque, int version_id)
{
    SiFiveL2CCState *s = opaque;

    sifive_l2cc_update_l2lim(s, s->regs[R_L2_WAY_ENABLE]);
    return 0;
}

static const VMStateDescription vmstate_sifive_l2cc = {
    .name = TYPE_SIFIVE_L2CC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = sifive_l2cc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_EQUAL(num_masters, SiFiveL2CCState),
        VMSTATE_UINT64_EQUAL(zero_size, SiFiveL2CCState),
        VMSTATE_UINT64_ARRAY(regs, SiFiveL2CCState, SIFIVE_L2CC_REG_NUM),
        VMSTATE_END_OF_LIST()
    },
};

static void sifive_l2cc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = sifive_l2cc_reset_hold;
    device_class_set_props(dc, sifive_l2cc_properties);
    dc->realize = sifive_l2cc_realize;
    dc->vmsd = &vmstate_sifive_l2cc;
}

static const TypeInfo sifive_l2cc_info = {
    .name = TYPE_SIFIVE_L2CC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFiveL2CCState),
    .instance_init = sifive_l2cc_init,
    .class_init = sifive_l2cc_class_init,
};

static void sifive_l2cc_register_types(void)
{
    type_register_static(&sifive_l2cc_info);
}

type_init(sifive_l2cc_register_types)
