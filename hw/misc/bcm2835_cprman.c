/*
 * BCM2835 Clock subsystem (poor man's version)
 * https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf
 *
 * Copyright (C) 2018 Guenter Roeck <linux@roeck-us.net>
 * Copyright (C) 2018 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/bcm2835_cprman.h"
#include "hw/register.h"
#include "trace.h"

FIELD(CM,     PASSWD,   24, 8)

FIELD(CM_CTL, SRC,       0, 4)
FIELD(CM_CTL, ENABLE,    4, 1)
FIELD(CM_CTL, KILL,      5, 1)
FIELD(CM_CTL, GATE,      6, 1)
FIELD(CM_CTL, BUSY,      7, 1)
FIELD(CM_CTL, BUSYD,     8, 1)
FIELD(CM_CTL, FRAC,      9, 1)

FIELD(CM_DIV, FRAC,      0, 12)
FIELD(CM_DIV, INTEGER,  12, 12)

enum cprman_clock_source {
    SRC_GND = 0,
    SRC_OSC = 1,
    SRC_TEST_DBG0 = 2,
    SRC_TEST_DBG1 = 3,
    SRC_PLLA_CORE = 4,
    SRC_PLLC_CORE0 = 5,
    SRC_PLLD_CORE = 6,
    SRC_PLLH_AUX = 7,
    SRC_PLLC_CORE1 = 8,
    SRC_PLLC_CORE2 = 9
};

static const char *src_name(int src)
{
    static const char *src_names[16] = {
        [SRC_GND] = "GND",
        [SRC_OSC] = "OSC",
        [SRC_TEST_DBG0] = "TEST_DBG0",
        [SRC_TEST_DBG1] = "TEST_DBG1",
        [SRC_PLLA_CORE] = "PLLA_CORE",
        [SRC_PLLC_CORE0] = "PLLC_CORE0",
        [SRC_PLLD_CORE] = "PLLD_CORE",
        [SRC_PLLH_AUX] = "PLLH_AUX",
        [SRC_PLLC_CORE1] = "PLLC_CORE1",
        [SRC_PLLC_CORE2] = "PLLC_CORE2",
    };
    return src_names[src] ? src_names[src] : "UNKN";
}

static const char *ctldiv_names[CPRMAN_NUM_REGS] = {
    [0] = "GNRIC",
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

static bool is_ctldiv(hwaddr addr)
{
    return !!ctldiv_names[addr / 8];
}

static const char *rname(hwaddr addr)
{
    addr &= ~3;
    switch (addr) {
    case 0x100: return "OSCCOUNT";
    case 0x104 ... 0x110:
    case 0x170: return "PLLx";
    case 0x114: return "LOCK";
    case 0x118: return "EVENT";
    default: {
        int idx = addr / 8;
        return ctldiv_names[idx] ? ctldiv_names[idx] : "UNKN";
        }
    }
}

static uint32_t scale(uint32_t value)
{
    return (1000ull * value) / 1024;
}

static uint64_t bcm2835_cprman_read(void *opaque, hwaddr addr, unsigned size)
{
    BCM2835CprmanState *s = (BCM2835CprmanState *)opaque;
    bool is_div = addr % 8;
    uint64_t value = s->regs[addr >> 2];

    trace_bcm2835_cprman_rd(size << 3, addr, rname(addr), value);
    if (is_ctldiv(addr)) {
        if (is_div) {
            trace_bcm2835_cprman_rd_div(rname(addr),
                                        FIELD_EX32(value, CM_DIV, INTEGER),
                                        scale(FIELD_EX32(value, CM_DIV, FRAC)));
        } else {
            trace_bcm2835_cprman_rd_ctl(rname(addr), src_name(value & 0xf),
                                        FIELD_EX32(value, CM_CTL, ENABLE));
        }
    } else {
        switch (addr & ~3) {
        case 0x100 ... 0x118:
        case 0x170:
            qemu_log_mask(LOG_UNIMP, "[CM]: unimp r%02d PLL? 0x%04"
                                     HWADDR_PRIx " = 0x%"PRIx64 "\n",
                          size << 3, addr, value);
            value = -1; /* FIXME PLL lock? */
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "[CM]: unimp r%02d ??? 0x%04"
                                     HWADDR_PRIx " = 0x%"PRIx64 "\n",
                          size << 3, addr, value);
        }
    }
    return value;
}

#define CM_PASSWD 'Z'

static void bcm2835_cprman_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    BCM2835CprmanState *s = (BCM2835CprmanState *)opaque;
    bool is_div = addr % 8;
    const char *name = rname(addr);

    if (FIELD_EX32(value, CM, PASSWD) != CM_PASSWD) {
        qemu_log_mask(LOG_GUEST_ERROR, "[CM]: password key error w%02d *0x%04"
                                       HWADDR_PRIx " = 0x%" PRIx64 " (%s)\n",
                      size << 3, addr, value, name);
        return;
    }
    value &= ~R_CM_PASSWD_MASK;
    trace_bcm2835_cprman_wr(size << 3, addr, name, value);

    s->regs[addr >> 2] = value;
    if (is_ctldiv(addr)) {
        if (is_div) {
            trace_bcm2835_cprman_wr_div(name,
                                        FIELD_EX32(value, CM_DIV, INTEGER),
                                        scale(FIELD_EX32(value, CM_DIV, FRAC)));
        } else {
            trace_bcm2835_cprman_wr_ctl(name, src_name(value & 0xf),
                                        FIELD_EX32(value, CM_CTL, ENABLE));
        }
    } else {
        switch (addr & ~3) {
        case 0x100 ... 0x118:
        case 0x170:
            qemu_log_mask(LOG_UNIMP, "[CM]: unimp w%02d PLL? *0x%04"
                                     HWADDR_PRIx " = 0x%" PRIx64 "\n",
                          size << 3, addr, value);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "[CM]: unimp w%02d ??? 0x%04"
                                     HWADDR_PRIx " = 0x%"PRIx64 "\n",
                          size << 3, addr, value);
        }
    }
}

static const MemoryRegionOps bcm2835_cprman_ops = {
    .read = bcm2835_cprman_read,
    .write = bcm2835_cprman_write,
    .impl.min_access_size = 4,
    .valid.min_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_cprman = {
    .name = TYPE_BCM2835_CPRMAN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, BCM2835CprmanState, CPRMAN_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_cprman_reset(DeviceState *dev)
{
    BCM2835CprmanState *s = BCM2835_CPRMAN(dev);
    int i;

    /*
     * Available information suggests that CPRMAN registers have default
     * values which are not overwritten by ROMMON (u-boot). The hardware
     * default values are unknown at this time.
     * The default values selected here are necessary and sufficient
     * to boot Linux directly (on raspi2 and raspi3). The selected
     * values enable all clocks and set clock rates to match their
     * parent rates.
     */
    for (i = 0; i < CPRMAN_NUM_REGS; i += 2) {
        if (!is_ctldiv(i * 4)) {
            continue;
        }
        s->regs[i] = R_CM_CTL_ENABLE_MASK | (SRC_OSC << R_CM_CTL_SRC_SHIFT);
        s->regs[i + 1] = (1 << R_CM_DIV_FRAC_SHIFT);
    }
}

static void bcm2835_cprman_init(Object *obj)
{
    BCM2835CprmanState *s = BCM2835_CPRMAN(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_cprman_ops, s,
                          TYPE_BCM2835_CPRMAN, 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void bcm2835_cprman_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = bcm2835_cprman_reset;
    dc->vmsd = &vmstate_bcm2835_cprman;
}

static TypeInfo bcm2835_cprman_info = {
    .name          = TYPE_BCM2835_CPRMAN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835CprmanState),
    .class_init    = bcm2835_cprman_class_init,
    .instance_init = bcm2835_cprman_init,
};

static void bcm2835_cprman_register_types(void)
{
    type_register_static(&bcm2835_cprman_info);
}

type_init(bcm2835_cprman_register_types)
