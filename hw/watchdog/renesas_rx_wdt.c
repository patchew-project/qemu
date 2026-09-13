/*
 * Renesas RX Watchdog Timers (WDT and IWDT)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), sections 28 (WDT) and 29 (IWDT)
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *
 * The watchdogs accept their refresh sequences (write 0x00 then 0xFF to the
 * refresh register) and present a ready, non-underflowed status. QEMU models no
 * real timeout, so a missed refresh never resets the guest; this keeps firmware
 * that arms the watchdog during init running normally.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/watchdog/renesas_rx_wdt.h"
#include "migration/vmstate.h"

/* WDT register offsets from 0x00088020. */
#define R_WDTRR     0x00    /* 8-bit  refresh        */
#define R_WDTCR     0x02    /* 16-bit control        */
#define R_WDTSR     0x04    /* 16-bit status         */
#define R_WDTRCR    0x06    /* 8-bit  reset control  */

/* IWDT register offsets (block at 0x00088030, i.e. +0x10). */
#define R_IWDTRR    0x10    /* 8-bit  refresh        */
#define R_IWDTCR    0x12    /* 16-bit control        */
#define R_IWDTSR    0x14    /* 16-bit status         */
#define R_IWDTRCR   0x16    /* 8-bit  reset control  */
#define R_IWDTCSTPR 0x18    /* 8-bit  count-stop     */

/* The counter value lives in the status register's low 14 bits. */
#define WDT_COUNT_RELOAD    0x3fff

static void wdt_refresh(uint8_t *phase, uint16_t *sr, uint64_t value)
{
    /* A 0x00 followed by 0xFF reloads the down-counter to its top value. */
    if (*phase == 0 && value == 0x00) {
        *phase = 1;
    } else if (*phase == 1 && value == 0xff) {
        *phase = 0;
        *sr = (*sr & ~WDT_COUNT_RELOAD) | WDT_COUNT_RELOAD;
    } else {
        *phase = 0;
    }
}

static uint64_t wdt_read(void *opaque, hwaddr offset, unsigned size)
{
    RenesasRxWdtState *s = opaque;

    switch (offset) {
    case R_WDTRR:
        return 0xff;
    case R_WDTCR:
        return s->wdtcr;
    case R_WDTSR:
        return s->wdtsr;
    case R_WDTRCR:
        return s->wdtrcr;
    case R_IWDTRR:
        return 0xff;
    case R_IWDTCR:
        return s->iwdtcr;
    case R_IWDTSR:
        return s->iwdtsr;
    case R_IWDTRCR:
        return s->iwdtrcr;
    case R_IWDTCSTPR:
        return s->iwdtcstpr;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas-rx-wdt: read unimplemented 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void wdt_write(void *opaque, hwaddr offset, uint64_t value,
                      unsigned size)
{
    RenesasRxWdtState *s = opaque;

    switch (offset) {
    case R_WDTRR:
        wdt_refresh(&s->wdt_rr_phase, &s->wdtsr, value);
        break;
    case R_WDTCR:
        s->wdtcr = value;
        break;
    case R_WDTSR:
        s->wdtsr = value;
        break;
    case R_WDTRCR:
        s->wdtrcr = value;
        break;
    case R_IWDTRR:
        wdt_refresh(&s->iwdt_rr_phase, &s->iwdtsr, value);
        break;
    case R_IWDTCR:
        s->iwdtcr = value;
        break;
    case R_IWDTSR:
        s->iwdtsr = value;
        break;
    case R_IWDTRCR:
        s->iwdtrcr = value;
        break;
    case R_IWDTCSTPR:
        s->iwdtcstpr = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas-rx-wdt: write unimplemented 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps wdt_ops = {
    .read = wdt_read,
    .write = wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 2 },
    .valid = { .min_access_size = 1, .max_access_size = 2 },
};

static void rx_wdt_reset(DeviceState *dev)
{
    RenesasRxWdtState *s = RENESAS_RX_WDT(dev);

    s->wdtcr = 0x33f3;
    s->wdtsr = 0;
    s->wdtrcr = 0x80;
    s->wdt_rr_phase = 0;
    s->iwdtcr = 0;
    s->iwdtsr = 0;
    s->iwdtrcr = 0x80;
    s->iwdtcstpr = 0;
    s->iwdt_rr_phase = 0;
}

static void rx_wdt_init(Object *obj)
{
    RenesasRxWdtState *s = RENESAS_RX_WDT(obj);

    memory_region_init_io(&s->mr, obj, &wdt_ops, s,
                          "renesas-rx-wdt", RX_WDT_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
}

static const VMStateDescription vmstate_rx_wdt = {
    .name = "renesas-rx-wdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(wdtcr, RenesasRxWdtState),
        VMSTATE_UINT16(wdtsr, RenesasRxWdtState),
        VMSTATE_UINT8(wdtrcr, RenesasRxWdtState),
        VMSTATE_UINT8(wdt_rr_phase, RenesasRxWdtState),
        VMSTATE_UINT16(iwdtcr, RenesasRxWdtState),
        VMSTATE_UINT16(iwdtsr, RenesasRxWdtState),
        VMSTATE_UINT8(iwdtrcr, RenesasRxWdtState),
        VMSTATE_UINT8(iwdtcstpr, RenesasRxWdtState),
        VMSTATE_UINT8(iwdt_rr_phase, RenesasRxWdtState),
        VMSTATE_END_OF_LIST()
    }
};

static void rx_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rx_wdt;
    device_class_set_legacy_reset(dc, rx_wdt_reset);
}

static const TypeInfo rx_wdt_info = {
    .name = TYPE_RENESAS_RX_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasRxWdtState),
    .instance_init = rx_wdt_init,
    .class_init = rx_wdt_class_init,
};

static void rx_wdt_register_types(void)
{
    type_register_static(&rx_wdt_info);
}

type_init(rx_wdt_register_types)
