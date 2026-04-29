/*
 * Microchip PIC32MK — Watchdog Timer (WDT)
 * Datasheet: DS60001519E §17, Register 17-1
 *
 * Models the WDTCON register, the clear-key mechanism (write 0x5743
 * to upper 16 bits via 16-bit write to WDTCON+2), and a real countdown
 * timer that triggers a system reset if firmware fails to clear the
 * WDT in time.
 *
 * WDTCON layout:
 *   bits[31:16] WDTCLRKEY  — write 0x5743 here to clear the counter
 *   bit  15     ON         — WDT enable
 *   bits[12:8]  RUNDIV     — read-only post-scaler (reflects config bits)
 *   bits[5:1]   SLPDIV     — read-only post-scaler (reflects config bits)
 *   bit  0      WDTWINEN   — windowed mode enable
 *
 * The timeout period is derived from the LPRC (32 kHz) oscillator
 * and the RUNDIV post-scaler.  Default RUNDIV=0 ≈ 1 ms base period,
 * giving a ~1 s timeout with the default post-scaler (WDTPS ≈ 20).
 * In emulation we use a configurable period (default 2 s) that can
 * be tuned via the "timeout-ms" qdev property.
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/mips/pic32mk.h"
#include "system/runstate.h"

/*
 * WDTCON register bit positions (DS60001519E §17, Register 17-1)
 * -----------------------------------------------------------------------
 */
#define WDTCON_ON       (1u << 15)  /* WDT enable */
#define WDTCON_WDTWINEN (1u << 0)   /* Windowed mode enable — bit 0 */
#define WDTCON_WR_MASK  (WDTCON_ON | WDTCON_WDTWINEN)  /* writable bits */

/* Upper-halfword clear key written by firmware WDT_Clear() */
#define WDT_CLRKEY      0x5743u

/* Default timeout in milliseconds (~2 s, conservative for emulation) */
#define WDT_DEFAULT_TIMEOUT_MS  2000

#define TYPE_PIC32MK_WDT "pic32mk-wdt"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKWDTState, PIC32MK_WDT)

typedef struct PIC32MKWDTState {
    SysBusDevice parent_obj;
    MemoryRegion  mmio;
    uint32_t      wdtcon;       /* WDTCON register value */
    QEMUTimer     timer;        /* Countdown timer */
    uint32_t      timeout_ms;   /* Timeout period (ms), qdev property */
} PIC32MKWDTState;

/*
 * Timer helpers
 * -----------------------------------------------------------------------
 */

static void pic32mk_wdt_rearm(PIC32MKWDTState *s)
{
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    timer_mod(&s->timer, now + s->timeout_ms);
}

static void pic32mk_wdt_stop(PIC32MKWDTState *s)
{
    timer_del(&s->timer);
}

static void pic32mk_wdt_update(PIC32MKWDTState *s, uint32_t old)
{
    bool was_on = (old & WDTCON_ON) != 0;
    bool is_on  = (s->wdtcon & WDTCON_ON) != 0;

    if (is_on && !was_on) {
        /* Turning ON — start countdown */
        pic32mk_wdt_rearm(s);
    } else if (!is_on && was_on) {
        /* Turning OFF — stop countdown */
        pic32mk_wdt_stop(s);
    }
}

/* Timer callback — WDT expired, trigger system reset */
static void pic32mk_wdt_expire(void *opaque)
{
    PIC32MKWDTState *s = PIC32MK_WDT(opaque);

    if (!(s->wdtcon & WDTCON_ON)) {
        return; /* Race: turned off between schedule and callback */
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "pic32mk-wdt: watchdog timeout — system reset\n");
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}

/*
 * Read handler — WDTCON + SET/CLR/INV aliases
 * -----------------------------------------------------------------------
 */
static uint64_t pic32mk_wdt_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKWDTState *s = PIC32MK_WDT(opaque);

    switch (addr & ~0x3u) {
    case 0x00:
        /* WDTCON (base, +4 SET, +8 CLR, +C INV all read same) */;
    case 0x04:
    case 0x08:
    case 0x0C:
        return s->wdtcon;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk-wdt: unimplemented read @ 0x%" HWADDR_PRIx "\n",
                      addr);
        return 0;
    }
}

/*
 * Write handler — WDTCON + SET/CLR/INV; clear-key on upper 16 bits
 * -----------------------------------------------------------------------
 */
static void pic32mk_wdt_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    PIC32MKWDTState *s = PIC32MK_WDT(opaque);
    uint32_t old = s->wdtcon;

    /*
     * firmware WDT_Clear() does a 16-bit write of 0x5743 to WDTCON+2
     * (the upper halfword).  Detect by: addr==2, size==2, val==0x5743.
     * Restart the countdown timer.
     */
    if (addr == 2 && size == 2 && (uint16_t)val == WDT_CLRKEY) {
        if (s->wdtcon & WDTCON_ON) {
            pic32mk_wdt_rearm(s);
        }
        return;
    }

    switch (addr & 0xCu) {
    case 0x0:
        /* WDTCON write */;
        s->wdtcon = (uint32_t)val & WDTCON_WR_MASK;
        break;
    case 0x4:
        /* SET */;
        s->wdtcon |= (uint32_t)val & WDTCON_WR_MASK;
        break;
    case 0x8:
        /* CLR */;
        s->wdtcon &= ~((uint32_t)val & WDTCON_WR_MASK);
        break;
    case 0xC:
        /* INV */;
        s->wdtcon ^= (uint32_t)val & WDTCON_WR_MASK;
        break;
    }

    pic32mk_wdt_update(s, old);
}

static const MemoryRegionOps pic32mk_wdt_ops = {
    .read       = pic32mk_wdt_read,
    .write      = pic32mk_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 2,
        .max_access_size = 4,
    },
};

static void pic32mk_wdt_realize(DeviceState *dev, Error **errp)
{
    PIC32MKWDTState *s = PIC32MK_WDT(dev);
    memory_region_init_io(&s->mmio, OBJECT(s), &pic32mk_wdt_ops, s,
                          "pic32mk-wdt", PIC32MK_WDT_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    timer_init_ms(&s->timer, QEMU_CLOCK_VIRTUAL,
                  pic32mk_wdt_expire, s);
}

static void pic32mk_wdt_reset_hold(Object *obj, ResetType type)
{
    PIC32MKWDTState *s = PIC32MK_WDT(obj);
    timer_del(&s->timer);
    s->wdtcon = 0;
}

static const Property pic32mk_wdt_properties[] = {
    DEFINE_PROP_UINT32("timeout-ms", PIC32MKWDTState, timeout_ms,
                       WDT_DEFAULT_TIMEOUT_MS),
};

static void pic32mk_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    dc->realize = pic32mk_wdt_realize;
    dc->desc    = "PIC32MK Watchdog Timer";
    device_class_set_props(dc, pic32mk_wdt_properties);
    rc->phases.hold = pic32mk_wdt_reset_hold;
}

static const TypeInfo pic32mk_wdt_info = {
    .name          = TYPE_PIC32MK_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKWDTState),
    .class_init    = pic32mk_wdt_class_init,
};

static void pic32mk_wdt_register_types(void)
{
    type_register_static(&pic32mk_wdt_info);
}

type_init(pic32mk_wdt_register_types)
