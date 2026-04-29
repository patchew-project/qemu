/*
 * PIC32MK General Purpose Timers × 9 (T1–T9)
 * Datasheet: DS60001519E, §14
 *
 * Timer types:
 *   Type A: Timer 1 only — 16-bit, asynchronous clock option
 *   Type B: Timer 2, 4, 6, 8 — 16-bit, 32-bit pairable with Type C
 *   Type C: Timer 3, 5, 7, 9 — 16-bit, pairs with preceding Type B
 *
 * Each timer instance is a SysBusDevice with:
 *   - One MMIO region (PIC32MK_TIMER_BLOCK_SIZE bytes)
 *   - QEMU ptimer for timing
 *   - One IRQ output (connected to EVIC input)
 *
 * Register layout within each 0x200-byte block
 * (registers have SET/CLR/INV sub-regs at +4/+8/+C):
 *   TxCON  +0x00  Control: ON, TCKPS, T32(Type B only), TCS
 *   TMRx   +0x10  Current count value (16 or 32-bit)
 *   PRx    +0x20  Period register — fires IRQ when TMR == PR
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/ptimer.h"
#include "hw/core/qdev-properties.h"
#include "hw/mips/pic32mk.h"

/*
 * Prescaler lookup tables:
 * Type A (Timer1): 2-bit TCKPS at bits[5:4], values 0..3 → {1,8,64,256}
 * Type B/C:        3-bit TCKPS at bits[6:4], values 0..7 → {1,2,4,8,16,32,64,256}
 */
static const uint32_t timer_prescalers_a[4] = { 1, 8, 64, 256 };
static const uint32_t timer_prescalers_bc[8] = { 1, 2, 4, 8, 16, 32, 64, 256 };

/*
 * Device state
 * -----------------------------------------------------------------------
 */

#define TYPE_PIC32MK_TIMER  "pic32mk-timer"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKTimerState, PIC32MK_TIMER)

struct PIC32MKTimerState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    uint32_t con;   /* TxCON */
    uint32_t tmr;   /* TMRx  */
    uint32_t pr;    /* PRx   — period (0xFFFF on reset for 16-bit) */

    ptimer_state *ptimer;
    qemu_irq irq;
    bool type_a;    /* true = Timer1 (Type A, 2-bit TCKPS {1,8,64,256}) */
};

/*
 * ptimer callback — fires when TMRx wraps (hits PRx)
 * -----------------------------------------------------------------------
 */

static void pic32mk_timer_expired(void *opaque)
{
    PIC32MKTimerState *s = opaque;

    /*
     * The ptimer already reloaded with the period; just assert the IRQ.
     * The EVIC input is level-sensitive: a pulse is sufficient because
     * the EVIC latches IFSx on the rising edge.
      */
    qemu_irq_pulse(s->irq);
}

/*
 * Start/stop/reload helpers
 * -----------------------------------------------------------------------
 */

static void timer_reload(PIC32MKTimerState *s)
{
    uint32_t prescale_idx = (s->con & PIC32MK_TCON_TCKPS_MASK)
                            >> PIC32MK_TCON_TCKPS_SHIFT;
    uint32_t prescale = s->type_a
        ? timer_prescalers_a[prescale_idx & 3]
        : timer_prescalers_bc[prescale_idx & 7];
    uint32_t period       = (s->pr == 0) ? 0xFFFFu : s->pr;

    ptimer_transaction_begin(s->ptimer);
    ptimer_set_freq(s->ptimer, PIC32MK_CPU_HZ / prescale);
    ptimer_set_limit(s->ptimer, period, 1);
    ptimer_run(s->ptimer, 0);   /* 0 = periodic */
    ptimer_transaction_commit(s->ptimer);
}

static void timer_stop(PIC32MKTimerState *s)
{
    ptimer_transaction_begin(s->ptimer);
    ptimer_stop(s->ptimer);
    ptimer_transaction_commit(s->ptimer);
}

/*
 * MMIO helpers
 * -----------------------------------------------------------------------
 */

/* PIC32MK: base+0=REG, +4=CLR, +8=SET, +0xC=INV */
static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0:
        *reg  = val;
        break;
    case 4:
        *reg &= ~val;
        break;
    case 8:
        *reg |= val;
        break;
    case 12:
        *reg ^= val;
        break;
    }
}

static uint32_t *timer_find_reg(PIC32MKTimerState *s, hwaddr base)
{
    switch (base) {
    case PIC32MK_TxCON:
        return &s->con;
    case PIC32MK_TMRx:
        return &s->tmr;
    case PIC32MK_PRx:
        return &s->pr;
    default:
        return NULL;
    }
}

/*
 * MMIO read/write
 * -----------------------------------------------------------------------
 */

static uint64_t timer_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKTimerState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xF;

    /* TMRx: return ptimer current count when timer is running */
    if (base == PIC32MK_TMRx) {
        if (s->con & PIC32MK_TCON_ON) {
            return (uint32_t)ptimer_get_count(s->ptimer);
        }
        return s->tmr;
    }

    uint32_t *reg = timer_find_reg(s, base);
    if (reg) {
        return *reg;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_timer: unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  addr);
    return 0;
}

static void timer_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKTimerState *s = opaque;
    int sub       = (int)(addr & 0xF);
    hwaddr base   = addr & ~(hwaddr)0xF;
    uint32_t *reg = timer_find_reg(s, base);

    if (!reg) {
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_timer: unimplemented write @ 0x%04"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                      addr, val);
        return;
    }

    bool was_on = !!(s->con & PIC32MK_TCON_ON);
    apply_sci(reg, (uint32_t)val, sub);

    /* TMRx write: update count when timer is stopped */
    if (base == PIC32MK_TMRx) {
        if (!was_on) {
            ptimer_transaction_begin(s->ptimer);
            ptimer_set_count(s->ptimer, s->tmr);
            ptimer_transaction_commit(s->ptimer);
        }
        return;
    }

    /* TxCON write: handle ON bit transitions */
    if (base == PIC32MK_TxCON) {
        bool now_on = !!(s->con & PIC32MK_TCON_ON);
        if (!was_on && now_on) {
            timer_reload(s);
        } else if (was_on && !now_on) {
            timer_stop(s);
            /* Save current count */
            s->tmr = (uint32_t)ptimer_get_count(s->ptimer);
        } else if (now_on) {
            /* Prescaler may have changed; reload */
            timer_reload(s);
        }
    }

    /* PRx write: update period on the fly */
    if (base == PIC32MK_PRx && (s->con & PIC32MK_TCON_ON)) {
        timer_reload(s);
    }
}

static const MemoryRegionOps timer_ops = {
    .read       = timer_read,
    .write      = timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_timer_reset(DeviceState *dev)
{
    PIC32MKTimerState *s = PIC32MK_TIMER(dev);

    timer_stop(s);
    s->con = 0;
    s->tmr = 0;
    s->pr  = 0xFFFF;   /* 16-bit period register reset value */
    qemu_irq_lower(s->irq);
}

static void pic32mk_timer_init(Object *obj)
{
    PIC32MKTimerState *s = PIC32MK_TIMER(obj);

    memory_region_init_io(&s->mr, obj, &timer_ops, s,
                          TYPE_PIC32MK_TIMER, PIC32MK_TIMER_BLOCK_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    s->ptimer = ptimer_init(pic32mk_timer_expired, s, PTIMER_POLICY_NO_IMMEDIATE_TRIGGER);
}

static void pic32mk_timer_finalize(Object *obj)
{
    PIC32MKTimerState *s = PIC32MK_TIMER(obj);
    ptimer_free(s->ptimer);
}

static Property pic32mk_timer_properties[] = {
    DEFINE_PROP_BOOL("type-a", PIC32MKTimerState, type_a, false),
};

static void pic32mk_timer_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_timer_reset);
    device_class_set_props(dc, pic32mk_timer_properties);
}

static const TypeInfo pic32mk_timer_info = {
    .name          = TYPE_PIC32MK_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKTimerState),
    .instance_init = pic32mk_timer_init,
    .instance_finalize = pic32mk_timer_finalize,
    .class_init    = pic32mk_timer_class_init,
};

static void pic32mk_timer_register_types(void)
{
    type_register_static(&pic32mk_timer_info);
}

type_init(pic32mk_timer_register_types)
