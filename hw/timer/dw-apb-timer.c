/*
 * Synopsys DesignWare APB timer
 *
 * Copyright (c) 2026 raoyi <rao232328@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/core/ptimer.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/timer/dw-apb-timer.h"
#include "trace.h"

#define DW_APB_TIMER_STRIDE       0x14
#define DW_APB_TIMER_MMIO_SIZE    0x100

/* Per-timer register offsets */
#define DW_APB_TIMER_N_LOAD_COUNT    0x00
#define DW_APB_TIMER_N_CURRENT_VALUE 0x04
#define DW_APB_TIMER_N_CONTROL       0x08
#define DW_APB_TIMER_N_EOI           0x0c
#define DW_APB_TIMER_N_INT_STATUS    0x10
/* Global register offsets */
#define DW_APB_TIMER_INT_STATUS        0xa0
#define DW_APB_TIMER_EOI               0xa4
#define DW_APB_TIMER_RAW_INT_STATUS    0xa8
#define DW_APB_TIMER_COMP_VERSION      0xac

/* Control register bits */
#define DW_APB_TIMER_CONTROL_ENABLE        BIT(0)
/* 1: periodic, 0: free running. */
#define DW_APB_TIMER_CONTROL_MODE_PERIODIC BIT(1)
#define DW_APB_TIMER_CONTROL_INT_MASK      BIT(2)
#define DW_APB_TIMER_CONTROL_RW_MASK       0x7

/*
 * Component version of the DW_apb_timers IP, fixed in silicon.
 * Decodes as the ASCII string "211*" (2.11a series); the model reports
 * the version integrated in K230. Linux does not read this register.
 */
#define DW_APB_TIMER_COMP_VERSION_VAL 0x3231312A

static void dw_apb_timer_update_irq(DWAPBTimerChannel *t)
{
    qemu_set_irq(t->irq, t->int_status &&
                 !(t->control & DW_APB_TIMER_CONTROL_INT_MASK));
}

static void dw_apb_timer_clk_update(void *opaque, ClockEvent event)
{
    DWAPBTimerChannel *t = opaque;

    if (!t->ptimer) {
        return;
    }
    ptimer_transaction_begin(t->ptimer);
    ptimer_set_period_from_clock(t->ptimer, t->clk, 1);
    ptimer_transaction_commit(t->ptimer);
}

static void dw_apb_timer_enable(DWAPBTimerChannel *t)
{
    trace_dw_apb_timer_enable(t->id, t->load);

    ptimer_transaction_begin(t->ptimer);
    ptimer_set_limit(t->ptimer, t->load ? t->load : 1, 1);
    ptimer_run(t->ptimer, 1);
    ptimer_transaction_commit(t->ptimer);
}

static void dw_apb_timer_disable(DWAPBTimerChannel *t)
{
    trace_dw_apb_timer_disable(t->id);

    ptimer_transaction_begin(t->ptimer);
    ptimer_stop(t->ptimer);
    ptimer_transaction_commit(t->ptimer);

    t->int_status = 0;
    dw_apb_timer_update_irq(t);
}

static void dw_apb_timer_tick(void *opaque)
{
    DWAPBTimerChannel *t = opaque;
    uint32_t reload;

    trace_dw_apb_timer_tick(t->id);

    t->int_status = 1;
    dw_apb_timer_update_irq(t);

    if (t->control & DW_APB_TIMER_CONTROL_MODE_PERIODIC) {
        reload = t->load ? t->load : 1;
    } else {
        reload = UINT32_MAX;
    }

    ptimer_set_limit(t->ptimer, reload, 1);
    ptimer_run(t->ptimer, 1);
}

static uint64_t dw_apb_timer_read(void *opaque, hwaddr addr,
                                  unsigned int size)
{
    DWAPBTimerState *s = DW_APB_TIMER(opaque);
    uint32_t value = 0;

    if (addr < DW_APB_TIMER_INT_STATUS) {
        unsigned int idx = addr / DW_APB_TIMER_STRIDE;
        hwaddr reg = addr % DW_APB_TIMER_STRIDE;

        if (idx < s->num_timers) {
            DWAPBTimerChannel *t = &s->timers[idx];

            switch (reg) {
            case DW_APB_TIMER_N_LOAD_COUNT:
                value = t->load;
                break;
            case DW_APB_TIMER_N_CURRENT_VALUE:
                if (t->control & DW_APB_TIMER_CONTROL_ENABLE) {
                    value = ptimer_get_count(t->ptimer);
                }
                break;
            case DW_APB_TIMER_N_CONTROL:
                value = t->control;
                break;
            case DW_APB_TIMER_N_EOI:
                t->int_status = 0;
                dw_apb_timer_update_irq(t);
                trace_dw_apb_timer_irq_clear(t->id);
                break;
            case DW_APB_TIMER_N_INT_STATUS:
                value = t->int_status &&
                        !(t->control & DW_APB_TIMER_CONTROL_INT_MASK);
                break;
            default:
                break;
            }
        }
    } else {
        switch (addr) {
        case DW_APB_TIMER_INT_STATUS:
            for (int i = 0; i < s->num_timers; i++) {
                DWAPBTimerChannel *t = &s->timers[i];

                value |= (t->int_status &&
                          !(t->control & DW_APB_TIMER_CONTROL_INT_MASK)) << i;
            }
            break;
        case DW_APB_TIMER_EOI:
            for (int i = 0; i < s->num_timers; i++) {
                DWAPBTimerChannel *t = &s->timers[i];

                t->int_status = 0;
                dw_apb_timer_update_irq(t);
            }
            break;
        case DW_APB_TIMER_RAW_INT_STATUS:
            for (int i = 0; i < s->num_timers; i++) {
                value |= s->timers[i].int_status << i;
            }
            break;
        case DW_APB_TIMER_COMP_VERSION:
            value = DW_APB_TIMER_COMP_VERSION_VAL;
            break;
        default:
            break;
        }
    }

    trace_dw_apb_timer_read(addr, value);
    return value;
}

static void dw_apb_timer_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned int size)
{
    DWAPBTimerState *s = DW_APB_TIMER(opaque);

    if (addr < DW_APB_TIMER_INT_STATUS) {
        unsigned int idx = addr / DW_APB_TIMER_STRIDE;
        hwaddr reg = addr % DW_APB_TIMER_STRIDE;

        if (idx < s->num_timers) {
            DWAPBTimerChannel *t = &s->timers[idx];

            switch (reg) {
            case DW_APB_TIMER_N_LOAD_COUNT:
                t->load = value;
                break;
            case DW_APB_TIMER_N_CONTROL: {
                uint32_t old_control = t->control;
                uint32_t new_control = value & DW_APB_TIMER_CONTROL_RW_MASK;

                t->control = new_control;
                if ((new_control ^ old_control) &
                    DW_APB_TIMER_CONTROL_ENABLE) {
                    if (new_control & DW_APB_TIMER_CONTROL_ENABLE) {
                        dw_apb_timer_enable(t);
                    } else {
                        dw_apb_timer_disable(t);
                    }
                }
                if ((new_control ^ old_control) &
                    DW_APB_TIMER_CONTROL_INT_MASK) {
                    dw_apb_timer_update_irq(t);
                }
                break;
            }
            default:
                break;
            }
        }
    }

    trace_dw_apb_timer_write(addr, value);
}

static const MemoryRegionOps dw_apb_timer_ops = {
    .read = dw_apb_timer_read,
    .write = dw_apb_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void dw_apb_timer_reset_hold(Object *obj, ResetType type)
{
    DWAPBTimerState *s = DW_APB_TIMER(obj);

    for (int i = 0; i < s->num_timers; i++) {
        DWAPBTimerChannel *t = &s->timers[i];

        ptimer_transaction_begin(t->ptimer);
        ptimer_stop(t->ptimer);
        ptimer_transaction_commit(t->ptimer);

        t->load = 0;
        t->control = 0;
        t->int_status = 0;
        dw_apb_timer_update_irq(t);
    }
}

static const VMStateDescription vmstate_dw_apb_timer_channel = {
    .name = "dw-apb-timer-channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PTIMER(ptimer, DWAPBTimerChannel),
        VMSTATE_CLOCK(clk, DWAPBTimerChannel),
        VMSTATE_UINT32(load, DWAPBTimerChannel),
        VMSTATE_UINT32(control, DWAPBTimerChannel),
        VMSTATE_UINT32(int_status, DWAPBTimerChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_dw_apb_timer = {
    .name = "dw-apb-timer",
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_UINT32(timers, DWAPBTimerState,
                                     num_timers, 0,
                                     vmstate_dw_apb_timer_channel,
                                     DWAPBTimerChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const Property dw_apb_timer_properties[] = {
    DEFINE_PROP_UINT32("num-timers", DWAPBTimerState, num_timers, 1),
};

static void dw_apb_timer_init(Object *obj)
{
    DWAPBTimerState *s = DW_APB_TIMER(obj);

    for (int i = 0; i < DW_APB_TIMER_MAX_TIMERS; i++) {
        DWAPBTimerChannel *t = &s->timers[i];
        g_autofree char *name = g_strdup_printf("timer[%d]", i);

        t->id = i;
        t->clk = qdev_init_clock_in(DEVICE(obj), name,
                                    dw_apb_timer_clk_update, t,
                                    ClockUpdate);
    }
}

static void dw_apb_timer_realize(DeviceState *dev, Error **errp)
{
    DWAPBTimerState *s = DW_APB_TIMER(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (s->num_timers == 0 || s->num_timers > DW_APB_TIMER_MAX_TIMERS) {
        error_setg(errp, "dw-apb-timer: num-timers must be between 1 and %u",
                   DW_APB_TIMER_MAX_TIMERS);
        return;
    }

    for (int i = 0; i < s->num_timers; i++) {
        DWAPBTimerChannel *t = &s->timers[i];

        if (!clock_has_source(t->clk)) {
            error_setg(errp, "dw-apb-timer: timer[%u] clock must be connected",
                       i);
            return;
        }
    }

    for (int i = 0; i < s->num_timers; i++) {
        DWAPBTimerChannel *t = &s->timers[i];

        t->ptimer = ptimer_init(dw_apb_timer_tick, t,
            PTIMER_POLICY_NO_IMMEDIATE_TRIGGER |
            PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
            PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);
        ptimer_transaction_begin(t->ptimer);
        ptimer_set_limit(t->ptimer, UINT32_MAX, 1);
        ptimer_transaction_commit(t->ptimer);
        sysbus_init_irq(sbd, &t->irq);

        /* The source may have been connected before the ptimer existed. */
        dw_apb_timer_clk_update(t, ClockUpdate);
    }

    memory_region_init_io(&s->mmio, OBJECT(dev), &dw_apb_timer_ops,
                          s, TYPE_DW_APB_TIMER, DW_APB_TIMER_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void dw_apb_timer_unrealize(DeviceState *dev)
{
    DWAPBTimerState *s = DW_APB_TIMER(dev);

    for (int i = 0; i < s->num_timers; i++) {
        ptimer_free(s->timers[i].ptimer);
    }
}

static void dw_apb_timer_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = dw_apb_timer_realize;
    dc->unrealize = dw_apb_timer_unrealize;
    dc->vmsd = &vmstate_dw_apb_timer;
    dc->desc = "Synopsys DesignWare APB timer";
    rc->phases.hold = dw_apb_timer_reset_hold;
    device_class_set_props(dc, dw_apb_timer_properties);
}

static const TypeInfo dw_apb_timer_info = {
    .name          = TYPE_DW_APB_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAPBTimerState),
    .instance_init = dw_apb_timer_init,
    .class_init    = dw_apb_timer_class_init,
};

static void dw_apb_timer_register_type(void)
{
    type_register_static(&dw_apb_timer_info);
}

type_init(dw_apb_timer_register_type)
