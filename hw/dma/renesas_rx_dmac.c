/*
 * Renesas RX DMA Controller (DMAC)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), section 17 (DMAC)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *
 * Models the eight DMAC channels. Hardware (peripheral-triggered) activation is
 * not modelled; software-requested transfers (DMREQ.SWREQ) are performed
 * synchronously, which covers memory-to-memory use and driver self-tests. The
 * source/destination address increment modes and 8/16/32-bit transfer unit are
 * honoured.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/dma/renesas_rx_dmac.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"

/* Per-channel register offsets. */
#define R_DMSAR     0x00    /* 32-bit */
#define R_DMDAR     0x04    /* 32-bit */
#define R_DMCRA     0x08    /* 32-bit */
#define R_DMCRB     0x0C    /* 16-bit */
#define R_DMTMD     0x10    /* 16-bit */
#define R_DMINT     0x13    /* 8-bit  */
#define R_DMAMD     0x14    /* 16-bit */
#define R_DMOFR     0x18    /* 32-bit */
#define R_DMCNT     0x1C    /* 8-bit  */
#define R_DMREQ     0x1D    /* 8-bit  */
#define R_DMSTS     0x1E    /* 8-bit  */

/* Common registers. */
#define R_DMAST     0x200   /* 8-bit  */

/* DMTMD: SZ field (bits 9:8) = transfer unit. */
#define DMTMD_SZ(v)     (((v) >> 8) & 3)
/* DMAMD: SM (bits 9:8) source mode, DM (bits 13:12) destination mode. */
#define DMAMD_SM(v)     (((v) >> 8) & 3)
#define DMAMD_DM(v)     (((v) >> 12) & 3)
#define ADDR_MODE_FIXED 0
#define ADDR_MODE_OFF   1   /* offset addition (not modelled, treated fixed) */
#define ADDR_MODE_INC   2
#define ADDR_MODE_DEC   3

#define DMCNT_DTE       (1u << 0)   /* transfer enable */
#define DMREQ_SWREQ     (1u << 0)   /* software request */
#define DMREQ_CLRS      (1u << 4)
#define DMSTS_ACT       (1u << 7)
#define DMSTS_DTIF      (1u << 1)   /* transfer-end interrupt flag */

static unsigned dmac_unit_bytes(uint16_t dmtmd)
{
    switch (DMTMD_SZ(dmtmd)) {
    case 0:
        return 1;
    case 1:
        return 2;
    default: return 4;
    }
}

static void dmac_step_addr(uint32_t *addr, unsigned mode, unsigned bytes)
{
    if (mode == ADDR_MODE_INC) {
        *addr += bytes;
    } else if (mode == ADDR_MODE_DEC) {
        *addr -= bytes;
    }
}

/* Perform a software-requested transfer on a channel. */
static void dmac_do_transfer(RenesasRxDmacState *s, int n)
{
    RxDmacChannel *c = &s->ch[n];
    unsigned bytes = dmac_unit_bytes(c->dmtmd);
    unsigned smode = DMAMD_SM(c->dmamd);
    unsigned dmode = DMAMD_DM(c->dmamd);
    uint16_t count = c->dmcra & 0xffff;
    uint8_t buf[4];

    if (!(s->dmast & 1) || !(c->dmcnt & DMCNT_DTE)) {
        return;
    }

    /* A zero count means 65536 transfers on hardware; clamp it for QEMU. */
    if (count == 0) {
        count = 1;
    }

    while (count--) {
        address_space_read(s->as, c->dmsar, MEMTXATTRS_UNSPECIFIED, buf, bytes);
        address_space_write(s->as, c->dmdar, MEMTXATTRS_UNSPECIFIED,
                            buf, bytes);
        dmac_step_addr(&c->dmsar, smode, bytes);
        dmac_step_addr(&c->dmdar, dmode, bytes);
    }

    /* Transfer complete: clear enable, flag status, raise interrupt. */
    c->dmcra &= 0xffff0000;
    c->dmcnt &= ~DMCNT_DTE;
    c->dmsts = (c->dmsts & ~DMSTS_ACT) | DMSTS_DTIF;
    if (c->dmint & 1) {
        qemu_irq_pulse(s->irq[n]);
    }
}

static uint64_t dmac_read(void *opaque, hwaddr offset, unsigned size)
{
    RenesasRxDmacState *s = opaque;
    RxDmacChannel *c;
    unsigned reg;

    if (offset == R_DMAST) {
        return s->dmast;
    }
    if (offset >= RX_DMAC_NR_CH * RX_DMAC_CH_SIZE) {
        return 0;
    }

    c = &s->ch[offset / RX_DMAC_CH_SIZE];
    reg = offset % RX_DMAC_CH_SIZE;
    switch (reg) {
    case R_DMSAR:
        return c->dmsar;
    case R_DMDAR:
        return c->dmdar;
    case R_DMCRA:
        return c->dmcra;
    case R_DMCRB:
        return c->dmcrb;
    case R_DMTMD:
        return c->dmtmd;
    case R_DMINT:
        return c->dmint;
    case R_DMAMD:
        return c->dmamd;
    case R_DMOFR:
        return c->dmofr;
    case R_DMCNT:
        return c->dmcnt;
    case R_DMREQ:
        return c->dmreq;
    case R_DMSTS:
        return c->dmsts;
    default:      return 0;
    }
}

static void dmac_write(void *opaque, hwaddr offset, uint64_t value,
                       unsigned size)
{
    RenesasRxDmacState *s = opaque;
    RxDmacChannel *c;
    unsigned reg, n;

    if (offset == R_DMAST) {
        s->dmast = value;
        return;
    }
    if (offset >= RX_DMAC_NR_CH * RX_DMAC_CH_SIZE) {
        qemu_log_mask(LOG_UNIMP, "renesas-rx-dmac: write unimplemented 0x%"
                      HWADDR_PRIx "\n", offset);
        return;
    }

    n = offset / RX_DMAC_CH_SIZE;
    c = &s->ch[n];
    reg = offset % RX_DMAC_CH_SIZE;
    switch (reg) {
    case R_DMSAR:
        c->dmsar = value;
        break;
    case R_DMDAR:
        c->dmdar = value;
        break;
    case R_DMCRA:
        c->dmcra = value;
        break;
    case R_DMCRB:
        c->dmcrb = value;
        break;
    case R_DMTMD:
        c->dmtmd = value;
        break;
    case R_DMINT:
        c->dmint = value;
        break;
    case R_DMAMD:
        c->dmamd = value;
        break;
    case R_DMOFR:
        c->dmofr = value;
        break;
    case R_DMCNT:
        c->dmcnt = value;
        break;
    case R_DMREQ:
        c->dmreq = value;
        if (value & DMREQ_SWREQ) {
            dmac_do_transfer(s, n);
            c->dmreq &= ~DMREQ_SWREQ;
        }
        break;
    case R_DMSTS:
        /* DTIF/ACT are status flags cleared by writing the register. */
        c->dmsts = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas-rx-dmac: write unimplemented 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps dmac_ops = {
    .read = dmac_read,
    .write = dmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 4 },
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

static void rx_dmac_reset(DeviceState *dev)
{
    RenesasRxDmacState *s = RENESAS_RX_DMAC(dev);

    memset(s->ch, 0, sizeof(s->ch));
    s->dmast = 0;
}

static void rx_dmac_realize(DeviceState *dev, Error **errp)
{
    RenesasRxDmacState *s = RENESAS_RX_DMAC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (s->dma_mr == NULL) {
        s->dma_mr = get_system_memory();
    }
    s->as = g_new0(AddressSpace, 1);
    address_space_init(s->as, s->dma_mr, "renesas-rx-dmac");

    memory_region_init_io(&s->mr, OBJECT(s), &dmac_ops, s,
                          "renesas-rx-dmac", RX_DMAC_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->mr);
    for (int i = 0; i < RX_DMAC_NR_CH; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
}

static const Property rx_dmac_properties[] = {
    DEFINE_PROP_LINK("dma-memory", RenesasRxDmacState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static const VMStateDescription vmstate_rx_dmac_ch = {
    .name = "renesas-rx-dmac-channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(dmsar, RxDmacChannel),
        VMSTATE_UINT32(dmdar, RxDmacChannel),
        VMSTATE_UINT32(dmcra, RxDmacChannel),
        VMSTATE_UINT16(dmcrb, RxDmacChannel),
        VMSTATE_UINT16(dmtmd, RxDmacChannel),
        VMSTATE_UINT8(dmint, RxDmacChannel),
        VMSTATE_UINT16(dmamd, RxDmacChannel),
        VMSTATE_UINT32(dmofr, RxDmacChannel),
        VMSTATE_UINT8(dmcnt, RxDmacChannel),
        VMSTATE_UINT8(dmreq, RxDmacChannel),
        VMSTATE_UINT8(dmsts, RxDmacChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_rx_dmac = {
    .name = "renesas-rx-dmac",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(ch, RenesasRxDmacState, RX_DMAC_NR_CH, 1,
                             vmstate_rx_dmac_ch, RxDmacChannel),
        VMSTATE_UINT8(dmast, RenesasRxDmacState),
        VMSTATE_END_OF_LIST()
    }
};

static void rx_dmac_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rx_dmac_realize;
    dc->vmsd = &vmstate_rx_dmac;
    device_class_set_props(dc, rx_dmac_properties);
    device_class_set_legacy_reset(dc, rx_dmac_reset);
}

static const TypeInfo rx_dmac_info = {
    .name = TYPE_RENESAS_RX_DMAC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasRxDmacState),
    .class_init = rx_dmac_class_init,
};

static void rx_dmac_register_types(void)
{
    type_register_static(&rx_dmac_info);
}

type_init(rx_dmac_register_types)
