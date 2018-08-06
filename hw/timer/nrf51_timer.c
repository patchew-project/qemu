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
#include "hw/timer/nrf51_timer.h"
#include "trace.h"

#define NRF51_TIMER_SIZE         0x1000

#define NRF51_TIMER_TASK_START 0x000
#define NRF51_TIMER_TASK_STOP 0x004
#define NRF51_TIMER_TASK_COUNT 0x008
#define NRF51_TIMER_TASK_CLEAR 0x00C
#define NRF51_TIMER_TASK_SHUTDOWN 0x010
#define NRF51_TIMER_TASK_CAPTURE_0 0x040
#define NRF51_TIMER_TASK_CAPTURE_3 0x04C

#define NRF51_TIMER_EVENT_COMPARE_0 0x140
#define NRF51_TIMER_EVENT_COMPARE_3 0x14C

#define NRF51_TIMER_REG_SHORTS 0x200
#define NRF51_TIMER_REG_SHORTS_MASK 0xf0f
#define NRF51_TIMER_REG_INTENSET 0x304
#define NRF51_TIMER_REG_INTENCLR 0x308
#define NRF51_TIMER_REG_INTEN_MASK 0xf0000
#define NRF51_TIMER_REG_MODE 0x504
#define NRF51_TIMER_REG_MODE_MASK 0x01
#define NRF51_TIMER_REG_BITMODE 0x508
#define NRF51_TIMER_REG_BITMODE_MASK 0x03
#define NRF51_TIMER_REG_PRESCALER 0x510
#define NRF51_TIMER_REG_PRESCALER_MASK 0x0F
#define NRF51_TIMER_REG_CC0 0x540
#define NRF51_TIMER_REG_CC3 0x54C

#define TIMER_CLK 16000000ULL

/* Trigger */
#define NRF51_TRIGGER_TASK 0x01

/* Events */
#define NRF51_EVENT_CLEAR  0x00

static uint8_t const bitwidths[] = {16, 8, 24, 32};
#define BWM(x) ((1UL << bitwidths[x]) - 1)

static inline uint64_t ns_to_ticks(Nrf51TimerState *s, uint64_t ns)
{
    uint64_t t = NANOSECONDS_PER_SECOND * (1 << s->prescaler);
    return muldiv64(ns, TIMER_CLK, t);
}

static inline uint64_t ticks_to_ns(Nrf51TimerState *s, uint64_t ticks)
{
    ticks *= (1 << s->prescaler);
    return muldiv64(ticks, NANOSECONDS_PER_SECOND, TIMER_CLK);
}

static void update_irq(Nrf51TimerState *s)
{
    bool flag = false;
    size_t i;

    for (i = 0; i < NRF51_TIMER_REG_COUNT; i++) {
        flag |= s->events_compare[i] && extract32(s->inten, 16 + i, 1);
    }
    qemu_set_irq(s->irq, flag);
}

static void update_events(Nrf51TimerState *s, uint64_t now)
{
    uint64_t strobe;
    uint64_t tick;
    uint64_t cc;
    size_t i;
    bool occured;

    strobe = ns_to_ticks(s, now - s->last_visited);
    tick = ns_to_ticks(s, s->last_visited - s->time_offset) & BWM(s->bitmode);

    for (i = 0; i < NRF51_TIMER_REG_COUNT; i++) {
        cc = s->cc[i];

        if (tick < cc) {
            occured = (cc - tick) <= strobe;
        } else {
            occured = ((cc + (1UL << bitwidths[s->bitmode])) - tick) <= strobe;
        }

        s->events_compare[i] |= occured;
    }

    s->last_visited = now;
}

static int cmpfunc(const void *a, const void *b)
{
   return *(uint32_t *)a - *(uint32_t *)b;
}

static uint64_t get_next_timeout(Nrf51TimerState *s, uint64_t now)
{
    uint64_t r;
    size_t idx;

    uint64_t tick = (ns_to_ticks(s, now - s->time_offset)) & BWM(s->bitmode);
    int8_t next = -1;

    for (idx = 0; idx < NRF51_TIMER_REG_COUNT; idx++) {
        if (s->cc_sorted[idx] > tick) {
            next = idx;
            break;
        }
    }

    if (next == -1) {
        r = s->cc_sorted[0] + (1UL << bitwidths[s->bitmode]);
    } else {
        r = s->cc_sorted[next];
    }

    return now + ticks_to_ns(s, r - tick);
}

static void update_internal_state(Nrf51TimerState *s, uint64_t now)
{
    if (s->runstate == NRF51_TIMER_RUNNING) {
        timer_mod(&s->timer, get_next_timeout(s, now));
    } else {
        timer_del(&s->timer);
    }

    update_irq(s);
}

static void timer_expire(void *opaque)
{
    Nrf51TimerState *s = NRF51_TIMER(opaque);
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    update_events(s, now);
    update_internal_state(s, now);
}

static uint64_t nrf51_timer_read(void *opaque, hwaddr offset, unsigned int size)
{
    Nrf51TimerState *s = NRF51_TIMER(opaque);
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

static inline void update_cc_sorted(Nrf51TimerState *s)
{
    memcpy(s->cc_sorted, s->cc, sizeof(s->cc_sorted));
    qsort(s->cc_sorted, NRF51_TIMER_REG_COUNT, sizeof(uint32_t), cmpfunc);
}

static void nrf51_timer_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned int size)
{
    Nrf51TimerState *s = NRF51_TIMER(opaque);
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    size_t idx;

    trace_nrf51_timer_write(offset, value, size);

    switch (offset) {
    case NRF51_TIMER_TASK_START:
        if (value == NRF51_TRIGGER_TASK) {
            s->runstate = NRF51_TIMER_RUNNING;
        }
        break;
    case NRF51_TIMER_TASK_STOP:
    case NRF51_TIMER_TASK_SHUTDOWN:
        if (value == NRF51_TRIGGER_TASK) {
            s->runstate = NRF51_TIMER_STOPPED;
        }
        break;
    case NRF51_TIMER_TASK_COUNT:
        if (value == NRF51_TRIGGER_TASK) {
            qemu_log_mask(LOG_UNIMP, "COUNTER mode not implemented\n");
        }
        break;
    case NRF51_TIMER_TASK_CLEAR:
        if (value == NRF51_TRIGGER_TASK) {
            s->time_offset = now;
            s->last_visited = now;
        }
        break;
    case NRF51_TIMER_TASK_CAPTURE_0 ... NRF51_TIMER_TASK_CAPTURE_3:
        if (value == NRF51_TRIGGER_TASK) {
            idx = (offset - NRF51_TIMER_TASK_CAPTURE_0) / 4;
            s->cc[idx] = ns_to_ticks(s, now - s->time_offset) & BWM(s->bitmode);
            update_cc_sorted(s);
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
        if (s->mode != NRF51_TIMER_TIMER) {
            qemu_log_mask(LOG_UNIMP, "COUNTER mode not implemented\n");
            return;
        }
        s->mode = value;
        break;
    case NRF51_TIMER_REG_BITMODE:
        if (s->mode == NRF51_TIMER_TIMER && s->runstate != NRF51_TIMER_STOPPED) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: erroneous change of BITMODE while timer is running\n",
                __func__);
        }
        s->bitmode = value & NRF51_TIMER_REG_BITMODE_MASK;
        s->time_offset = now;
        s->last_visited = now;
        break;
    case NRF51_TIMER_REG_PRESCALER:
        if (s->mode == NRF51_TIMER_TIMER && s->runstate != NRF51_TIMER_STOPPED) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: erroneous change of PRESCALER while timer is running\n",
                __func__);
        }
        s->prescaler = value & NRF51_TIMER_REG_PRESCALER_MASK;
        s->time_offset = now;
        s->last_visited = now;
        break;
    case NRF51_TIMER_REG_CC0 ... NRF51_TIMER_REG_CC3:
        s->cc[(offset - NRF51_TIMER_REG_CC0) / 4] = value & BWM(s->bitmode);
        update_cc_sorted(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    update_internal_state(s, now);
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
    Nrf51TimerState *s = NRF51_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &rng_ops, s,
            TYPE_NRF51_TIMER, NRF51_TIMER_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, timer_expire, s);
}

static void nrf51_timer_reset(DeviceState *dev)
{
    Nrf51TimerState *s = NRF51_TIMER(dev);

    s->runstate = NRF51_TIMER_STOPPED;

    memset(s->events_compare, 0x00, sizeof(s->events_compare));
    memset(s->cc, 0x00, sizeof(s->cc));
    memset(s->cc_sorted, 0x00, sizeof(s->cc_sorted));
    s->shorts = 0x00;
    s->inten = 0x00;
    s->mode = 0x00;
    s->bitmode = 0x00;
    s->prescaler = 0x00;

    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->time_offset = now;
    s->last_visited = now;
}

static const VMStateDescription vmstate_nrf51_timer = {
    .name = TYPE_NRF51_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER(timer, Nrf51TimerState),
        VMSTATE_UINT8(runstate, Nrf51TimerState),
        VMSTATE_UINT64(time_offset, Nrf51TimerState),
        VMSTATE_UINT64(last_visited, Nrf51TimerState),
        VMSTATE_UINT8_ARRAY(events_compare, Nrf51TimerState,
                            NRF51_TIMER_REG_COUNT),
        VMSTATE_UINT32_ARRAY(cc, Nrf51TimerState, NRF51_TIMER_REG_COUNT),
        VMSTATE_UINT32_ARRAY(cc_sorted, Nrf51TimerState, NRF51_TIMER_REG_COUNT),
        VMSTATE_UINT32(shorts, Nrf51TimerState),
        VMSTATE_UINT32(inten, Nrf51TimerState),
        VMSTATE_UINT32(mode, Nrf51TimerState),
        VMSTATE_UINT32(bitmode, Nrf51TimerState),
        VMSTATE_UINT32(prescaler, Nrf51TimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property nrf51_timer_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void nrf51_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = nrf51_timer_properties;
    dc->reset = nrf51_timer_reset;
    dc->vmsd = &vmstate_nrf51_timer;
}

static const TypeInfo nrf51_timer_info = {
    .name = TYPE_NRF51_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Nrf51TimerState),
    .instance_init = nrf51_timer_init,
    .class_init = nrf51_timer_class_init
};

static void nrf51_timer_register_types(void)
{
    type_register_static(&nrf51_timer_info);
}

type_init(nrf51_timer_register_types)
