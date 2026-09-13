/*
 * Renesas RX65N Serial Peripheral Interface (RSPI)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), Section 30
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "hw/ssi/renesas_rspi.h"
#include "migration/vmstate.h"

/*
 * Register offsets from device base.
 * Reference: RX65N Group Hardware Manual Table 30.1
 */
REG8(SPCR,   0x00)
  FIELD(SPCR, SPMS,   0, 1)   /* SPI mode: 0=4-wire, 1=3-wire */
  FIELD(SPCR, TXONLY, 1, 1)   /* transmit-only mode */
  FIELD(SPCR, MODFEN, 2, 1)   /* mode fault detect enable */
  FIELD(SPCR, MSTR,   3, 1)   /* master/slave: 1=master */
  FIELD(SPCR, SPEIE,  4, 1)   /* error interrupt enable */
  FIELD(SPCR, SPTIE,  5, 1)   /* transmit interrupt enable */
  FIELD(SPCR, SPE,    6, 1)   /* SPI function enable */
  FIELD(SPCR, SPRIE,  7, 1)   /* receive interrupt enable */

REG8(SSLP,   0x01)   /* SSL polarity */
REG8(SPPCR,  0x02)   /* pin control */

REG8(SPSR,   0x03)
  FIELD(SPSR, OVRF,   0, 1)   /* overrun flag */
  FIELD(SPSR, IDLNF,  1, 1)   /* idle flag */
  FIELD(SPSR, MODF,   2, 1)   /* mode fault flag */
  FIELD(SPSR, PERF,   3, 1)   /* parity error flag */
  FIELD(SPSR, UDRF,   4, 1)   /* underrun flag */
  FIELD(SPSR, SPTEF,  5, 1)   /* transmit buffer empty */
  FIELD(SPSR, SPRFF,  6, 1)   /* receive FIFO full (not implemented here) */
  FIELD(SPSR, SPRF,   7, 1)   /* receive buffer full */

REG32(SPDR,  0x04)   /* data register (also 16-bit and 8-bit access) */
REG8(SPSCR,  0x08)   /* sequence control */
REG8(SPSSR,  0x09)   /* sequence status */
REG8(SPBR,   0x0A)   /* bit rate */
REG8(SPDCR,  0x0B)
  FIELD(SPDCR, SPFC,  0, 2)   /* frames per access */
  FIELD(SPDCR, SPRDTD, 4, 1)   /* receive/transmit data select */
  FIELD(SPDCR, SPLW,  5, 1)   /* access width (1=long word) */
  FIELD(SPDCR, SPBYT, 6, 1)   /* byte access */

REG8(SPCKD,  0x0C)   /* clock delay */
REG8(SSLND,  0x0D)   /* SSL negate delay */
REG8(SPND,   0x0E)   /* next-access delay */
REG8(SPCR2,  0x0F)   /* control 2 */
/* SPCMD0–SPCMD7 at offsets 0x10–0x1E (16-bit each) */
#define A_SPCMD_BASE 0x10
#define A_SPCMD_END  0x1E
REG8(SPDCR2, 0x20)   /* data control 2 */

static void rspi_do_transfer(RX65NRSPIState *s)
{
    uint32_t rx;

    /* Only transfer in master mode with SPI enabled */
    if (!FIELD_EX8(s->spcr, SPCR, MSTR) ||
        !FIELD_EX8(s->spcr, SPCR, SPE)) {
        return;
    }

    rx = ssi_transfer(s->ssi, s->spdr);
    s->spdr = rx;

    /* Mark receive buffer full, transmit buffer empty */
    s->spsr = FIELD_DP8(s->spsr, SPSR, SPRF,  1);
    s->spsr = FIELD_DP8(s->spsr, SPSR, SPTEF, 1);

    /* Fire receive interrupt */
    if (FIELD_EX8(s->spcr, SPCR, SPRIE)) {
        qemu_irq_pulse(s->irq[RSPI_IRQ_SPRI]);
    }
}

static uint64_t rspi_read(void *opaque, hwaddr addr, unsigned size)
{
    RX65NRSPIState *s = opaque;
    uint64_t ret;

    if (addr >= A_SPCMD_BASE && addr <= A_SPCMD_END) {
        int idx = (addr - A_SPCMD_BASE) / 2;
        return s->spcmd[idx];
    }

    switch (addr) {
    case A_SPCR:
        return s->spcr;
    case A_SSLP:
        return s->sslp;
    case A_SPPCR:
        return s->sppcr;
    case A_SPSR:
        ret = s->spsr;
        /* Reading SPSR clears SPRF and OVRF */
        s->spsr = FIELD_DP8(s->spsr, SPSR, SPRF, 0);
        s->spsr = FIELD_DP8(s->spsr, SPSR, OVRF, 0);
        return ret;
    case A_SPDR:
        s->spsr = FIELD_DP8(s->spsr, SPSR, SPRF, 0);
        /* Raise transmit interrupt now that the buffer is free again */
        if (FIELD_EX8(s->spcr, SPCR, SPTIE)) {
            qemu_irq_pulse(s->irq[RSPI_IRQ_SPTI]);
        }
        return s->spdr;
    case A_SPSCR:
        return s->spscr;
    case A_SPSSR:
        return s->spssr;
    case A_SPBR:
        return s->spbr;
    case A_SPDCR:
        return s->spdcr;
    case A_SPCKD:
        return s->spckd;
    case A_SSLND:
        return s->sslnd;
    case A_SPND:
        return s->spnd;
    case A_SPCR2:
        return s->spcr2;
    case A_SPDCR2:
        return s->spdcr2;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "renesas_rspi: read 0x%" HWADDR_PRIX " not implemented\n",
                      addr);
        return UINT64_MAX;
    }
}

static void rspi_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RX65NRSPIState *s = opaque;

    if (addr >= A_SPCMD_BASE && addr <= A_SPCMD_END) {
        int idx = (addr - A_SPCMD_BASE) / 2;
        s->spcmd[idx] = val;
        return;
    }

    switch (addr) {
    case A_SPCR:
        s->spcr = val;
        /* Raise SPTI when SPE+SPTIE are both set (buffer initially empty) */
        if (FIELD_EX8(s->spcr, SPCR, SPE) &&
            FIELD_EX8(s->spcr, SPCR, SPTIE) &&
            FIELD_EX8(s->spsr, SPSR, SPTEF)) {
            qemu_irq_pulse(s->irq[RSPI_IRQ_SPTI]);
        }
        break;
    case A_SSLP:
        s->sslp   = val;
        break;
    case A_SPPCR:
        s->sppcr  = val;
        break;
    case A_SPSR:
        /* W0C for error/status bits */
        s->spsr &= val | 0xe0;  /* keep SPTEF, SPRFF, SPRF read-only */
        break;
    case A_SPDR:
        s->spdr = val;
        s->spsr = FIELD_DP8(s->spsr, SPSR, SPTEF, 0);
        rspi_do_transfer(s);
        break;
    case A_SPSCR:
        s->spscr  = val;
        break;
    case A_SPBR:
        s->spbr   = val;
        break;
    case A_SPDCR:
        s->spdcr  = val;
        break;
    case A_SPCKD:
        s->spckd  = val;
        break;
    case A_SSLND:
        s->sslnd  = val;
        break;
    case A_SPND:
        s->spnd   = val;
        break;
    case A_SPCR2:
        s->spcr2  = val;
        break;
    case A_SPDCR2:
        s->spdcr2 = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_rspi: write 0x%" HWADDR_PRIX
                      " not implemented\n",
                      addr);
        break;
    }
}

static const MemoryRegionOps rspi_ops = {
    .read  = rspi_read,
    .write = rspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void rspi_reset(DeviceState *dev)
{
    RX65NRSPIState *s = RENESAS_RSPI(dev);
    int i;

    s->spcr   = 0x00;
    s->sslp   = 0x00;
    s->sppcr  = 0x00;
    s->spsr   = 0x20;  /* SPTEF=1: transmit buffer initially empty */
    s->spdr   = 0x00000000;
    s->spscr  = 0x00;
    s->spssr  = 0x00;
    s->spbr   = 0xff;
    s->spdcr  = 0x20;  /* SPLW=1: long-word access default */
    s->spckd  = 0x00;
    s->sslnd  = 0x00;
    s->spnd   = 0x00;
    s->spcr2  = 0x00;
    s->spdcr2 = 0x00;
    for (i = 0; i < 8; i++) {
        s->spcmd[i] = 0x0700; /* default: 8-bit, CPOL=0, CPHA=0 */
    }
}

static void rspi_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RX65NRSPIState *s = RENESAS_RSPI(obj);
    int i;

    /* 0x24 bytes covers SPCR through SPDCR2 */
    memory_region_init_io(&s->memory, obj, &rspi_ops, s,
                          "renesas-rspi", 0x24);
    sysbus_init_mmio(d, &s->memory);

    for (i = 0; i < RSPI_NR_IRQ; i++) {
        sysbus_init_irq(d, &s->irq[i]);
    }

    s->ssi = ssi_create_bus(DEVICE(obj), "ssi");
}

static const VMStateDescription vmstate_rspi = {
    .name = "renesas-rspi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(spcr,   RX65NRSPIState),
        VMSTATE_UINT8(sslp,   RX65NRSPIState),
        VMSTATE_UINT8(sppcr,  RX65NRSPIState),
        VMSTATE_UINT8(spsr,   RX65NRSPIState),
        VMSTATE_UINT32(spdr,  RX65NRSPIState),
        VMSTATE_UINT8(spscr,  RX65NRSPIState),
        VMSTATE_UINT8(spssr,  RX65NRSPIState),
        VMSTATE_UINT8(spbr,   RX65NRSPIState),
        VMSTATE_UINT8(spdcr,  RX65NRSPIState),
        VMSTATE_UINT8(spckd,  RX65NRSPIState),
        VMSTATE_UINT8(sslnd,  RX65NRSPIState),
        VMSTATE_UINT8(spnd,   RX65NRSPIState),
        VMSTATE_UINT8(spcr2,  RX65NRSPIState),
        VMSTATE_UINT16_ARRAY(spcmd, RX65NRSPIState, 8),
        VMSTATE_UINT8(spdcr2, RX65NRSPIState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property rspi_properties[] = {
    DEFINE_PROP_UINT64("input-freq", RX65NRSPIState, input_freq, 0),
};

static void rspi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rspi;
    device_class_set_legacy_reset(dc, rspi_reset);
    device_class_set_props(dc, rspi_properties);
}

static const TypeInfo rspi_info = {
    .name          = TYPE_RENESAS_RSPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RX65NRSPIState),
    .instance_init = rspi_init,
    .class_init    = rspi_class_init,
};

static void rspi_register_types(void)
{
    type_register_static(&rspi_info);
}

type_init(rspi_register_types)
