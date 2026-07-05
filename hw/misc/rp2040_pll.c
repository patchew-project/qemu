/*
 * RP2040 PLL emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_pll.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define XOSC_HZ             12000000

#define PLL_CS              0x00
#define PLL_PWR             0x04
#define PLL_FBDIV_INT       0x08
#define PLL_PRIM            0x0c

#define PLL_CS_LOCK         BIT(31)
#define PLL_CS_BYPASS       BIT(8)
#define PLL_CS_REFDIV_MASK  0x3f

#define PLL_PWR_VCOPD       BIT(5)
#define PLL_PWR_POSTDIVPD   BIT(3)
#define PLL_PWR_DSMPD       BIT(2)
#define PLL_PWR_PD          BIT(0)
#define PLL_PWR_MASK        (PLL_PWR_VCOPD | PLL_PWR_POSTDIVPD | \
                             PLL_PWR_DSMPD | PLL_PWR_PD)

#define PLL_FBDIV_MASK      0x00000fff
#define PLL_PRIM_MASK       0x00077000

#define ATOMIC_ALIAS_MASK   0x3000
#define ATOMIC_XOR          0x1000
#define ATOMIC_SET          0x2000
#define ATOMIC_CLR          0x3000

static uint32_t rp2040_pll_apply_alias(uint32_t old, uint32_t value,
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

static bool rp2040_pll_locked(RP2040PllState *s)
{
    return !(s->pwr & (PLL_PWR_PD | PLL_PWR_VCOPD));
}

static unsigned rp2040_pll_output_hz(RP2040PllState *s)
{
    uint32_t refdiv = s->cs & PLL_CS_REFDIV_MASK;
    uint32_t fbdiv = s->fbdiv_int & PLL_FBDIV_MASK;
    uint32_t postdiv1 = extract32(s->prim, 16, 3);
    uint32_t postdiv2 = extract32(s->prim, 12, 3);
    uint64_t hz;

    if (!rp2040_pll_locked(s) || (s->pwr & PLL_PWR_POSTDIVPD)) {
        return 0;
    }

    refdiv = refdiv ? refdiv : 1;
    if (s->cs & PLL_CS_BYPASS) {
        return XOSC_HZ / refdiv;
    }

    if (!fbdiv || !postdiv1 || !postdiv2) {
        return s->fallback_hz;
    }

    hz = XOSC_HZ;
    hz = hz * fbdiv / refdiv / postdiv1 / postdiv2;
    return hz;
}

static void rp2040_pll_update_clock(RP2040PllState *s)
{
    clock_update_hz(s->clk, rp2040_pll_output_hz(s));
}

static uint64_t rp2040_pll_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040PllState *s = opaque;
    hwaddr offset = addr & 0xfff;
    const char *name = s->trace_name ? s->trace_name : "rp2040.pll";
    uint64_t value;

    switch (offset) {
    case PLL_CS:
        value = s->cs & ~PLL_CS_LOCK;
        if (rp2040_pll_locked(s)) {
            value |= PLL_CS_LOCK;
        }
        break;
    case PLL_PWR:
        value = s->pwr;
        break;
    case PLL_FBDIV_INT:
        value = s->fbdiv_int;
        break;
    case PLL_PRIM:
        value = s->prim;
        break;
    default:
        value = 0;
        rp2040_log_unimplemented_read(name, size, s->base + addr, offset,
                                      value);
        break;
    }

    return value;
}

static void rp2040_pll_write(void *opaque, hwaddr addr,
                             uint64_t value64, unsigned size)
{
    RP2040PllState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    const char *name = s->trace_name ? s->trace_name : "rp2040.pll";
    uint32_t value = value64;

    switch (offset) {
    case PLL_CS:
        s->cs = rp2040_pll_apply_alias(s->cs, value, alias) &
                (PLL_CS_BYPASS | PLL_CS_REFDIV_MASK);
        break;
    case PLL_PWR:
        s->pwr = rp2040_pll_apply_alias(s->pwr, value, alias) & PLL_PWR_MASK;
        break;
    case PLL_FBDIV_INT:
        s->fbdiv_int = rp2040_pll_apply_alias(s->fbdiv_int, value, alias) &
                       PLL_FBDIV_MASK;
        break;
    case PLL_PRIM:
        s->prim = rp2040_pll_apply_alias(s->prim, value, alias) &
                  PLL_PRIM_MASK;
        break;
    default:
        rp2040_log_unimplemented_write(name, size, s->base + addr, offset,
                                       value64);
        break;
    }

    rp2040_pll_update_clock(s);
}

static const MemoryRegionOps rp2040_pll_ops = {
    .read = rp2040_pll_read,
    .write = rp2040_pll_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_pll_reset(DeviceState *dev)
{
    RP2040PllState *s = RP2040_PLL(dev);

    s->cs = 1;
    s->pwr = PLL_PWR_MASK;
    s->fbdiv_int = 0;
    s->prim = (7 << 16) | (7 << 12);

    rp2040_pll_update_clock(s);
}

static void rp2040_pll_init(Object *obj)
{
    RP2040PllState *s = RP2040_PLL(obj);
    DeviceState *dev = DEVICE(obj);

    s->clk = qdev_init_clock_out(dev, "clk");
    memory_region_init_io(&s->iomem, obj, &rp2040_pll_ops, s,
                          "rp2040.pll", RP2040_PLL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_pll_vmstate = {
    .name = TYPE_RP2040_PLL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cs, RP2040PllState),
        VMSTATE_UINT32(pwr, RP2040PllState),
        VMSTATE_UINT32(fbdiv_int, RP2040PllState),
        VMSTATE_UINT32(prim, RP2040PllState),
        VMSTATE_CLOCK(clk, RP2040PllState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property rp2040_pll_properties[] = {
    DEFINE_PROP_STRING("trace-name", RP2040PllState, trace_name),
    DEFINE_PROP_UINT32("base", RP2040PllState, base, 0),
    DEFINE_PROP_UINT32("fallback-hz", RP2040PllState, fallback_hz, 0),
};

static void rp2040_pll_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_pll_reset);
    device_class_set_props(dc, rp2040_pll_properties);
    dc->vmsd = &rp2040_pll_vmstate;
}

static const TypeInfo rp2040_pll_info = {
    .name          = TYPE_RP2040_PLL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040PllState),
    .instance_init = rp2040_pll_init,
    .class_init    = rp2040_pll_class_init,
};

static void rp2040_pll_register_types(void)
{
    type_register_static(&rp2040_pll_info);
}
type_init(rp2040_pll_register_types)
