/*
 * Allwinner A10 timer device emulation
 *
 * Copyright (C) 2013 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/timer/allwinner-a10-pit.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define AW_A10_PIT_TIMER_NR         6

#define AW_A10_PIT_TIMER_IRQ_EN     0
#define AW_A10_PIT_TIMER_IRQ_ST     0x4

#define AW_A10_PIT_TIMER_CONTROL    0x0
#define AW_A10_PIT_TIMER_EN         0x1
#define AW_A10_PIT_TIMER_RELOAD     0x2
#define AW_A10_PIT_TIMER_MODE       0x80

#define AW_A10_PIT_TIMER_INTERVAL   0x4
#define AW_A10_PIT_TIMER_COUNT      0x8
#define AW_A10_PIT_WDOG_CONTROL     0x90
#define AW_A10_PIT_WDOG_MODE        0x94

#define AW_A10_PIT_COUNT_CTL        0xa0
#define AW_A10_PIT_COUNT_RL_EN      0x2
#define AW_A10_PIT_COUNT_CLR_EN     0x1
#define AW_A10_PIT_COUNT_LO         0xa4
#define AW_A10_PIT_COUNT_HI         0xa8

#define AW_A10_PIT_TIMER_BASE       0x10
#define AW_A10_PIT_TIMER_BASE_END   \
    (AW_A10_PIT_TIMER_BASE * AW_A10_PIT_TIMER_NR + AW_A10_PIT_TIMER_COUNT)

#define AW_A10_PIT_DEFAULT_CLOCK    0x4

#define AW_A10_PIT(obj) \
    OBJECT_CHECK(AllwinnerTmrCtrlState, (obj), TYPE_AW_A10_PIT)

static void a10_pit_update_irq(AllwinnerTmrCtrlState *s)
{
    int i;

    for (i = 0; i < s->timer_count; i++) {
        qemu_set_irq(s->timer[i].irq,
                     !!(s->irq_status & s->irq_enable & (1 << i)));
    }
}

static uint64_t a10_pit_read(void *opaque, hwaddr offset, unsigned size)
{
    AllwinnerTmrCtrlState *s = AW_A10_PIT(opaque);
    uint8_t index;

    switch (offset) {
    case AW_A10_PIT_TIMER_IRQ_EN:
        return s->irq_enable;
    case AW_A10_PIT_TIMER_IRQ_ST:
        return s->irq_status;
    case AW_A10_PIT_TIMER_BASE ... AW_A10_PIT_TIMER_BASE_END:
        index = offset & 0xf0;
        index >>= 4;
        index -= 1;
        switch (offset & 0x0f) {
        case AW_A10_PIT_TIMER_CONTROL:
            return s->timer[index].control;
        case AW_A10_PIT_TIMER_INTERVAL:
            return s->timer[index].interval;
        case AW_A10_PIT_TIMER_COUNT:
            s->timer[index].count = ptimer_get_count(s->timer[index].ptimer);
            return s->timer[index].count;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%x\n",  __func__, (int)offset);
            break;
        }
    case AW_A10_PIT_WDOG_CONTROL:
        break;
    case AW_A10_PIT_WDOG_MODE:
        break;
    case AW_A10_PIT_COUNT_LO:
        return s->count_lo;
    case AW_A10_PIT_COUNT_HI:
        return s->count_hi;
    case AW_A10_PIT_COUNT_CTL:
        return s->count_ctl;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    return 0;
}

/* Must be called inside a ptimer transaction block for s->timer[idx].ptimer */
static void a10_pit_set_freq(AllwinnerTmrCtrlState *s, int index)
{
    uint32_t prescaler, source, source_freq;

    prescaler = 1 << extract32(s->timer[index].control, 4, 3);
    source = extract32(s->timer[index].control, 2, 2);
    source_freq = s->clk_freq[source];

    if (source_freq) {
        ptimer_set_freq(s->timer[index].ptimer, source_freq / prescaler);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid clock source %u\n",
                      __func__, source);
    }
}

static void a10_pit_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
     AllwinnerTmrCtrlState *s = AW_A10_PIT(opaque);
     uint8_t index;

    switch (offset) {
    case AW_A10_PIT_TIMER_IRQ_EN:
        s->irq_enable = value;
        a10_pit_update_irq(s);
        break;
    case AW_A10_PIT_TIMER_IRQ_ST:
        s->irq_status &= ~value;
        a10_pit_update_irq(s);
        break;
    case AW_A10_PIT_TIMER_BASE ... AW_A10_PIT_TIMER_BASE_END:
        index = offset & 0xf0;
        index >>= 4;
        index -= 1;
        switch (offset & 0x0f) {
        case AW_A10_PIT_TIMER_CONTROL:
            s->timer[index].control = value;
            ptimer_transaction_begin(s->timer[index].ptimer);
            a10_pit_set_freq(s, index);
            if (s->timer[index].control & AW_A10_PIT_TIMER_RELOAD) {
                ptimer_set_count(s->timer[index].ptimer,
                                 s->timer[index].interval);
            }
            if (s->timer[index].control & AW_A10_PIT_TIMER_EN) {
                int oneshot = 0;
                if (s->timer[index].control & AW_A10_PIT_TIMER_MODE) {
                    oneshot = 1;
                }
                ptimer_run(s->timer[index].ptimer, oneshot);
            } else {
                ptimer_stop(s->timer[index].ptimer);
            }
            ptimer_transaction_commit(s->timer[index].ptimer);
            break;
        case AW_A10_PIT_TIMER_INTERVAL:
            s->timer[index].interval = value;
            ptimer_transaction_begin(s->timer[index].ptimer);
            ptimer_set_limit(s->timer[index].ptimer,
                             s->timer[index].interval, 1);
            ptimer_transaction_commit(s->timer[index].ptimer);
            break;
        case AW_A10_PIT_TIMER_COUNT:
            s->timer[index].count = value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        }
        break;
    case AW_A10_PIT_WDOG_CONTROL:
        s->watch_dog_control = value;
        break;
    case AW_A10_PIT_WDOG_MODE:
        s->watch_dog_mode = value;
        break;
    case AW_A10_PIT_COUNT_LO:
        s->count_lo = value;
        break;
    case AW_A10_PIT_COUNT_HI:
        s->count_hi = value;
        break;
    case AW_A10_PIT_COUNT_CTL:
        s->count_ctl = value;
        if (s->count_ctl & AW_A10_PIT_COUNT_RL_EN) {
            uint64_t  tmp_count = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

            s->count_lo = tmp_count;
            s->count_hi = tmp_count >> 32;
            s->count_ctl &= ~AW_A10_PIT_COUNT_RL_EN;
        }
        if (s->count_ctl & AW_A10_PIT_COUNT_CLR_EN) {
            s->count_lo = 0;
            s->count_hi = 0;
            s->count_ctl &= ~AW_A10_PIT_COUNT_CLR_EN;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }
}

static const MemoryRegionOps a10_pit_ops = {
    .read = a10_pit_read,
    .write = a10_pit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static Property a10_pit_properties[] = {
    DEFINE_PROP_UINT32("clk0-freq", AllwinnerTmrCtrlState, clk_freq[0], 0),
    DEFINE_PROP_UINT32("clk1-freq", AllwinnerTmrCtrlState, clk_freq[1], 0),
    DEFINE_PROP_UINT32("clk2-freq", AllwinnerTmrCtrlState, clk_freq[2], 0),
    DEFINE_PROP_UINT32("clk3-freq", AllwinnerTmrCtrlState, clk_freq[3], 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_aw_timer = {
    .name = "aw_timer",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(control, AllwinnerTmrState),
        VMSTATE_UINT32(interval, AllwinnerTmrState),
        VMSTATE_UINT32(count, AllwinnerTmrState),
        VMSTATE_PTIMER(ptimer, AllwinnerTmrState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_a10_pit = {
    .name = "a10.pit",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(irq_enable, AllwinnerTmrCtrlState),
        VMSTATE_UINT32(irq_status, AllwinnerTmrCtrlState),
        VMSTATE_STRUCT_ARRAY(timer, AllwinnerTmrCtrlState,
                             AW_PIT_TIMER_MAX,
                             0, vmstate_aw_timer,
                             AllwinnerTmrState),
        VMSTATE_UINT32(watch_dog_mode, AllwinnerTmrCtrlState),
        VMSTATE_UINT32(watch_dog_control, AllwinnerTmrCtrlState),
        VMSTATE_UINT32(count_lo, AllwinnerTmrCtrlState),
        VMSTATE_UINT32(count_hi, AllwinnerTmrCtrlState),
        VMSTATE_UINT32(count_ctl, AllwinnerTmrCtrlState),
        VMSTATE_END_OF_LIST()
    }
};

static void a10_pit_reset(DeviceState *dev)
{
    AllwinnerTmrCtrlState *s = AW_A10_PIT(dev);
    uint8_t i;

    s->irq_enable = 0;
    s->irq_status = 0;
    a10_pit_update_irq(s);

    for (i = 0; i < s->timer_count; i++) {
        s->timer[i].control = AW_A10_PIT_DEFAULT_CLOCK;
        s->timer[i].interval = 0;
        s->timer[i].count = 0;
        ptimer_transaction_begin(s->timer[i].ptimer);
        ptimer_stop(s->timer[i].ptimer);
        a10_pit_set_freq(s, i);
        ptimer_transaction_commit(s->timer[i].ptimer);
    }
    s->watch_dog_mode = 0;
    s->watch_dog_control = 0;
    s->count_lo = 0;
    s->count_hi = 0;
    s->count_ctl = 0;
}

static void a10_pit_timer_cb(void *opaque)
{
    AllwinnerTmrState *tc = opaque;
    AllwinnerTmrCtrlState *s = tc->container;
    uint8_t i = tc->index;

    if (s->timer[i].control & AW_A10_PIT_TIMER_EN) {
        s->irq_status |= 1 << i;
        if (s->timer[i].control & AW_A10_PIT_TIMER_MODE) {
            ptimer_stop(s->timer[i].ptimer);
            s->timer[i].control &= ~AW_A10_PIT_TIMER_EN;
        }
        a10_pit_update_irq(s);
    }
}

static void a10_pit_init(Object *obj)
{
    AllwinnerTmrCtrlState *s = AW_A10_PIT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    uint8_t i;

    s->timer_count = AW_A10_PIT_TIMER_NR;

    for (i = 0; i < s->timer_count; i++) {
        sysbus_init_irq(sbd, &s->timer[i].irq);
    }
    memory_region_init_io(&s->iomem, OBJECT(s), &a10_pit_ops, s,
                          TYPE_AW_A10_PIT, 0x400);
    sysbus_init_mmio(sbd, &s->iomem);

    for (i = 0; i < s->timer_count; i++) {
        AllwinnerTmrState *tc = &s->timer[i];

        tc->container = s;
        tc->index = i;
        s->timer[i].ptimer = ptimer_init(a10_pit_timer_cb, tc,
                                         PTIMER_POLICY_DEFAULT);
    }
}

static void a10_pit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = a10_pit_reset;
    dc->props = a10_pit_properties;
    dc->desc = "allwinner a10 timer";
    dc->vmsd = &vmstate_a10_pit;
}

static const TypeInfo a10_pit_info = {
    .name = TYPE_AW_A10_PIT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AllwinnerTmrCtrlState),
    .instance_init = a10_pit_init,
    .class_init = a10_pit_class_init,
};

static void a10_register_types(void)
{
    type_register_static(&a10_pit_info);
}

type_init(a10_register_types);
