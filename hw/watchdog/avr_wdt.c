/*
 * AVR watchdog
 *
 * Copyright (c) 2021 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/watchdog/avr_wdt.h"
#include "trace.h"
#include "target/avr/cpu.h"
#include "sysemu/runstate.h"
#include "migration/vmstate.h"
#include "hw/registerfields.h"

REG8(CSR, 0x00)
    FIELD(CSR, WDP0, 0, 1)
    FIELD(CSR, WDP1, 1, 1)
    FIELD(CSR, WDP2, 2, 1)
    FIELD(CSR, WDE,  3, 1)
    FIELD(CSR, WDCE, 4, 1)
    FIELD(CSR, WDP3, 5, 1)
    FIELD(CSR, WDIE, 6, 1)
    FIELD(CSR, WDIF, 7, 1)

REG8(MCUSR, 0x55)
    FIELD(MCUSR, WDRF, 2, 1)

/* Helper macros */
#define WDP0(csr)       FIELD_EX8(csr, CSR, WDP0)
#define WDP1(csr)       FIELD_EX8(csr, CSR, WDP1)
#define WDP2(csr)       FIELD_EX8(csr, CSR, WDP2)
#define WDP3(csr)       FIELD_EX8(csr, CSR, WDP3)
#define WDP(csr)        ((WDP3(csr) << 3) | (WDP2(csr) << 2) | \
                         (WDP1(csr) << 1) | (WDP0(csr) << 0))
#define WDIE(csr)       FIELD_EX8(csr, CSR, WDIE)
#define WDE(csr)        FIELD_EX8(csr, CSR, WDE)
#define WCE(csr)        FIELD_EX8(csr, CSR, WVE)

#define DB_PRINT(fmt, args...) /* Nothing */

#define MS2NS(n)        ((n) * 1000000ull)

static void set_bits(uint8_t *addr, uint8_t bits)
{
    *addr |= bits;
}

static void rst_bits(uint8_t *addr, uint8_t bits)
{
    *addr &= ~bits;
}

static void avr_wdt_reset_alarm(AVRWatchdogState *wdt)
{
    uint32_t csr = wdt->csr;
    int wdp = WDP(csr);

    if (WDIE(csr) == 0 && WDE(csr) == 0) {
        /* watchdog is stopped */
        return;
    }

    timer_mod_ns(&wdt->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
            (MS2NS(15) << wdp));
}

static void avr_wdt_interrupt(void *opaque)
{
    AVRWatchdogState *wdt = opaque;
    int8_t csr = wdt->csr;

    if (WDIE(csr) == 1) {
        /* Interrupt Mode */
        set_bits(&wdt->csr, R_CSR_WDIF_MASK);
        qemu_set_irq(wdt->irq, 1);
        rst_bits(&wdt->csr, R_CSR_WDIE_MASK);
        trace_avr_wdt_interrupt();
    }

    if (WDE(csr) == 1) {
        /* System Reset Mode */
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }

    avr_wdt_reset_alarm(wdt);
}

static void avr_wdt_reset(DeviceState *dev)
{
    AVRWatchdogState *wdt = AVR_WDT(dev);

    wdt->csr = 0;
    qemu_set_irq(wdt->irq, 0);
    avr_wdt_reset_alarm(wdt);
}

static uint64_t avr_wdt_read(void *opaque, hwaddr offset, unsigned size)
{
    assert(size == 1);
    AVRWatchdogState *wdt = opaque;
    uint8_t retval = wdt->csr;

    trace_avr_wdt_read(offset, retval);

    return (uint64_t)retval;
}

static void avr_wdt_write(void *opaque, hwaddr offset,
                              uint64_t val64, unsigned size)
{
    assert(size == 1);
    AVRWatchdogState *wdt = opaque;
    uint8_t val = (uint8_t)val64;
    uint8_t set1 = val; /* bits that should be set to 1 */
    uint8_t set0 = ~val;/* bits that should be set to 0 */
    uint8_t mcusr = 0;

    /*
     *  Bit 7 - WDIF: Watchdog Interrupt Flag
     *  This bit is set when a time-out occurs in the Watchdog Timer and the
     *  Watchdog Timer is configured for interrupt. WDIF is cleared by hardware
     *  when executing the corresponding interrupt handling vector.
     *  Alternatively, WDIF is cleared by writing a logic one to the flag.
     *  When the I-bit in SREG and WDIE are set, the Watchdog Time-out Interrupt
     *  is executed.
     */
    if (val & R_CSR_WDIF_MASK) {
        rst_bits(&set1, R_CSR_WDIF_MASK);  /* don't set 1 */
        set_bits(&set0, R_CSR_WDIF_MASK);  /* set 0 */
    } else {
        rst_bits(&set1, R_CSR_WDIF_MASK);  /* don't set 1 */
        rst_bits(&set0, R_CSR_WDIF_MASK);  /* don't set 0 */
    }

    /*
     *  Bit 4 - WDCE: Watchdog Change Enable
     *  This bit is used in timed sequences for changing WDE and prescaler
     *  bits. To clear the WDE bit, and/or change the prescaler bits,
     *  WDCE must be set.
     *  Once written to one, hardware will clear WDCE after four clock cycles.
     */
    if (!(val & R_CSR_WDCE_MASK)) {
        uint8_t bits = R_CSR_WDE_MASK | R_CSR_WDP0_MASK | R_CSR_WDP1_MASK |
                       R_CSR_WDP2_MASK | R_CSR_WDP3_MASK;
        rst_bits(&set1, bits);
        rst_bits(&set0, bits);
    }

    /*
     *  Bit 3 - WDE: Watchdog System Reset Enable
     *  WDE is overridden by WDRF in MCUSR. This means that WDE is always set
     *  when WDRF is set. To clear WDE, WDRF must be cleared first. This
     *  feature ensures multiple resets during conditions causing failure, and
     *  a safe start-up after the failure.
     */
    cpu_physical_memory_read(A_MCUSR, &mcusr, sizeof(mcusr));
    if (mcusr & R_MCUSR_WDRF_MASK) {
        set_bits(&set1, R_CSR_WDE_MASK);
        rst_bits(&set0, R_CSR_WDE_MASK);
    }

    /*  update CSR value */
    assert((set0 & set1) == 0);

    val = wdt->csr;
    set_bits(&val, set1);
    rst_bits(&val, set0);
    wdt->csr = val;
    trace_avr_wdt_write(offset, val);
    avr_wdt_reset_alarm(wdt);

    /*
     *  Bit 6 - WDIE: Watchdog Interrupt Enable
     *  When this bit is written to one and the I-bit in the Status Register is
     *  set, the Watchdog Interrupt is enabled. If WDE is cleared in
     *  combination with this setting, the Watchdog Timer is in Interrupt Mode,
     *  and the corresponding interrupt is executed if time-out in the Watchdog
     *  Timer occurs.
     *  If WDE is set, the Watchdog Timer is in Interrupt and System Reset Mode.
     *  The first time-out in the Watchdog Timer will set WDIF. Executing the
     *  corresponding interrupt vector will clear WDIE and WDIF automatically by
     *  hardware (the Watchdog goes to System Reset Mode). This is useful for
     *  keeping the Watchdog Timer security while using the interrupt. To stay
     *  in Interrupt and System Reset Mode, WDIE must be set after each
     *  interrupt. This should however not be done within the interrupt service
     *  routine itself, as this might compromise the safety-function of the
     *  Watchdog System Reset mode. If the interrupt is not executed before the
     *  next time-out, a System Reset will be applied.
     */
    if ((val & R_CSR_WDIE_MASK) && (wdt->csr & R_CSR_WDIF_MASK)) {
        avr_wdt_interrupt(opaque);
    }
}

static const MemoryRegionOps avr_wdt_ops = {
    .read = avr_wdt_read,
    .write = avr_wdt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.max_access_size = 1}
};

static void avr_wdt_wdr(void *opaque, int irq, int level)
{
    AVRWatchdogState *wdt = AVR_WDT(opaque);

    avr_wdt_reset_alarm(wdt);
}

static void avr_wdt_init(Object *obj)
{
    AVRWatchdogState *s = AVR_WDT(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->iomem, obj, &avr_wdt_ops,
                          s, "avr-wdt", 0xa);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    qdev_init_gpio_in_named(DEVICE(s), avr_wdt_wdr, "wdr", 1);
}

static void avr_wdt_realize(DeviceState *dev, Error **errp)
{
    AVRWatchdogState *s = AVR_WDT(dev);

    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, avr_wdt_interrupt, s);
}

static const VMStateDescription avr_wdt_vmstate = {
    .name = "avr-wdt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER(timer, AVRWatchdogState),
        VMSTATE_UINT8(csr, AVRWatchdogState),
        VMSTATE_END_OF_LIST()
    }
};

static void avr_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = avr_wdt_reset;
    dc->realize = avr_wdt_realize;
    dc->vmsd = &avr_wdt_vmstate;
}

static const TypeInfo avr_wdt_info = {
    .name = TYPE_AVR_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AVRWatchdogState),
    .instance_init = avr_wdt_init,
    .class_init = avr_wdt_class_init,
};

static void avr_wdt_register_types(void)
{
    type_register_static(&avr_wdt_info);
}

type_init(avr_wdt_register_types)
