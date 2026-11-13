/*
 * Rockchip RK3588 clock registers
 *
 * Copyright (C) 2026 Process Mission
 *
 * Authors:
 *    Chao Liu <chao.liu@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/register.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_ROCKCHIP_CRU "rockchip-cru"
OBJECT_DECLARE_SIMPLE_TYPE(RockchipCRUState, ROCKCHIP_CRU)

#define ROCKCHIP_CRU_SIZE 0x5c000
#define ROCKCHIP_CRU_NUM_PLL_REGS (5 * 3 + 5 * 4)
#define ROCKCHIP_CRU_NUM_REGS (ROCKCHIP_CRU_NUM_PLL_REGS + 11)

struct RockchipCRUState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    RegisterInfo regs_info[ROCKCHIP_CRU_NUM_REGS];
    uint32_t regs[ROCKCHIP_CRU_NUM_REGS];
};

#define PLL_POWER_DOWN BIT(13)
#define PLL_LOCKED BIT(15)

static uint64_t rockchip_cru_masked_write(RegisterInfo *reg, uint64_t value)
{
    uint32_t old = *(uint32_t *)reg->data;
    uint16_t mask = value >> 16;

    return (old & ~mask) | (value & mask);
}

static RegisterInfo *rockchip_cru_register(RockchipCRUState *s, hwaddr offset)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(s->regs_info); i++) {
        if (s->regs_info[i].access->addr == offset) {
            return &s->regs_info[i];
        }
    }
    return NULL;
}

static uint64_t rockchip_cru_pll_lock(RegisterInfo *reg, uint64_t value)
{
    RockchipCRUState *s = ROCKCHIP_CRU(reg->opaque);
    hwaddr base = reg->access->addr - 0x18;
    uint32_t con0 = *(uint32_t *)rockchip_cru_register(s, base)->data;
    uint32_t con1 = *(uint32_t *)rockchip_cru_register(s, base + 4)->data;
    bool powered = !(con1 & PLL_POWER_DOWN);
    bool configured = (con0 & 0x3ff) >= 64 &&
                      (con1 & 0x3f) &&
                      extract32(con1, 6, 3) <= 6;

    /* PLL settling is synchronous after configuration and power-up. */
    return powered && configured ? PLL_LOCKED : 0;
}

#define CRU_PLL_CON0(label, base) \
    { .name = label "_CON0", .addr = base, \
      .pre_write = rockchip_cru_masked_write }

#define CRU_PLL_CON1(label, base) \
    { .name = label "_CON1", .addr = base + 4, \
      .pre_write = rockchip_cru_masked_write }

#define CRU_PLL_CON2(label, base) \
    { .name = label "_CON2", .addr = base + 8, .ro = 0xffff0000 }

#define CRU_PLL_CON6(label, base) \
    { .name = label "_CON6", .addr = base + 0x18, .ro = UINT32_MAX, \
      .post_read = rockchip_cru_pll_lock }

#define CRU_CLOCK(label, offset) \
    { .name = label, .addr = offset, .pre_write = rockchip_cru_masked_write }

static const RegisterAccessInfo rockchip_cru_pll_regs[] = {
    CRU_PLL_CON0("B0PLL", 0x50000),
    CRU_PLL_CON1("B0PLL", 0x50000),
    CRU_PLL_CON6("B0PLL", 0x50000),
    CRU_PLL_CON0("B1PLL", 0x52020),
    CRU_PLL_CON1("B1PLL", 0x52020),
    CRU_PLL_CON6("B1PLL", 0x52020),
    CRU_PLL_CON0("LPLL", 0x58040),
    CRU_PLL_CON1("LPLL", 0x58040),
    CRU_PLL_CON6("LPLL", 0x58040),
    CRU_PLL_CON0("V0PLL", 0x00160),
    CRU_PLL_CON1("V0PLL", 0x00160),
    CRU_PLL_CON2("V0PLL", 0x00160),
    CRU_PLL_CON6("V0PLL", 0x00160),
    CRU_PLL_CON0("AUPLL", 0x00180),
    CRU_PLL_CON1("AUPLL", 0x00180),
    CRU_PLL_CON2("AUPLL", 0x00180),
    CRU_PLL_CON6("AUPLL", 0x00180),
    CRU_PLL_CON0("CPLL", 0x001a0),
    CRU_PLL_CON1("CPLL", 0x001a0),
    CRU_PLL_CON2("CPLL", 0x001a0),
    CRU_PLL_CON6("CPLL", 0x001a0),
    CRU_PLL_CON0("GPLL", 0x001c0),
    CRU_PLL_CON1("GPLL", 0x001c0),
    CRU_PLL_CON2("GPLL", 0x001c0),
    CRU_PLL_CON6("GPLL", 0x001c0),
    CRU_PLL_CON0("NPLL", 0x001e0),
    CRU_PLL_CON1("NPLL", 0x001e0),
    CRU_PLL_CON6("NPLL", 0x001e0),
    CRU_PLL_CON0("PPLL", 0x08200),
    CRU_PLL_CON1("PPLL", 0x08200),
    CRU_PLL_CON2("PPLL", 0x08200),
    CRU_PLL_CON6("PPLL", 0x08200),
    CRU_PLL_CON0("SPLL", 0x18220),
    CRU_PLL_CON1("SPLL", 0x18220),
    CRU_PLL_CON6("SPLL", 0x18220),
};

static const RegisterAccessInfo rockchip_cru_clock_regs[] = {
    CRU_CLOCK("MODE_CON0", 0x00280),
    CRU_CLOCK("SBUS_MODE_CON0", 0x18280),
    CRU_CLOCK("CLKSEL_CON8", 0x00320),
    CRU_CLOCK("CLKSEL_CON9", 0x00324),
    CRU_CLOCK("CLKSEL_CON38", 0x00398),
    CRU_CLOCK("CLKSEL_CON78", 0x00438),
    CRU_CLOCK("CLKSEL_CON165", 0x00594),
    CRU_CLOCK("CLKGATE_CON19", 0x0084c),
    CRU_CLOCK("BIGCORE0_MODE_CON0", 0x50280),
    CRU_CLOCK("BIGCORE1_MODE_CON0", 0x52280),
    CRU_CLOCK("DSU_MODE_CON0", 0x58280),
};

G_STATIC_ASSERT(ARRAY_SIZE(rockchip_cru_pll_regs) +
                ARRAY_SIZE(rockchip_cru_clock_regs) == ROCKCHIP_CRU_NUM_REGS);

static uint64_t rockchip_cru_read(void *opaque, hwaddr offset, unsigned size)
{
    RockchipCRUState *s = opaque;
    RegisterInfo *reg = rockchip_cru_register(s, offset);

    if (reg) {
        return register_read(reg, UINT32_MAX, TYPE_ROCKCHIP_CRU, false);
    }
    qemu_log_mask(LOG_UNIMP, "rockchip-cru: unimplemented read at 0x%"
                  HWADDR_PRIx "\n", offset);
    return 0;
}

static void rockchip_cru_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    RockchipCRUState *s = opaque;
    RegisterInfo *reg = rockchip_cru_register(s, offset);

    if (reg) {
        register_write(reg, value, UINT32_MAX, TYPE_ROCKCHIP_CRU, false);
        return;
    }
    qemu_log_mask(LOG_UNIMP, "rockchip-cru: unimplemented write at 0x%"
                  HWADDR_PRIx " = 0x%" PRIx64 "\n", offset, value);
}

static const MemoryRegionOps rockchip_cru_ops = {
    .read = rockchip_cru_read,
    .write = rockchip_cru_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void rockchip_cru_reset(DeviceState *dev)
{
    RockchipCRUState *s = ROCKCHIP_CRU(dev);

    for (unsigned int i = 0; i < ARRAY_SIZE(s->regs_info); i++) {
        register_reset(&s->regs_info[i]);
    }
}

static void rockchip_cru_init(Object *obj)
{
    RockchipCRUState *s = ROCKCHIP_CRU(obj);

    /* Keep sparse CRU addresses in a compact RegisterInfo array. */
    for (unsigned int i = 0; i < ARRAY_SIZE(s->regs_info); i++) {
        const RegisterAccessInfo *access;
        unsigned int pll_regs = ARRAY_SIZE(rockchip_cru_pll_regs);

        if (i < pll_regs) {
            access = &rockchip_cru_pll_regs[i];
        } else {
            access = &rockchip_cru_clock_regs[i - pll_regs];
        }
        s->regs_info[i] = (RegisterInfo) {
            .data = &s->regs[i],
            .data_size = sizeof(s->regs[i]),
            .access = access,
            .opaque = s,
        };
    }
    memory_region_init_io(&s->iomem, obj, &rockchip_cru_ops, s,
                          TYPE_ROCKCHIP_CRU, ROCKCHIP_CRU_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void rockchip_cru_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, rockchip_cru_reset);
}

static const TypeInfo rockchip_cru_type_info = {
    .name = TYPE_ROCKCHIP_CRU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RockchipCRUState),
    .instance_init = rockchip_cru_init,
    .class_init = rockchip_cru_class_init,
};

static void rockchip_cru_register_types(void)
{
    type_register_static(&rockchip_cru_type_info);
}

type_init(rockchip_cru_register_types)
