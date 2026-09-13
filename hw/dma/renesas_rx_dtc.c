/*
 * Renesas RX Data Transfer Controller (DTC)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), section 18 (DTC)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *
 * Register-level model of the DTC. The DTC fetches RAM-resident transfer
 * descriptors when activated by a peripheral interrupt; that activation path is
 * not modelled, so the control/status registers behave as storage and the
 * module always reports idle. This lets FIT/RTOS drivers configure and enable
 * the DTC (and fall back to interrupt-driven transfers) without faulting.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/dma/renesas_rx_dtc.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

#define R_DTCCR     0x00    /* 8-bit  */
#define R_DTCVBR    0x04    /* 32-bit */
#define R_DTCADMOD  0x08    /* 8-bit  */
#define R_DTCST     0x0C    /* 8-bit  */
#define R_DTCSTS    0x0E    /* 16-bit */
#define R_DTCIBR    0x10    /* 32-bit */
#define R_DTCOR     0x14    /* 8-bit  */
#define R_DTCEXBR   0x18    /* 16-bit */

static uint64_t dtc_read(void *opaque, hwaddr offset, unsigned size)
{
    RenesasRxDtcState *s = opaque;

    switch (offset) {
    case R_DTCCR:
        return s->dtccr;
    case R_DTCVBR:
        return s->dtcvbr;
    case R_DTCADMOD:
        return s->dtcadmod;
    case R_DTCST:
        return s->dtcst;
    case R_DTCSTS:
        /*ACT=0: never busy */
        return s->dtcsts;
    case R_DTCIBR:
        return s->dtcibr;
    case R_DTCOR:
        return s->dtcor;
    case R_DTCEXBR:
        return s->dtcexbr;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas-rx-dtc: read unimplemented 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void dtc_write(void *opaque, hwaddr offset, uint64_t value,
                      unsigned size)
{
    RenesasRxDtcState *s = opaque;

    switch (offset) {
    case R_DTCCR:
        s->dtccr = value;
        break;
    case R_DTCVBR:
        s->dtcvbr = value;
        break;
    case R_DTCADMOD:
        s->dtcadmod = value;
        break;
    case R_DTCST:
        s->dtcst = value;
        break;
    case R_DTCIBR:
        s->dtcibr = value;
        break;
    case R_DTCOR:
        s->dtcor = value;
        break;
    case R_DTCEXBR:
        s->dtcexbr = value;
        break;
    case R_DTCSTS:
        /*status, read-only */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas-rx-dtc: write unimplemented 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps dtc_ops = {
    .read = dtc_read,
    .write = dtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 4 },
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

static void rx_dtc_reset(DeviceState *dev)
{
    RenesasRxDtcState *s = RENESAS_RX_DTC(dev);

    s->dtccr = 0x08;    /* RRS = 1 out of reset */
    s->dtcvbr = 0;
    s->dtcadmod = 0;
    s->dtcst = 0;
    s->dtcsts = 0;
    s->dtcibr = 0;
    s->dtcor = 0;
    s->dtcexbr = 0;
}

static void rx_dtc_init(Object *obj)
{
    RenesasRxDtcState *s = RENESAS_RX_DTC(obj);

    memory_region_init_io(&s->mr, obj, &dtc_ops, s,
                          "renesas-rx-dtc", RX_DTC_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
}

static const VMStateDescription vmstate_rx_dtc = {
    .name = "renesas-rx-dtc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(dtccr, RenesasRxDtcState),
        VMSTATE_UINT32(dtcvbr, RenesasRxDtcState),
        VMSTATE_UINT8(dtcadmod, RenesasRxDtcState),
        VMSTATE_UINT8(dtcst, RenesasRxDtcState),
        VMSTATE_UINT16(dtcsts, RenesasRxDtcState),
        VMSTATE_UINT32(dtcibr, RenesasRxDtcState),
        VMSTATE_UINT8(dtcor, RenesasRxDtcState),
        VMSTATE_UINT16(dtcexbr, RenesasRxDtcState),
        VMSTATE_END_OF_LIST()
    }
};

static void rx_dtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rx_dtc;
    device_class_set_legacy_reset(dc, rx_dtc_reset);
}

static const TypeInfo rx_dtc_info = {
    .name = TYPE_RENESAS_RX_DTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasRxDtcState),
    .instance_init = rx_dtc_init,
    .class_init = rx_dtc_class_init,
};

static void rx_dtc_register_types(void)
{
    type_register_static(&rx_dtc_info);
}

type_init(rx_dtc_register_types)
