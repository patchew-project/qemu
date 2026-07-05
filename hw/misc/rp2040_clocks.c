/*
 * RP2040 clocks emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/qdev-clock.h"
#include "hw/misc/rp2040_clocks.h"
#include "hw/misc/rp2040_nyi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define ROSC_HZ     6000000
#define XOSC_HZ     12000000

#define CLK_GPOUT0_CTRL      0x00
#define CLK_REF_CTRL         0x30
#define CLK_REF_DIV          0x34
#define CLK_REF_SELECTED     0x38
#define CLK_SYS_CTRL         0x3c
#define CLK_SYS_DIV          0x40
#define CLK_SYS_SELECTED     0x44
#define CLK_PERI_CTRL        0x48
#define CLK_PERI_DIV         0x4c
#define CLK_PERI_SELECTED    0x50
#define CLK_USB_CTRL         0x54
#define CLK_USB_DIV          0x58
#define CLK_USB_SELECTED     0x5c
#define CLK_ADC_CTRL         0x60
#define CLK_ADC_DIV          0x64
#define CLK_ADC_SELECTED     0x68
#define CLK_RTC_CTRL         0x6c
#define CLK_RTC_DIV          0x70
#define CLK_RTC_SELECTED     0x74
#define FC0_REF_KHZ          0x80
#define FC0_MIN_KHZ          0x84
#define FC0_MAX_KHZ          0x88
#define FC0_DELAY            0x8c
#define FC0_INTERVAL         0x90
#define FC0_SRC              0x94
#define FC0_STATUS           0x98
#define FC0_RESULT           0x9c
#define WAKE_EN0             0xa0
#define WAKE_EN1             0xa4
#define SLEEP_EN0            0xa8
#define SLEEP_EN1            0xac
#define ENABLED0             0xb0
#define ENABLED1             0xb4
#define INTR                 0xb8

#define CTRL_ENABLE          BIT(11)

#define ATOMIC_ALIAS_MASK    0x3000
#define ATOMIC_XOR           0x1000
#define ATOMIC_SET           0x2000
#define ATOMIC_CLR           0x3000

static void rp2040_clocks_update(RP2040ClocksState *s);

static uint32_t rp2040_clocks_div(uint32_t reg)
{
    uint32_t div = reg >> 8;

    return div == 0 ? 1u << 16 : div;
}

static unsigned rp2040_clocks_hz(Clock *clk)
{
    return MIN(clock_get_hz(clk), UINT_MAX);
}

static unsigned rp2040_clocks_sys_aux_freq(RP2040ClocksState *s,
                                           uint32_t auxsrc)
{
    switch (auxsrc & 0x7) {
    case 0x0:
        return rp2040_clocks_hz(s->pll_sys);
    case 0x1:
        return rp2040_clocks_hz(s->pll_usb);
    case 0x2:
        return ROSC_HZ;
    case 0x3:
        return XOSC_HZ;
    default:
        return 0;
    }
}

static unsigned rp2040_clocks_ref_aux_freq(RP2040ClocksState *s,
                                           uint32_t auxsrc)
{
    switch (auxsrc & 0x3) {
    case 0x0:
        return rp2040_clocks_hz(s->pll_usb);
    case 0x1:
        return ROSC_HZ;
    case 0x2:
        return XOSC_HZ;
    default:
        return 0;
    }
}

static unsigned rp2040_clocks_peri_aux_freq(RP2040ClocksState *s,
                                            uint32_t auxsrc)
{
    switch (auxsrc & 0x7) {
    case 0x0:
        return rp2040_clocks_hz(s->clk_sys);
    case 0x1:
        return rp2040_clocks_hz(s->pll_sys);
    case 0x2:
        return rp2040_clocks_hz(s->pll_usb);
    case 0x3:
        return ROSC_HZ;
    case 0x4:
        return XOSC_HZ;
    default:
        return 0;
    }
}

static unsigned rp2040_clocks_usb_adc_rtc_aux_freq(RP2040ClocksState *s,
                                                   uint32_t auxsrc)
{
    switch (auxsrc & 0x7) {
    case 0x0:
        return rp2040_clocks_hz(s->pll_usb);
    case 0x1:
        return rp2040_clocks_hz(s->pll_sys);
    case 0x2:
        return ROSC_HZ;
    case 0x3:
        return XOSC_HZ;
    default:
        return 0;
    }
}

static unsigned rp2040_clocks_ctrl_auxsrc(uint32_t ctrl)
{
    return extract32(ctrl, 5, 4);
}

static unsigned rp2040_clocks_ref_freq(RP2040ClocksState *s)
{
    uint32_t ctrl = s->regs[CLK_REF_CTRL / 4];
    uint32_t src = extract32(ctrl, 0, 2);
    unsigned freq;

    switch (src) {
    case 0:
        freq = ROSC_HZ;
        break;
    case 1:
        freq = rp2040_clocks_ref_aux_freq(s,
                                          rp2040_clocks_ctrl_auxsrc(ctrl));
        break;
    case 2:
        freq = XOSC_HZ;
        break;
    default:
        freq = 0;
        break;
    }

    return freq / rp2040_clocks_div(s->regs[CLK_REF_DIV / 4]);
}

static unsigned rp2040_clocks_sys_freq(RP2040ClocksState *s)
{
    uint32_t ctrl = s->regs[CLK_SYS_CTRL / 4];
    uint32_t src = extract32(ctrl, 0, 1);
    unsigned freq;

    if (src) {
        freq = rp2040_clocks_sys_aux_freq(s,
                                          rp2040_clocks_ctrl_auxsrc(ctrl));
    } else {
        freq = rp2040_clocks_ref_freq(s);
    }

    return freq / rp2040_clocks_div(s->regs[CLK_SYS_DIV / 4]);
}

static unsigned rp2040_clocks_peri_freq(RP2040ClocksState *s)
{
    uint32_t ctrl = s->regs[CLK_PERI_CTRL / 4];

    if (!(ctrl & CTRL_ENABLE)) {
        return 0;
    }

    return rp2040_clocks_peri_aux_freq(s, rp2040_clocks_ctrl_auxsrc(ctrl)) /
           rp2040_clocks_div(s->regs[CLK_PERI_DIV / 4]);
}

static unsigned rp2040_clocks_usb_adc_rtc_freq(RP2040ClocksState *s,
                                               uint32_t ctrl_off,
                                               uint32_t div_off)
{
    uint32_t ctrl = s->regs[ctrl_off / 4];

    if (!(ctrl & CTRL_ENABLE)) {
        return 0;
    }

    return rp2040_clocks_usb_adc_rtc_aux_freq(s,
                                              rp2040_clocks_ctrl_auxsrc(ctrl)) /
           rp2040_clocks_div(s->regs[div_off / 4]);
}

static void rp2040_clocks_update(RP2040ClocksState *s)
{
    unsigned ref_hz = rp2040_clocks_ref_freq(s);
    unsigned sys_hz = rp2040_clocks_sys_freq(s);

    clock_update_hz(s->clk_ref, ref_hz);
    clock_update_hz(s->clk_sys, sys_hz);
    clock_update_hz(s->clk_peri, rp2040_clocks_peri_freq(s));
    clock_update_hz(s->clk_usb,
                    rp2040_clocks_usb_adc_rtc_freq(s, CLK_USB_CTRL,
                                                   CLK_USB_DIV));
    clock_update_hz(s->clk_adc,
                    rp2040_clocks_usb_adc_rtc_freq(s, CLK_ADC_CTRL,
                                                   CLK_ADC_DIV));
    clock_update_hz(s->clk_rtc,
                    rp2040_clocks_usb_adc_rtc_freq(s, CLK_RTC_CTRL,
                                                   CLK_RTC_DIV));
}

static uint32_t rp2040_clocks_selected(RP2040ClocksState *s, hwaddr offset)
{
    switch (offset) {
    case CLK_REF_SELECTED:
        return 1u << extract32(s->regs[CLK_REF_CTRL / 4], 0, 2);
    case CLK_SYS_SELECTED:
        return 1u << extract32(s->regs[CLK_SYS_CTRL / 4], 0, 1);
    case CLK_PERI_SELECTED:
    case CLK_USB_SELECTED:
    case CLK_ADC_SELECTED:
    case CLK_RTC_SELECTED:
        return 1;
    default:
        return 0;
    }
}

static uint32_t rp2040_clocks_fc0_result(RP2040ClocksState *s)
{
    unsigned khz;

    switch (s->regs[FC0_SRC / 4] & 0xff) {
    case 0x01:
        khz = rp2040_clocks_hz(s->pll_sys) / 1000;
        break;
    case 0x02:
        khz = rp2040_clocks_hz(s->pll_usb) / 1000;
        break;
    case 0x03:
    case 0x04:
        khz = ROSC_HZ / 1000;
        break;
    case 0x05:
        khz = XOSC_HZ / 1000;
        break;
    case 0x08:
        khz = rp2040_clocks_ref_freq(s) / 1000;
        break;
    case 0x09:
        khz = rp2040_clocks_sys_freq(s) / 1000;
        break;
    case 0x0a:
        khz = clock_get_hz(s->clk_peri) / 1000;
        break;
    case 0x0b:
        khz = clock_get_hz(s->clk_usb) / 1000;
        break;
    case 0x0c:
        khz = clock_get_hz(s->clk_adc) / 1000;
        break;
    case 0x0d:
        khz = clock_get_hz(s->clk_rtc) / 1000;
        break;
    default:
        khz = 0;
        break;
    }

    return khz << 5;
}

static bool rp2040_clocks_is_selected(hwaddr offset)
{
    return offset == CLK_REF_SELECTED ||
           offset == CLK_SYS_SELECTED ||
           offset == CLK_PERI_SELECTED ||
           offset == CLK_USB_SELECTED ||
           offset == CLK_ADC_SELECTED ||
           offset == CLK_RTC_SELECTED;
}

static uint64_t rp2040_clocks_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040ClocksState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint64_t value;

    if (offset >= sizeof(s->regs)) {
        value = 0;
        rp2040_log_unimplemented_read("clocks", size,
                                      RP2040_CLOCKS_BASE + addr, offset,
                                      value);
    } else if (rp2040_clocks_is_selected(offset)) {
        value = rp2040_clocks_selected(s, offset);
    } else {
        switch (offset) {
        case FC0_STATUS:
            value = BIT(4);
            break;
        case FC0_RESULT:
            value = rp2040_clocks_fc0_result(s);
            break;
        case ENABLED0:
        case ENABLED1:
            value = s->regs[(offset - 0x10) / 4];
            break;
        default:
            value = s->regs[offset / 4];
            break;
        }
    }

    return value;
}

static void rp2040_clocks_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    RP2040ClocksState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t old;
    uint32_t newval = value;

    if (offset < sizeof(s->regs) && !rp2040_clocks_is_selected(offset)) {
        old = s->regs[offset / 4];
        switch (alias) {
        case ATOMIC_XOR:
            newval = old ^ value;
            break;
        case ATOMIC_SET:
            newval = old | value;
            break;
        case ATOMIC_CLR:
            newval = old & ~value;
            break;
        default:
            break;
        }

        switch (offset) {
        case ENABLED0:
        case ENABLED1:
        case FC0_STATUS:
        case FC0_RESULT:
        case INTR:
            break;
        default:
            s->regs[offset / 4] = newval;
            rp2040_clocks_update(s);
            break;
        }
    } else if (offset >= sizeof(s->regs)) {
        rp2040_log_unimplemented_write("clocks", size,
                                       RP2040_CLOCKS_BASE + addr, offset,
                                       value);
    }
}

static void rp2040_clocks_input_update(void *opaque, ClockEvent event)
{
    rp2040_clocks_update(opaque);
}

static const MemoryRegionOps rp2040_clocks_ops = {
    .read = rp2040_clocks_read,
    .write = rp2040_clocks_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_clocks_reset(DeviceState *dev)
{
    RP2040ClocksState *s = RP2040_CLOCKS(dev);

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[CLK_REF_DIV / 4] = 0x00000100;
    s->regs[CLK_SYS_DIV / 4] = 0x00000100;
    s->regs[CLK_PERI_CTRL / 4] = CTRL_ENABLE;
    s->regs[CLK_PERI_DIV / 4] = 0x00000100;
    s->regs[CLK_USB_CTRL / 4] = CTRL_ENABLE | (0x3 << 5);
    s->regs[CLK_USB_DIV / 4] = 0x00000100;
    s->regs[CLK_ADC_CTRL / 4] = CTRL_ENABLE | (0x3 << 5);
    s->regs[CLK_ADC_DIV / 4] = 0x00000100;
    s->regs[CLK_RTC_CTRL / 4] = CTRL_ENABLE | (0xa << 5);
    s->regs[CLK_RTC_DIV / 4] = 0x00000100;
    s->regs[FC0_REF_KHZ / 4] = XOSC_HZ / 1000;
    s->regs[WAKE_EN0 / 4] = 0xffffffff;
    s->regs[WAKE_EN1 / 4] = 0xffffffff;
    s->regs[SLEEP_EN0 / 4] = 0xffffffff;
    s->regs[SLEEP_EN1 / 4] = 0xffffffff;

    rp2040_clocks_update(s);
}

static void rp2040_clocks_init(Object *obj)
{
    RP2040ClocksState *s = RP2040_CLOCKS(obj);
    DeviceState *dev = DEVICE(obj);

    s->clk_ref = qdev_init_clock_out(dev, "clk-ref");
    s->clk_sys = qdev_init_clock_out(dev, "clk-sys");
    s->clk_peri = qdev_init_clock_out(dev, "clk-peri");
    s->clk_usb = qdev_init_clock_out(dev, "clk-usb");
    s->clk_adc = qdev_init_clock_out(dev, "clk-adc");
    s->clk_rtc = qdev_init_clock_out(dev, "clk-rtc");
    s->pll_sys = qdev_init_clock_in(dev, "pll-sys",
                                    rp2040_clocks_input_update, s, 0);
    s->pll_usb = qdev_init_clock_in(dev, "pll-usb",
                                    rp2040_clocks_input_update, s, 0);

    memory_region_init_io(&s->iomem, obj, &rp2040_clocks_ops, s,
                          "rp2040.clocks", RP2040_CLOCKS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_clocks_vmstate = {
    .name = TYPE_RP2040_CLOCKS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, RP2040ClocksState, 0x100 / 4),
        VMSTATE_CLOCK(clk_ref, RP2040ClocksState),
        VMSTATE_CLOCK(clk_sys, RP2040ClocksState),
        VMSTATE_CLOCK(clk_peri, RP2040ClocksState),
        VMSTATE_CLOCK(clk_usb, RP2040ClocksState),
        VMSTATE_CLOCK(clk_adc, RP2040ClocksState),
        VMSTATE_CLOCK(clk_rtc, RP2040ClocksState),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_clocks_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_clocks_reset);
    dc->vmsd = &rp2040_clocks_vmstate;
}

static const TypeInfo rp2040_clocks_info = {
    .name          = TYPE_RP2040_CLOCKS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040ClocksState),
    .instance_init = rp2040_clocks_init,
    .class_init    = rp2040_clocks_class_init,
};

static void rp2040_clocks_register_types(void)
{
    type_register_static(&rp2040_clocks_info);
}
type_init(rp2040_clocks_register_types)
