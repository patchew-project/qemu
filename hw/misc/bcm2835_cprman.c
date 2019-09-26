/*
 * BCM2835 Clock/Power/Reset Manager subsystem (poor man's version)
 *
 * Copyright (C) 2018 Guenter Roeck <linux@roeck-us.net>
 * Copyright (C) 2018 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "sysemu/runstate.h"
#include "hw/registerfields.h"
#include "hw/misc/bcm2835_cprman.h"
#include "trace.h"

#define CPRMAN_PASSWD 'Z'

FIELD(CPRMAN, PASSWD,   24, 8)

REG32(PM_RSTC,          0x1c)
REG32(PM_RSTS,          0x20)
REG32(PM_WDOG,          0x24)

static const char *pm_name(hwaddr addr)
{
    addr &= ~3;
    switch (addr) {
    case A_PM_RSTC: return "RST_CLR";
    case A_PM_RSTS: return "RST_SET";
    case A_PM_WDOG: return "WDG";
    default:
        return "UNKN";
    }
}

static uint64_t bcm2835_cprman_pm_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    uint32_t res = 0;

    trace_bcm2835_cprman_read(size << 3, addr, "PM", pm_name(addr), "", res);

    return res;
}

static void bcm2835_cprman_pm_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    const char *name;

    if (FIELD_EX32(value, CPRMAN, PASSWD) != CPRMAN_PASSWD) {
        qemu_log_mask(LOG_GUEST_ERROR, "[CPRMAN]: password key error w%02d"
                                       " *0x%04"HWADDR_PRIx" = 0x%"PRIx64"\n",
                      size << 3, addr, value);
        return;
    }
    value &= ~R_CPRMAN_PASSWD_MASK;

    name = pm_name(addr);
    trace_bcm2835_cprman_write_pm(addr, name, value);
    if (addr == A_PM_RSTC && value & 0x20) { /* TODO remove 0x20 magic */
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static const MemoryRegionOps bcm2835_cprman_pm_ops = {
    .read = bcm2835_cprman_pm_read,
    .write = bcm2835_cprman_pm_write,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

REG32(CM_CTL,               0)
FIELD(CM_CTL, SRC,       0, 4)
FIELD(CM_CTL, ENABLE,    4, 1)
FIELD(CM_CTL, KILL,      5, 1)
FIELD(CM_CTL, GATE,      6, 1)
FIELD(CM_CTL, BUSY,      7, 1)
FIELD(CM_CTL, BUSYD,     8, 1)
FIELD(CM_CTL, FRAC,      9, 1)

REG32(CM_DIV,               4)
FIELD(CM_DIV, FRAC,      0, 12)
FIELD(CM_DIV, INTEGER,  12, 12)

REG32(CM_OSCCOUNT,      0x100)
REG32(CM_LOCK,          0x114)
REG32(CM_EVENT,         0x118)
REG32(CM_PLLB,          0x170)

/* Bits used by R_CM_CTL_SRC_MASK */
enum cprman_clock_source {
    SRC_GND = 0,
    SRC_OSC = 1,
    SRC_TEST_DBG0 = 2,
    SRC_TEST_DBG1 = 3,
    SRC_PLLA = 4,
    SRC_PLLC_CORE0 = 5,
    SRC_PLLD = 6,
    SRC_PLLH_AUX = 7,
    SRC_PLLC_CORE1 = 8,
    SRC_PLLC_CORE2 = 9
};

static const char *src_name(int src)
{
    static const char *src_names[16] = {
        [SRC_GND] = "GND",
        [SRC_OSC] = "OSC",
        [SRC_PLLA] = "PLLA",
        [SRC_PLLC_CORE0] = "PLLC_CORE0",
        [SRC_PLLD] = "PLLD",
        [SRC_PLLH_AUX] = "PLLH_AUX",
        [SRC_PLLC_CORE1] = "PLLC_CORE1",
        [SRC_PLLC_CORE2] = "PLLC_CORE2",
    };
    return src_names[src] ? src_names[src] : "UNKN";
}

static const char *ctldiv_names[0x200 / 4] = {
    [0] = "GENERIC",
    [1] = "VPU",
    [2] = "SYS",
    [3] = "PERIA",
    [4] = "PERII",
    [5] = "H264",
    [6] = "ISP",
    [7] = "V3D",
    [8] = "CAM0",
    [9] = "CAM1",
    [10] = "CCP2",
    [11] = "DSI0E",
    [12] = "DSI0P",
    [13] = "DPI",
    [14] = "GP0",
    [15] = "GP1",
    [16] = "GP2",
    [17] = "HSM",
    [18] = "OTP",
    [19] = "PCM",
    [20] = "PWM",
    [21] = "SLIM",
    [22] = "SMI",
    [24] = "TCNT",
    [25] = "TEC",
    [26] = "TD0",
    [27] = "TD1",
    [28] = "TSENS",
    [29] = "TIMER",
    [30] = "UART",
    [31] = "VEC",
    [43] = "DSI1E",
    [44] = "DSI1P",
    [45] = "DFT",
    [50] = "PULSE",
    [53] = "SDC",
    [54] = "ARM",
    [55] = "AVEO",
    [56] = "EMMC",
};

static const char *cm_name(hwaddr addr)
{
    int idx;

    addr &= ~3;
    switch (addr) {
    case A_CM_OSCCOUNT: return "OSCCOUNT";
    case 0x104 ... 0x110:
    case A_CM_PLLB: return "PLLx";
    case A_CM_LOCK: return "LOCK";
    case A_CM_EVENT: return "EVENT";
    default:
        idx = addr / 8;
        return ctldiv_names[idx] ? ctldiv_names[idx] : "UNKN";
    }
}

static uint32_t scale(uint32_t value)
{
    return (1000ull * value) >> 10;
}

/*
 * Available information suggests that CPRMAN registers have default
 * values which are not overwritten by ROMMON (u-boot). The hardware
 * default values are unknown at this time.
 * The default values selected here are necessary and sufficient
 * to boot Linux directly (on raspi2 and raspi3). The selected
 * values enable all clocks and set clock rates to match their
 * parent rates.
 */
static uint64_t bcm2835_cprman_cm_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    uint32_t res = 0;

    if (addr == A_CM_LOCK) {
        res = 0b11111 << 8; /* all locked! */
    } else {
        switch (addr & 0xf) {
        case A_CM_CTL:
            res = SRC_OSC | R_CM_CTL_ENABLE_MASK;
            break;
        case A_CM_DIV:
            res = FIELD_DP32(0, CM_DIV, INTEGER, 1);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
        }
    }
    trace_bcm2835_cprman_read(size << 3, addr, "CM", cm_name(addr), "", res);

    return res;
}

static void bcm2835_cprman_cm_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    const char *name;

    if (FIELD_EX32(value, CPRMAN, PASSWD) != CPRMAN_PASSWD) {
        qemu_log_mask(LOG_GUEST_ERROR, "[CPRMAN]: password key error w%02d"
                                       " *0x%04"HWADDR_PRIx" = 0x%"PRIx64"\n",
                      size << 3, addr, value);
        return;
    }
    value &= ~R_CPRMAN_PASSWD_MASK;

    name = cm_name(addr);
    switch (addr) {
    case A_CM_OSCCOUNT:
    case 0x104 ... 0x110:
    case A_CM_PLLB:
    case A_CM_LOCK:
    case A_CM_EVENT:
        trace_bcm2835_cprman_write_cm_generic(name, value);
        break;
    default:
        switch (addr & 0xf) {
        case A_CM_CTL:
            trace_bcm2835_cprman_write_cm_ctl(name, src_name(value & 0xf),
                                FIELD_EX32(value, CM_CTL, ENABLE));
            break;
        case A_CM_DIV:
            trace_bcm2835_cprman_write_cm_div(name,
                                FIELD_EX32(value, CM_DIV, INTEGER),
                                scale(FIELD_EX32(value, CM_DIV, FRAC)));
        }
    }
}

static const MemoryRegionOps bcm2835_cprman_cm_ops = {
    .read = bcm2835_cprman_cm_read,
    .write = bcm2835_cprman_cm_write,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

REG32(A2W_PLL_CTRL, 0x00)
FIELD(A2W_PLL_CTRL, NDIV,                   0, 12)
FIELD(A2W_PLL_CTRL, PDIV,                   12, 3)
FIELD(A2W_PLL_CTRL, POWER_DOWN,             16, 1)
FIELD(A2W_PLL_CTRL, POWER_RESET_DISABLE,    17, 1)

REG32(A2W_PLL_ANA0, 0x10)

FIELD(A2W_PLL_FRAC, DIV,                    0, 20)

FIELD(A2W_PLL_CHAN, DIV,                    0, 8)
FIELD(A2W_PLL_CHAN, DISABLE,                8, 1)

static const char *a2w_name(hwaddr addr)
{
    if (addr >= 0x300) {
        return "CHANx";
    }
    if (addr >= 0x200) {
        return "FRACx";
    }
    switch (addr & 0x1f) {
    case A_A2W_PLL_CTRL:
        return "CTRL";
    case A_A2W_PLL_ANA0:
        return "ANA0";
    default:
        return "UNKN";
    }
}

static const char *pll_name(int idx)
{
    static const char *pll_names[8] = {
        [0] = "PLLA",
        [1] = "PLLC",
        [2] = "PLLD",
        [3] = "PLLH",
        [7] = "PLLB",
    };
    return pll_names[idx] ? pll_names[idx] : "UNKN";
}

static uint64_t bcm2835_cprman_a2w_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    uint32_t res = 0;

    if (addr < 0x200) {
        /* Power */
        switch (addr & 0x1f) {
        case A_A2W_PLL_CTRL:
            res = R_A2W_PLL_CTRL_POWER_DOWN_MASK; /* On */
            break;
        case A_A2W_PLL_ANA0:
            break;
        }
    } else {
        /* addr < 0x300 is FREQ, else CHANNEL */
        qemu_log_mask(LOG_UNIMP, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }
    trace_bcm2835_cprman_read(size << 3, addr, "A2W", a2w_name(addr),
                              pll_name((addr >> 5) & 7), res);

    return res;
}

static void bcm2835_cprman_a2w_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size)
{
    if (FIELD_EX32(value, CPRMAN, PASSWD) != CPRMAN_PASSWD) {
        qemu_log_mask(LOG_GUEST_ERROR, "[CPRMAN]: password key error w%02d"
                                       " *0x%04"HWADDR_PRIx" = 0x%"PRIx64"\n",
                      size << 3, addr, value);
        return;
    }
    value &= ~R_CPRMAN_PASSWD_MASK;

    trace_bcm2835_cprman_write_a2w(addr, a2w_name(addr),
                                   pll_name((addr >> 5) & 7), value);
}

static const MemoryRegionOps bcm2835_cprman_a2w_ops = {
    .read = bcm2835_cprman_a2w_read,
    .write = bcm2835_cprman_a2w_write,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void bcm2835_cprman_init(Object *obj)
{
    BCM2835CprmanState *s = BCM2835_CPRMAN(obj);

    memory_region_init_io(&s->iomem.pm, obj, &bcm2835_cprman_pm_ops, s,
                          TYPE_BCM2835_CPRMAN "-pm", 0x1000);
    memory_region_init_io(&s->iomem.cm, obj, &bcm2835_cprman_cm_ops, s,
                          TYPE_BCM2835_CPRMAN "-cm", 0x1000);
    memory_region_init_io(&s->iomem.a2w, obj, &bcm2835_cprman_a2w_ops, s,
                          TYPE_BCM2835_CPRMAN "-a2w", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem.pm);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem.cm);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem.a2w);
}

static TypeInfo bcm2835_cprman_info = {
    .name          = TYPE_BCM2835_CPRMAN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835CprmanState),
    .instance_init = bcm2835_cprman_init,
};

static void bcm2835_cprman_register_types(void)
{
    type_register_static(&bcm2835_cprman_info);
}

type_init(bcm2835_cprman_register_types)
