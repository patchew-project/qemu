/*
 * nRF51 System-on-Chip Timer peripheral
 *
 * Reference Manual: http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.pdf
 * Product Spec: http://infocenter.nordicsemi.com/pdf/nRF51822_PS_v3.1.pdf
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/arm/nrf51.h"
#include "hw/timer/nrf51_timer.h"
#include "trace.h"

#define MINIMUM_PERIOD 10000UL
#define TIMER_TICK_PS 62500UL

static uint32_t const bitwidths[] = {16, 8, 24, 32};

static void set_prescaler(NRF51TimerState *s, uint32_t prescaler)
{
    uint64_t period;
    s->prescaler = prescaler;

    period = ((1UL << s->prescaler) * TIMER_TICK_PS) / 1000;
    /* Limit minimum timeout period to 10us to allow some progress */
    if (period < MINIMUM_PERIOD) {
        s->tick_period = MINIMUM_PERIOD;
        s->counter_inc = MINIMUM_PERIOD / period;
    } else {
        s->tick_period = period;
        s->counter_inc = 1;
    }
}

static void update_irq(NRF51TimerState *s)
{
    bool flag = false;
    size_t i;

    for (i = 0; i < NRF51_TIMER_REG_COUNT; i++) {
        flag |= s->events_compare[i] && extract32(s->inten, 16 + i, 1);
    }
    qemu_set_irq(s->irq, flag);
}

static void timer_expire(void *opaque)
{
    NRF51TimerState *s = NRF51_TIMER(opaque);
    bool should_stop = false;
    uint32_t counter = s->counter;
    size_t i;
    uint64_t diff;

    if (s->running) {
        for (i = 0; i < NRF51_TIMER_REG_COUNT; i++) {
            if (counter < s->cc[i]) {
                diff = s->cc[i] - counter;
            } else {
                diff = (s->cc[i] + BIT(bitwidths[s->bitmode])) - counter;
            }

            if (diff <= s->counter_inc) {
                s->events_compare[i] = true;

                if (s->shorts & BIT(i)) {
                    s->counter = 0;
                }

                should_stop |= s->shorts & BIT(i + 8);
            }
        }

        s->counter += s->counter_inc;
        s->counter &= (BIT(bitwidths[s->bitmode]) - 1);

        update_irq(s);

        if (should_stop) {
            s->running = false;
            timer_del(&s->timer);
        } else {
            s->time_offset += s->tick_period;
            timer_mod_ns(&s->timer, s->time_offset);
        }
    } else {
        timer_del(&s->timer);
    }
}

static void counter_compare(NRF51TimerState *s)
{
    uint32_t counter = s->counter;
    size_t i;
    for (i = 0; i < NRF51_TIMER_REG_COUNT; i++) {
        if (counter == s->cc[i]) {
            s->events_compare[i] = true;

            if (s->shorts & BIT(i)) {
                s->counter = 0;
            }
        }
    }
}

static uint64_t nrf51_timer_read(void *opaque, hwaddr offset, unsigned int size)
{
    NRF51TimerState *s = NRF51_TIMER(opaque);
    uint64_t r = 0;

    switch (offset) {
    case NRF51_TIMER_EVENT_COMPARE_0 ... NRF51_TIMER_EVENT_COMPARE_3:
        r = s->events_compare[(offset - NRF51_TIMER_EVENT_COMPARE_0) / 4];
        break;
    case NRF51_TIMER_REG_SHORTS:
        r = s->shorts;
        break;
    case NRF51_TIMER_REG_INTENSET:
        r = s->inten;
        break;
    case NRF51_TIMER_REG_INTENCLR:
        r = s->inten;
        break;
    case NRF51_TIMER_REG_MODE:
        r = s->mode;
        break;
    case NRF51_TIMER_REG_BITMODE:
        r = s->bitmode;
        break;
    case NRF51_TIMER_REG_PRESCALER:
        r = s->prescaler;
        break;
    case NRF51_TIMER_REG_CC0 ... NRF51_TIMER_REG_CC3:
        r = s->cc[(offset - NRF51_TIMER_REG_CC0) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    trace_nrf51_timer_read(offset, r, size);

    return r;
}

static void nrf51_timer_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned int size)
{
    NRF51TimerState *s = NRF51_TIMER(opaque);
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    size_t idx;

    trace_nrf51_timer_write(offset, value, size);

    switch (offset) {
    case NRF51_TIMER_TASK_START:
        if (value == NRF51_TRIGGER_TASK && s->mode == NRF51_TIMER_TIMER) {
            s->running = true;
            s->time_offset = now + s->tick_period;
            timer_mod_ns(&s->timer, s->time_offset);
        }
        break;
    case NRF51_TIMER_TASK_STOP:
    case NRF51_TIMER_TASK_SHUTDOWN:
        if (value == NRF51_TRIGGER_TASK) {
            s->running = false;
            timer_del(&s->timer);
        }
        break;
    case NRF51_TIMER_TASK_COUNT:
        if (value == NRF51_TRIGGER_TASK && s->mode == NRF51_TIMER_COUNTER) {
            s->counter = (s->counter + 1) & (BIT(bitwidths[s->bitmode]) - 1);
            counter_compare(s);
        }
        break;
    case NRF51_TIMER_TASK_CLEAR:
        if (value == NRF51_TRIGGER_TASK) {
            s->counter = 0;
        }
        break;
    case NRF51_TIMER_TASK_CAPTURE_0 ... NRF51_TIMER_TASK_CAPTURE_3:
        if (value == NRF51_TRIGGER_TASK) {
            idx = (offset - NRF51_TIMER_TASK_CAPTURE_0) / 4;
            s->cc[idx] = s->counter;
        }
        break;
    case NRF51_TIMER_EVENT_COMPARE_0 ... NRF51_TIMER_EVENT_COMPARE_3:
        if (value == NRF51_EVENT_CLEAR) {
            s->events_compare[(offset - NRF51_TIMER_EVENT_COMPARE_0) / 4] = 0;
        }
        break;
    case NRF51_TIMER_REG_SHORTS:
        s->shorts = value & NRF51_TIMER_REG_SHORTS_MASK;
        break;
    case NRF51_TIMER_REG_INTENSET:
        s->inten |= value & NRF51_TIMER_REG_INTEN_MASK;
        break;
    case NRF51_TIMER_REG_INTENCLR:
        s->inten &= ~(value & NRF51_TIMER_REG_INTEN_MASK);
        break;
    case NRF51_TIMER_REG_MODE:
        s->mode = value;
        break;
    case NRF51_TIMER_REG_BITMODE:
        if (s->mode == NRF51_TIMER_TIMER && s->running) {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: erroneous change of BITMODE while timer is running\n",
                    __func__);
        }
        s->bitmode = value & NRF51_TIMER_REG_BITMODE_MASK;
        break;
    case NRF51_TIMER_REG_PRESCALER:
        if (s->mode == NRF51_TIMER_TIMER && s->running) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: erroneous change of PRESCALER while timer is running\n",
                __func__);
        }
        set_prescaler(s, value & NRF51_TIMER_REG_PRESCALER_MASK);
        break;
    case NRF51_TIMER_REG_CC0 ... NRF51_TIMER_REG_CC3:
        idx = (offset - NRF51_TIMER_REG_CC0) / 4;
        s->cc[idx] = value & (BIT(bitwidths[s->bitmode]) - 1);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    update_irq(s);
}

static const MemoryRegionOps rng_ops = {
    .read =  nrf51_timer_read,
    .write = nrf51_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void nrf51_timer_init(Object *obj)
{
    NRF51TimerState *s = NRF51_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &rng_ops, s,
            TYPE_NRF51_TIMER, NRF51_TIMER_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, timer_expire, s);
}

static void nrf51_timer_reset(DeviceState *dev)
{
    NRF51TimerState *s = NRF51_TIMER(dev);

    timer_del(&s->timer);
    s->time_offset = 0x00;
    s->counter = 0x00;
    s->counter_inc = 0x00;
    s->tick_period = 0x00;
    s->running = false;

    memset(s->events_compare, 0x00, sizeof(s->events_compare));
    memset(s->cc, 0x00, sizeof(s->cc));

    s->shorts = 0x00;
    s->inten = 0x00;
    s->mode = 0x00;
    s->bitmode = 0x00;
    set_prescaler(s, 0x00);
}

static int nrf51_timer_post_load(void *opaque, int version_id)
{
    NRF51TimerState *s = NRF51_TIMER(opaque);
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (s->running && s->mode == NRF51_TIMER_TIMER) {
        s->time_offset = now;
        timer_mod_ns(&s->timer, s->time_offset);
    }

    return 0;
}

static const VMStateDescription vmstate_nrf51_timer = {
    .name = TYPE_NRF51_TIMER,
    .version_id = 1,
    .post_load = nrf51_timer_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER(timer, NRF51TimerState),
        VMSTATE_INT64(time_offset, NRF51TimerState),
        VMSTATE_UINT32(counter, NRF51TimerState),
        VMSTATE_UINT32(counter_inc, NRF51TimerState),
        VMSTATE_UINT64(tick_period, NRF51TimerState),
        VMSTATE_BOOL(running, NRF51TimerState),
        VMSTATE_UINT8_ARRAY(events_compare, NRF51TimerState,
                            NRF51_TIMER_REG_COUNT),
        VMSTATE_UINT32_ARRAY(cc, NRF51TimerState, NRF51_TIMER_REG_COUNT),
        VMSTATE_UINT32(shorts, NRF51TimerState),
        VMSTATE_UINT32(inten, NRF51TimerState),
        VMSTATE_UINT32(mode, NRF51TimerState),
        VMSTATE_UINT32(bitmode, NRF51TimerState),
        VMSTATE_UINT32(prescaler, NRF51TimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void nrf51_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = nrf51_timer_reset;
    dc->vmsd = &vmstate_nrf51_timer;
}

static const TypeInfo nrf51_timer_info = {
    .name = TYPE_NRF51_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF51TimerState),
    .instance_init = nrf51_timer_init,
    .class_init = nrf51_timer_class_init
};

static void nrf51_timer_register_types(void)
{
    type_register_static(&nrf51_timer_info);
}

type_init(nrf51_timer_register_types)
