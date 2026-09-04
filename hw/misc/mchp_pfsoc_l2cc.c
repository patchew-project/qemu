/*
 * Microchip PolarFire SoC L2 cache controller
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
#include "hw/misc/mchp_pfsoc_l2cc.h"

REG64(L2_CONFIG, 0x000)
    FIELD(L2_CONFIG, BANKS, 0, 8)
    FIELD(L2_CONFIG, WAYS, 8, 8)
    FIELD(L2_CONFIG, SETS, 16, 8)
    FIELD(L2_CONFIG, BYTES, 24, 8)
REG64(L2_WAY_ENABLE, 0x008)
    FIELD(L2_WAY_ENABLE, VALUE, 0, 8)
REG64(L2_WAY_MASK_DMA, 0x800)
REG64(L2_WAY_MASK_AXI4_PORT_0, 0x808)
REG64(L2_WAY_MASK_AXI4_PORT_1, 0x810)
REG64(L2_WAY_MASK_AXI4_PORT_2, 0x818)
REG64(L2_WAY_MASK_AXI4_PORT_3, 0x820)
REG64(L2_WAY_MASK_HART0_DCACHE, 0x828)
REG64(L2_WAY_MASK_HART0_ICACHE, 0x830)
REG64(L2_WAY_MASK_HART1_DCACHE, 0x838)
REG64(L2_WAY_MASK_HART1_ICACHE, 0x840)
REG64(L2_WAY_MASK_HART2_DCACHE, 0x848)
REG64(L2_WAY_MASK_HART2_ICACHE, 0x850)
REG64(L2_WAY_MASK_HART3_DCACHE, 0x858)
REG64(L2_WAY_MASK_HART3_ICACHE, 0x860)
REG64(L2_WAY_MASK_HART4_DCACHE, 0x868)
REG64(L2_WAY_MASK_HART4_ICACHE, 0x870)

#define L2_CONFIG_RESET         0x06091004
#define L2_LIM_WAY_COUNT        15
#define L2_WAY_SIZE             (128 * KiB)

static void mchp_pfsoc_l2cc_update_l2lim(MchpPfSoCL2ccState *s,
                                         uint64_t way_enable)
{
    uint64_t l2lim_size;

    if (way_enable >= L2_LIM_WAY_COUNT) {
        l2lim_size = 0;
    } else {
        l2lim_size = (L2_LIM_WAY_COUNT - way_enable) * L2_WAY_SIZE;
    }

    memory_region_transaction_begin();
    memory_region_set_size(s->l2lim, l2lim_size);
    memory_region_set_enabled(s->l2lim, l2lim_size != 0);
    memory_region_transaction_commit();
}

static uint64_t mchp_pfsoc_l2cc_way_enable_pre_write(RegisterInfo *reg,
                                                     uint64_t value)
{
    uint64_t current = *(uint64_t *)reg->data;

    return MAX(current, value);
}

static void mchp_pfsoc_l2cc_way_enable_post_write(RegisterInfo *reg,
                                                  uint64_t value)
{
    MchpPfSoCL2ccState *s = MCHP_PFSOC_L2CC(reg->opaque);

    mchp_pfsoc_l2cc_update_l2lim(s, value);
}

#define WAY_MASK_REGISTER(_name)                \
    {                                           \
        .name = "WAY_MASK_" #_name,             \
        .addr = A_L2_WAY_MASK_ ## _name,        \
        .reset = UINT64_MAX,                    \
    }

static const RegisterAccessInfo mchp_pfsoc_l2cc_regs_info[] = {
    {
        .name = "CONFIG",
        .addr = A_L2_CONFIG,
        .reset = L2_CONFIG_RESET,
        .ro = UINT64_MAX,
    }, {
        .name = "WAY_ENABLE",
        .addr = A_L2_WAY_ENABLE,
        .rsvd = ~R_L2_WAY_ENABLE_VALUE_MASK,
        .pre_write = mchp_pfsoc_l2cc_way_enable_pre_write,
        .post_write = mchp_pfsoc_l2cc_way_enable_post_write,
    },
    WAY_MASK_REGISTER(DMA),
    WAY_MASK_REGISTER(AXI4_PORT_0),
    WAY_MASK_REGISTER(AXI4_PORT_1),
    WAY_MASK_REGISTER(AXI4_PORT_2),
    WAY_MASK_REGISTER(AXI4_PORT_3),
    WAY_MASK_REGISTER(HART0_DCACHE),
    WAY_MASK_REGISTER(HART0_ICACHE),
    WAY_MASK_REGISTER(HART1_DCACHE),
    WAY_MASK_REGISTER(HART1_ICACHE),
    WAY_MASK_REGISTER(HART2_DCACHE),
    WAY_MASK_REGISTER(HART2_ICACHE),
    WAY_MASK_REGISTER(HART3_DCACHE),
    WAY_MASK_REGISTER(HART3_ICACHE),
    WAY_MASK_REGISTER(HART4_DCACHE),
    WAY_MASK_REGISTER(HART4_ICACHE),
};

static bool mchp_pfsoc_l2cc_register_implemented(hwaddr offset)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(mchp_pfsoc_l2cc_regs_info); i++) {
        if (mchp_pfsoc_l2cc_regs_info[i].addr == offset) {
            return true;
        }
    }

    return false;
}

static uint64_t mchp_pfsoc_l2cc_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    if (mchp_pfsoc_l2cc_register_implemented(offset)) {
        return register_read_memory(opaque, offset, size);
    }

    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device read "
                  "(size %u, offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, offset);
    return 0;
}

static void mchp_pfsoc_l2cc_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    if (mchp_pfsoc_l2cc_register_implemented(offset)) {
        register_write_memory(opaque, offset, value, size);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "%s: unimplemented device write "
                  "(size %u, value 0x%" PRIx64
                  ", offset 0x%" HWADDR_PRIx ")\n",
                  __func__, size, value, offset);
}

static const MemoryRegionOps mchp_pfsoc_l2cc_ops = {
    .read = mchp_pfsoc_l2cc_read,
    .write = mchp_pfsoc_l2cc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl = { .min_access_size = 1, .max_access_size = 8 },
};

static void mchp_pfsoc_l2cc_reset_hold(Object *obj, ResetType type)
{
    MchpPfSoCL2ccState *s = MCHP_PFSOC_L2CC(obj);
    size_t i;

    memset(s->regs, 0, sizeof(s->regs));
    for (i = 0; i < ARRAY_SIZE(mchp_pfsoc_l2cc_regs_info); i++) {
        hwaddr addr = mchp_pfsoc_l2cc_regs_info[i].addr;

        register_reset(&s->regs_info[addr / sizeof(uint64_t)]);
    }
}

static void mchp_pfsoc_l2cc_init(Object *obj)
{
    MchpPfSoCL2ccState *s = MCHP_PFSOC_L2CC(obj);
    RegisterInfoArray *reg_array;

    reg_array = register_init_block64(
        DEVICE(obj), mchp_pfsoc_l2cc_regs_info,
        ARRAY_SIZE(mchp_pfsoc_l2cc_regs_info), s->regs_info, s->regs,
        &mchp_pfsoc_l2cc_ops, false, MCHP_PFSOC_L2CC_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &reg_array->mem);
}

static void mchp_pfsoc_l2cc_realize(DeviceState *dev, Error **errp)
{
    MchpPfSoCL2ccState *s = MCHP_PFSOC_L2CC(dev);

    if (!s->l2lim) {
        error_setg(errp, TYPE_MCHP_PFSOC_L2CC ": 'l2-lim' link not set");
        return;
    }

    mchp_pfsoc_l2cc_update_l2lim(s, s->regs[R_L2_WAY_ENABLE]);
}

static const Property mchp_pfsoc_l2cc_properties[] = {
    DEFINE_PROP_LINK("l2-lim", MchpPfSoCL2ccState, l2lim,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void mchp_pfsoc_l2cc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = mchp_pfsoc_l2cc_reset_hold;
    device_class_set_props(dc, mchp_pfsoc_l2cc_properties);
    dc->realize = mchp_pfsoc_l2cc_realize;
}

static const TypeInfo mchp_pfsoc_l2cc_info = {
    .name = TYPE_MCHP_PFSOC_L2CC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MchpPfSoCL2ccState),
    .instance_init = mchp_pfsoc_l2cc_init,
    .class_init = mchp_pfsoc_l2cc_class_init,
};

static void mchp_pfsoc_l2cc_register_types(void)
{
    type_register_static(&mchp_pfsoc_l2cc_info);
}

type_init(mchp_pfsoc_l2cc_register_types)
