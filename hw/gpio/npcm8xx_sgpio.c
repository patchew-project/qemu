/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Nuvoton Serial I/O EXPANSION INTERFACE (SOIX)
 *
 * Copyright 2025 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"

#include "hw/gpio/npcm8xx_sgpio.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "trace.h"

#define NPCM8XX_SGPIO_RD_MODE_MASK      0x6
#define NPCM8XX_SGPIO_RD_MODE_PERIODIC  0x4
#define NPCM8XX_SGPIO_RD_MODE_ON_DEMAND 0x0
#define NPCM8XX_SGPIO_IOXCTS_IOXIF_EN   BIT(7)
#define NPCM8XX_SGPIO_IOXCTS_WR_PEND    BIT(6)
#define NPCM8XX_SGPIO_IOXCTS_DATA16W    BIT(3)
#define NPCM8XX_SGPIO_REGS_SIZE         (4 * KiB)

#define  NPCM8XX_SGPIO_IXOEVCFG_FALLING BIT(1)
#define  NPCM8XX_SGPIO_IXOEVCFG_RISING  BIT(0)
#define  NPCM8XX_SGPIO_IXOEVCFG_BOTH    (NPCM8XX_SGPIO_IXOEVCFG_FALLING | \
                                         NPCM8XX_SGPIO_IXOEVCFG_RISING)

#define  IXOEVCFG_MASK 0x3

/* 8-bit register indices, with the event config registers being 16-bit */
enum NPCM8xxSGPIORegister {
    NPCM8XX_SGPIO_XDOUT0,
    NPCM8XX_SGPIO_XDOUT1,
    NPCM8XX_SGPIO_XDOUT2,
    NPCM8XX_SGPIO_XDOUT3,
    NPCM8XX_SGPIO_XDOUT4,
    NPCM8XX_SGPIO_XDOUT5,
    NPCM8XX_SGPIO_XDOUT6,
    NPCM8XX_SGPIO_XDOUT7,
    NPCM8XX_SGPIO_XDIN0,
    NPCM8XX_SGPIO_XDIN1,
    NPCM8XX_SGPIO_XDIN2,
    NPCM8XX_SGPIO_XDIN3,
    NPCM8XX_SGPIO_XDIN4,
    NPCM8XX_SGPIO_XDIN5,
    NPCM8XX_SGPIO_XDIN6,
    NPCM8XX_SGPIO_XDIN7,
    NPCM8XX_SGPIO_XEVCFG0 = 0x10,
    NPCM8XX_SGPIO_XEVCFG1 = 0x12,
    NPCM8XX_SGPIO_XEVCFG2 = 0x14,
    NPCM8XX_SGPIO_XEVCFG3 = 0x16,
    NPCM8XX_SGPIO_XEVCFG4 = 0x18,
    NPCM8XX_SGPIO_XEVCFG5 = 0x1a,
    NPCM8XX_SGPIO_XEVCFG6 = 0x1c,
    NPCM8XX_SGPIO_XEVCFG7 = 0x1e,
    NPCM8XX_SGPIO_XEVSTS0 = 0x20,
    NPCM8XX_SGPIO_XEVSTS1,
    NPCM8XX_SGPIO_XEVSTS2,
    NPCM8XX_SGPIO_XEVSTS3,
    NPCM8XX_SGPIO_XEVSTS4,
    NPCM8XX_SGPIO_XEVSTS5,
    NPCM8XX_SGPIO_XEVSTS6,
    NPCM8XX_SGPIO_XEVSTS7,
    NPCM8XX_SGPIO_IOXCTS,
    NPCM8XX_SGPIO_IOXINDR,
    NPCM8XX_SGPIO_IOXCFG1,
    NPCM8XX_SGPIO_IOXCFG2,
    NPCM8XX_SGPIO_IOXDATR = 0x2d,
    NPCM8XX_SGPIO_REGS_END,
};

static uint8_t npcm8xx_sgpio_get_in_port(NPCM8xxSGPIOState *s)
{
    uint8_t p;

    p = s->regs[NPCM8XX_SGPIO_IOXCFG2] & 0xf;
    if (p > 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                     "%s: Trying to set more the allowed input ports %d\n",
                     DEVICE(s)->canonical_path, p);
    }

    return p;
}

static uint8_t npcm8xx_sgpio_get_out_port(NPCM8xxSGPIOState *s)
{
    uint8_t p;

    p = (s->regs[NPCM8XX_SGPIO_IOXCFG2] >> 4) & 0xf;
    if (p > 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                     "%s: Trying to set more the allowed output ports %d\n",
                     DEVICE(s)->canonical_path, p);
    }

    return p;
}

static bool npcm8xx_sgpio_is_16bit(NPCM8xxSGPIOState *s)
{
    return s->regs[NPCM8XX_SGPIO_IOXCTS] & NPCM8XX_SGPIO_IOXCTS_DATA16W;
}

static uint64_t npcm8xx_sgpio_regs_read_with_cfg(NPCM8xxSGPIOState *s,
                                                 hwaddr reg)
{
    bool rd_word = npcm8xx_sgpio_is_16bit(s);
    uint64_t value;

    if (rd_word) {
        value = ((uint16_t)s->regs[reg] << 8) | s->regs[reg + 1];
    } else {
        value = s->regs[reg];
    }

    return value;
}

static void npcm8xx_sgpio_update_event(NPCM8xxSGPIOState *s, uint64_t diff)
{
    /* TODO in upcoming patch */
}

static void npcm8xx_sgpio_update_pins_in(NPCM8xxSGPIOState *s, uint64_t diff)
{
    /* TODO in upcoming patch */
}

static void npcm8xx_sgpio_update_pins_out(NPCM8xxSGPIOState *s, hwaddr reg)
{
    uint8_t nout, dout;

    if (~(s->regs[NPCM8XX_SGPIO_IOXCTS] & NPCM8XX_SGPIO_IOXCTS_IOXIF_EN)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Device disabled, transaction out aborted\n",
                      DEVICE(s)->canonical_path);
    }

    nout = npcm8xx_sgpio_get_out_port(s);
    dout = reg - NPCM8XX_SGPIO_XDOUT0;
    if (dout >= nout) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Accessing XDOUT%d when NOUT is %d\n",
                      DEVICE(s)->canonical_path, dout, nout);
    }
    s->pin_out_level[dout] = s->regs[NPCM8XX_SGPIO_XDOUT0 + dout];
    /* unset WR_PEND */
    s->regs[NPCM8XX_SGPIO_IOXCTS] &= ~0x40;
}

static uint64_t npcm8xx_sgpio_regs_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    NPCM8xxSGPIOState *s = opaque;
    uint8_t rd_mode = s->regs[NPCM8XX_SGPIO_IOXCTS] &
                      NPCM8XX_SGPIO_RD_MODE_MASK;
    hwaddr reg = addr / sizeof(uint8_t);
    uint8_t nout, nin, din, dout;
    uint64_t value = 0;

    switch (reg) {
    case NPCM8XX_SGPIO_XDOUT0 ... NPCM8XX_SGPIO_XDOUT7:
        nout = npcm8xx_sgpio_get_out_port(s);
        dout = reg - NPCM8XX_SGPIO_XDOUT0;

        if (dout >= nout) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Accessing XDOUT%d when NOUT is %d\n",
                          DEVICE(s)->canonical_path, dout, nout);
            break;
        }

        value = npcm8xx_sgpio_regs_read_with_cfg(s, reg);
        break;

    case NPCM8XX_SGPIO_XDIN0 ... NPCM8XX_SGPIO_XDIN7:
        nin = npcm8xx_sgpio_get_in_port(s);
        din = reg - NPCM8XX_SGPIO_XDIN0;

        if (din >= nin) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Accessing XDIN%d when NIN is %d\n",
                          DEVICE(s)->canonical_path, din, nin);
            break;
        }

        switch (rd_mode) {
        case NPCM8XX_SGPIO_RD_MODE_PERIODIC:
            /* XDIN are up-to-date from scanning, return directly. */
            value = npcm8xx_sgpio_regs_read_with_cfg(s, reg);
            break;
        case NPCM8XX_SGPIO_RD_MODE_ON_DEMAND:
            /*
             * IOX_SCAN write behavior is unimplemented.
             * Event generation is also umimplemented.
             */
            qemu_log_mask(LOG_UNIMP,
                        "%s: On Demand with Polling reading mode is not implemented.\n",
                        __func__);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown read mode\n", __func__);
        }
        break;

    case NPCM8XX_SGPIO_XEVCFG0 ... NPCM8XX_SGPIO_XEVCFG7:
        value = ((uint64_t)s->regs[reg] << 8) | s->regs[reg + 1];
        break;

    case NPCM8XX_SGPIO_XEVSTS0 ... NPCM8XX_SGPIO_XEVSTS7:
        value = npcm8xx_sgpio_regs_read_with_cfg(s, reg);
        break;

    case NPCM8XX_SGPIO_IOXCTS ... NPCM8XX_SGPIO_IOXDATR:
        value = s->regs[reg];
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, addr);
        break;
    }

    trace_npcm8xx_sgpio_read(DEVICE(s)->canonical_path, addr, value);

    return value;
}

static void npcm8xx_sgpio_regs_write(void *opaque, hwaddr addr, uint64_t v,
                                    unsigned int size)
{
    hwaddr reg = addr / sizeof(uint8_t);
    uint8_t hi_val = (uint8_t)(v >> 8);
    NPCM8xxSGPIOState *s = opaque;
    uint8_t value = (uint8_t) v;
    uint8_t diff;

    trace_npcm8xx_sgpio_write(DEVICE(s)->canonical_path, addr, v);

    switch (reg) {
    case NPCM8XX_SGPIO_XDOUT0 ... NPCM8XX_SGPIO_XDOUT7:
        /* Set WR_PEND bit */
        s->regs[NPCM8XX_SGPIO_IOXCTS] |= 0x40;
        if (npcm8xx_sgpio_is_16bit(s)) {
            if ((reg - NPCM8XX_SGPIO_XDOUT0) % 2) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: write unaligned 16 bit register @ 0x%"
                              HWADDR_PRIx "\n",
                              DEVICE(s)->canonical_path, addr);
                break;
            }
            s->regs[reg] = hi_val;
            s->regs[reg + 1] = value;
            npcm8xx_sgpio_update_pins_out(s, reg + 1);
        } else {
            s->regs[reg] = value;
        }
        npcm8xx_sgpio_update_pins_out(s, reg);
        break;

    /*  2 byte long regs */
    case NPCM8XX_SGPIO_XEVCFG0 ... NPCM8XX_SGPIO_XEVCFG7:
        if (~(s->regs[NPCM8XX_SGPIO_IOXCTS] & NPCM8XX_SGPIO_IOXCTS_IOXIF_EN)) {
            s->regs[reg] = hi_val;
            s->regs[reg + 1] = value;
        }
        break;

    case NPCM8XX_SGPIO_XEVSTS0 ... NPCM8XX_SGPIO_XEVSTS7:
        if (npcm8xx_sgpio_is_16bit(s)) {
            if ((reg - NPCM8XX_SGPIO_XEVSTS0) % 2) {
                qemu_log_mask(LOG_GUEST_ERROR,
                            "%s: write unaligned 16 bit register @ 0x%"
                            HWADDR_PRIx "\n",
                            DEVICE(s)->canonical_path, addr);
                break;
            }
            s->regs[reg] ^= hi_val;
            s->regs[reg + 1] ^= value;
            npcm8xx_sgpio_update_event(s, 0);
        } else {
            s->regs[reg] ^= value;
            npcm8xx_sgpio_update_event(s, 0);
        }
        break;

    case NPCM8XX_SGPIO_IOXCTS:
        /* Make sure RO bit WR_PEND is not written to */
        value &= ~NPCM8XX_SGPIO_IOXCTS_WR_PEND;
        diff = s->regs[reg] ^ value;
        s->regs[reg] = value;
        if ((s->regs[NPCM8XX_SGPIO_IOXCTS] & NPCM8XX_SGPIO_IOXCTS_IOXIF_EN) &&
            (diff & NPCM8XX_SGPIO_RD_MODE_MASK)) {
            /* reset RD_MODE if attempting to write with IOXIF_EN enabled */
            s->regs[reg] ^= (diff & NPCM8XX_SGPIO_RD_MODE_MASK);
        }
        break;

    case NPCM8XX_SGPIO_IOXINDR:
        /*
         * Only relevant to SIOX1.
         * HSIOX unimplemented for both, set value and do nothing.
         */
        s->regs[reg] = value;
        break;

    case NPCM8XX_SGPIO_IOXCFG1:
    case NPCM8XX_SGPIO_IOXCFG2:
        if (~(s->regs[NPCM8XX_SGPIO_IOXCTS] & NPCM8XX_SGPIO_IOXCTS_IOXIF_EN)) {
            s->regs[reg] = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: trying to write to register @ 0x%"
                    HWADDR_PRIx "while IOXIF_EN is enabled\n",
                    DEVICE(s)->canonical_path, addr);
        }
        break;

    case NPCM8XX_SGPIO_XDIN0 ... NPCM8XX_SGPIO_XDIN7:
    case NPCM8XX_SGPIO_IOXDATR:
        /* IOX_SCAN is unimplemented given no on-demand mode */
        qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: write to read-only register @ 0x%" HWADDR_PRIx "\n",
                    DEVICE(s)->canonical_path, addr);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, addr);
        break;
    }
}

static const MemoryRegionOps npcm8xx_sgpio_regs_ops = {
    .read = npcm8xx_sgpio_regs_read,
    .write = npcm8xx_sgpio_regs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
        .unaligned = false,
    },
};

static void npcm8xx_sgpio_enter_reset(Object *obj, ResetType type)
{
    NPCM8xxSGPIOState *s = NPCM8XX_SGPIO(obj);

    memset(s->regs, 0, sizeof(s->regs));
}

static void npcm8xx_sgpio_hold_reset(Object *obj, ResetType type)
{
    NPCM8xxSGPIOState *s = NPCM8XX_SGPIO(obj);

    npcm8xx_sgpio_update_pins_in(s, -1);
}

static void npcm8xx_sgpio_init(Object *obj)
{
    NPCM8xxSGPIOState *s = NPCM8XX_SGPIO(obj);

    memory_region_init_io(&s->mmio, obj, &npcm8xx_sgpio_regs_ops, s,
                          "regs", NPCM8XX_SGPIO_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    /* TODO: Add input GPIO pins */
}

static const VMStateDescription vmstate_npcm8xx_sgpio = {
    .name = "npcm8xx-sgpio",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(pin_in_level, NPCM8xxSGPIOState,
                            NPCM8XX_SGPIO_NR_PINS / NPCM8XX_SGPIO_MAX_PORTS),
        VMSTATE_UINT8_ARRAY(pin_out_level, NPCM8xxSGPIOState,
                            NPCM8XX_SGPIO_NR_PINS / NPCM8XX_SGPIO_MAX_PORTS),
        VMSTATE_UINT8_ARRAY(regs, NPCM8xxSGPIOState, NPCM8XX_SGPIO_NR_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void npcm8xx_sgpio_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *reset = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    QEMU_BUILD_BUG_ON(NPCM8XX_SGPIO_REGS_END > NPCM8XX_SGPIO_NR_REGS);

    dc->desc = "NPCM8xx SIOX Controller";
    dc->vmsd = &vmstate_npcm8xx_sgpio;
    reset->phases.enter = npcm8xx_sgpio_enter_reset;
    reset->phases.hold = npcm8xx_sgpio_hold_reset;
}

static const TypeInfo npcm8xx_sgpio_types[] = {
    {
        .name = TYPE_NPCM8XX_SGPIO,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NPCM8xxSGPIOState),
        .class_init = npcm8xx_sgpio_class_init,
        .instance_init = npcm8xx_sgpio_init,
    },
};
DEFINE_TYPES(npcm8xx_sgpio_types);
