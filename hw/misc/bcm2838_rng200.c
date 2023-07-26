/*
 * BCM2838 Random Number Generator emulation
 *
 * Copyright (C) 2022 Sergey Pushkarev <sergey.pushkarev@auriga.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qom/object_interfaces.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/misc/bcm2838_rng200.h"
#include "trace.h"

#define RNG_CTRL_OFFSET                      0x00
#define RNG_SOFT_RESET                       0x01
#define RNG_SOFT_RESET_OFFSET                0x04
#define RBG_SOFT_RESET_OFFSET                0x08
#define RNG_TOTAL_BIT_COUNT_OFFSET           0x0C
#define RNG_TOTAL_BIT_COUNT_THRESHOLD_OFFSET 0x10
#define RNG_INT_STATUS_OFFSET                0x18
#define RNG_INT_ENABLE_OFFSET                0x1C
#define RNG_FIFO_DATA_OFFSET                 0x20
#define RNG_FIFO_COUNT_OFFSET                0x24

#define RNG_WARM_UP_PERIOD_ELAPSED           17

#define BCM2838_RNG200_PTIMER_POLICY         (PTIMER_POLICY_CONTINUOUS_TRIGGER)

static void bcm2838_rng200_update_irq(BCM2838Rng200State *state)
{
    qemu_set_irq(state->irq, !!(state->rng_int_enable.value
                              & state->rng_int_status.value));
}

static void bcm2838_rng200_update_rbg_period(void *opaque, ClockEvent event)
{
    BCM2838Rng200State *s = (BCM2838Rng200State *)opaque;

    ptimer_transaction_begin(s->ptimer);
    ptimer_set_period_from_clock(s->ptimer, s->clock, s->rng_fifo_cap * 8);
    ptimer_transaction_commit(s->ptimer);
}

static void bcm2838_rng200_update_fifo(void *opaque, const void *buf,
                                       size_t size)
{
    BCM2838Rng200State *state = (BCM2838Rng200State *)opaque;
    Fifo8 *fifo = &state->fifo;
    size_t num = MIN(size, fifo8_num_free(fifo));
    uint32_t num_bits = num * 8;
    uint32_t bit_threshold_left = 0;

    state->rng_total_bit_count += num_bits;
    if (state->rng_bit_count_threshold > state->rng_total_bit_count) {
        bit_threshold_left =
            state->rng_bit_count_threshold - state->rng_total_bit_count;
    } else {
        bit_threshold_left = 0;
    }

    if (bit_threshold_left < num_bits) {
        num_bits -= bit_threshold_left;
    } else {
        num_bits = 0;
    }

    num = num_bits / 8;
    if ((num == 0) && (num_bits > 0)) {
        num = 1;
    }
    if (!state->use_timer || (num > 0)) {
        fifo8_push_all(fifo, buf, num);

        if (!state->use_timer
                || (fifo8_num_used(fifo) > state->rng_fifo_count.thld)) {
            state->rng_int_status.total_bits_count_irq = 1;
        }
    }

    state->rng_fifo_count.count = fifo8_num_used(fifo) >> 2;
    bcm2838_rng200_update_irq(state);
    trace_bcm2838_rng200_update_fifo(num, fifo8_num_used(fifo));
}

static void bcm2838_rng200_fill_fifo(BCM2838Rng200State *state)
{
    rng_backend_request_entropy(state->rng,
                                fifo8_num_free(&state->fifo),
                                bcm2838_rng200_update_fifo, state);
}

static void bcm2838_rng200_disable_rbg(BCM2838Rng200State *state)
{
    if (state->use_timer) {
        ptimer_transaction_begin(state->ptimer);
        ptimer_stop(state->ptimer);
        ptimer_transaction_commit(state->ptimer);
    }

    trace_bcm2838_rng200_disable_rbg();
}

static void bcm2838_rng200_enable_rbg(BCM2838Rng200State *state)
{
    state->rng_total_bit_count = RNG_WARM_UP_PERIOD_ELAPSED;

    if (state->use_timer) {
        uint32_t div = state->rng_ctrl.div + 1;

        ptimer_transaction_begin(state->ptimer);
        ptimer_set_limit(state->ptimer, div, 1);
        ptimer_set_count(state->ptimer, div);
        ptimer_run(state->ptimer, 0);
        ptimer_transaction_commit(state->ptimer);
    } else {
        bcm2838_rng200_fill_fifo(state);
    }

    trace_bcm2838_rng200_enable_rbg();
}

static void bcm2838_rng200_ptimer_cb(void *arg)
{
    BCM2838Rng200State *state = (BCM2838Rng200State *)arg;
    Fifo8 *fifo = &state->fifo;
    size_t size = fifo8_num_free(fifo);

    assert(state->rng_ctrl.rbg_enable);

    if (size > 0) {
        rng_backend_request_entropy(state->rng, size,
                                    bcm2838_rng200_update_fifo, state);
    } else {
        ptimer_stop(state->ptimer);
        trace_bcm2838_rng200_fifo_full();
    }
}

static void bcm2838_rng200_rng_reset(BCM2838Rng200State *state)
{
    state->rng_ctrl.value = 0;
    state->rng_total_bit_count = 0;
    state->rng_bit_count_threshold = 0;
    state->rng_fifo_count.value = 0;
    state->rng_int_status.value = 0;
    state->rng_int_status.startup_transition_met_irq = 1;
    state->rng_int_enable.value = 0;
    fifo8_reset(&state->fifo);

    trace_bcm2838_rng200_rng_soft_reset();
}

static void bcm2838_rng200_rbg_reset(BCM2838Rng200State *state)
{
    trace_bcm2838_rng200_rbg_soft_reset();
}

static uint32_t bcm2838_rng200_read_fifo_data(BCM2838Rng200State *state)
{
    Fifo8 *fifo = &state->fifo;
    const uint8_t *buf;
    uint32_t ret = 0;
    uint32_t num = 0;
    uint32_t max = MIN(fifo8_num_used(fifo), sizeof(ret));

    if (max > 0) {
        buf = fifo8_pop_buf(fifo, max, &num);
        if ((buf != NULL) && (num > 0)) {
            memcpy(&ret, buf, num);

            if (state->rng_ctrl.rbg_enable && state->use_timer) {
                ptimer_transaction_begin(state->ptimer);
                ptimer_run(state->ptimer, 0);
                ptimer_transaction_commit(state->ptimer);
            }
        }
    } else {
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "bcm2838_rng200_read_fifo_data: FIFO is empty\n"
        );
    }

    state->rng_fifo_count.count = fifo8_num_used(fifo) >> 2;

    if (!state->use_timer) {
        bcm2838_rng200_fill_fifo(state);
    }

    return ret;
}

static void bcm2838_rng200_ctrl_write(BCM2838Rng200State *s, uint64_t value)
{
    bool rng_enable = s->rng_ctrl.rbg_enable;

    s->rng_ctrl.value = value;
    if (!s->rng_ctrl.rbg_enable && rng_enable) {
        bcm2838_rng200_disable_rbg(s);
    } else if (s->rng_ctrl.rbg_enable && !rng_enable) {
        bcm2838_rng200_enable_rbg(s);
    }
}

static uint64_t bcm2838_rng200_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    BCM2838Rng200State *s = (BCM2838Rng200State *)opaque;
    uint32_t res = 0;

    switch (offset) {
    case RNG_CTRL_OFFSET:
        res = s->rng_ctrl.value;
        break;
    case RNG_SOFT_RESET_OFFSET:
    case RBG_SOFT_RESET_OFFSET:
        break;
    case RNG_INT_STATUS_OFFSET:
        res = s->rng_int_status.value;
        break;
    case RNG_INT_ENABLE_OFFSET:
        res = s->rng_int_enable.value;
        break;
    case RNG_FIFO_DATA_OFFSET:
        res = bcm2838_rng200_read_fifo_data(s);
        break;
    case RNG_FIFO_COUNT_OFFSET:
        res = s->rng_fifo_count.value;
        break;
    case RNG_TOTAL_BIT_COUNT_OFFSET:
        res = s->rng_total_bit_count;
        break;
    case RNG_TOTAL_BIT_COUNT_THRESHOLD_OFFSET:
        res = s->rng_bit_count_threshold;
        break;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "bcm2838_rng200_read: Bad offset 0x%" HWADDR_PRIx "\n",
            offset
        );
        res = 0;
        break;
    }

    trace_bcm2838_rng200_read((void *)offset, size, res);
    return res;
}

static void bcm2838_rng200_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    BCM2838Rng200State *s = (BCM2838Rng200State *)opaque;

    trace_bcm2838_rng200_write((void *)offset, value, size);

    switch (offset) {
    case RNG_CTRL_OFFSET:
        bcm2838_rng200_ctrl_write(s, value);
        break;
    case RNG_SOFT_RESET_OFFSET:
        if (value & RNG_SOFT_RESET) {
            bcm2838_rng200_rng_reset(s);
        }
        break;
    case RBG_SOFT_RESET_OFFSET:
        if (value & RNG_SOFT_RESET) {
            bcm2838_rng200_rbg_reset(s);
        }
        break;
    case RNG_INT_STATUS_OFFSET:
        s->rng_int_status.value &= ~value;
        bcm2838_rng200_update_irq(s);
        break;
    case RNG_INT_ENABLE_OFFSET:
        s->rng_int_enable.value = value;
        bcm2838_rng200_update_irq(s);
        break;
    case RNG_FIFO_COUNT_OFFSET:
        {
            BCM2838Rng200FifoCount tmp = {.value = value};
            s->rng_fifo_count.thld = tmp.thld;
        }
        break;
    case RNG_TOTAL_BIT_COUNT_THRESHOLD_OFFSET:
        s->rng_bit_count_threshold = value;
        if (s->use_timer) {
            s->rng_total_bit_count = 0;
        } else {
            s->rng_total_bit_count = value + 1;
        }
        break;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "bcm2838_rng200_write: Bad offset 0x%" HWADDR_PRIx "\n",
            offset
        );
        break;
    }
}

static const MemoryRegionOps bcm2838_rng200_ops = {
    .read = bcm2838_rng200_read,
    .write = bcm2838_rng200_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .max_access_size = 4,
        .min_access_size = 4,
    },
    .valid = {
        .max_access_size = 4,
        .min_access_size = 4
    },
};

static void bcm2838_rng200_realize(DeviceState *dev, Error **errp)
{
    BCM2838Rng200State *s = BCM2838_RNG200(dev);

    if (s->use_timer) {
        s->ptimer = ptimer_init(bcm2838_rng200_ptimer_cb, s,
                                BCM2838_RNG200_PTIMER_POLICY);
        if (s->ptimer == NULL) {
            error_setg(&error_fatal, "Failed to init RBG timer");
            return;
        }
    }

    if (s->rng == NULL) {
        Object *default_backend = object_new(TYPE_RNG_BUILTIN);

        if (!user_creatable_complete(USER_CREATABLE(default_backend),
                                     errp)) {
            object_unref(default_backend);
            error_setg(errp, "Failed to create user creatable RNG backend");
            return;
        }

        object_property_add_child(OBJECT(dev), "default-backend",
                                  default_backend);
        object_unref(default_backend);

        object_property_set_link(OBJECT(dev), "rng", default_backend,
                                 errp);
    }

    if (s->use_timer && !clock_has_source(s->clock)) {
        ptimer_transaction_begin(s->ptimer);
        ptimer_set_period(s->ptimer, s->rbg_period * s->rng_fifo_cap * 8);
        ptimer_transaction_commit(s->ptimer);
    }

    fifo8_create(&s->fifo, s->rng_fifo_cap);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static void bcm2838_rng200_init(Object *obj)
{
    BCM2838Rng200State *s = BCM2838_RNG200(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->clock = qdev_init_clock_in(DEVICE(s), "rbg-clock",
                                  bcm2838_rng200_update_rbg_period, s,
                                  ClockPreUpdate);
    if (s->clock == NULL) {
        error_setg(&error_fatal, "Failed to init RBG clock");
        return;
    }

    memory_region_init_io(&s->iomem, obj, &bcm2838_rng200_ops, s,
                          TYPE_BCM2838_RNG200, 0x28);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void bcm2838_rng200_reset(DeviceState *dev)
{
    BCM2838Rng200State *s = BCM2838_RNG200(dev);

    bcm2838_rng200_rbg_reset(s);
    bcm2838_rng200_rng_reset(s);
}

static Property bcm2838_rng200_properties[] = {
    DEFINE_PROP_UINT32("rbg-period", BCM2838Rng200State, rbg_period, 250),
    DEFINE_PROP_UINT32("rng-fifo-cap", BCM2838Rng200State, rng_fifo_cap, 128),
    DEFINE_PROP_LINK("rng", BCM2838Rng200State, rng,
                     TYPE_RNG_BACKEND, RngBackend *),
    DEFINE_PROP_BOOL("use-timer", BCM2838Rng200State, use_timer, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void bcm2838_rng200_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2838_rng200_realize;
    dc->reset = bcm2838_rng200_reset;
    device_class_set_props(dc, bcm2838_rng200_properties);
}

static const TypeInfo bcm2838_rng200_info = {
    .name          = TYPE_BCM2838_RNG200,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2838Rng200State),
    .class_init    = bcm2838_rng200_class_init,
    .instance_init = bcm2838_rng200_init,
};

static void bcm2838_rng200_register_types(void)
{
    type_register_static(&bcm2838_rng200_info);
}

type_init(bcm2838_rng200_register_types)
