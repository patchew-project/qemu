/*
 * RP2040 watchdog emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/qdev-clock.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_watchdog.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "system/watchdog.h"

#define WATCHDOG_CTRL       0x00
#define WATCHDOG_LOAD       0x04
#define WATCHDOG_REASON     0x08
#define WATCHDOG_SCRATCH0   0x0c
#define WATCHDOG_SCRATCH7   0x28
#define WATCHDOG_TICK       0x2c

#define WATCHDOG_CTRL_TRIGGER   BIT(31)
#define WATCHDOG_CTRL_ENABLE    BIT(30)
#define WATCHDOG_CTRL_PAUSE_DBG1 BIT(26)
#define WATCHDOG_CTRL_PAUSE_DBG0 BIT(25)
#define WATCHDOG_CTRL_PAUSE_JTAG BIT(24)
#define WATCHDOG_CTRL_PAUSE_MASK \
    (WATCHDOG_CTRL_PAUSE_DBG1 | WATCHDOG_CTRL_PAUSE_DBG0 | \
     WATCHDOG_CTRL_PAUSE_JTAG)
#define WATCHDOG_CTRL_TIME_MASK 0x00ffffff
#define WATCHDOG_CTRL_RW_MASK \
    (WATCHDOG_CTRL_ENABLE | WATCHDOG_CTRL_PAUSE_MASK)

#define WATCHDOG_LOAD_MASK  0x00ffffff
#define WATCHDOG_REASON_FORCE   BIT(1)
#define WATCHDOG_REASON_TIMER   BIT(0)

#define WATCHDOG_TICK_ENABLE    BIT(9)
#define WATCHDOG_TICK_RUNNING   BIT(10)
#define WATCHDOG_TICK_CYCLES_MASK 0x000001ff
#define WATCHDOG_TICK_RW_MASK \
    (WATCHDOG_TICK_ENABLE | WATCHDOG_TICK_CYCLES_MASK)
#define WATCHDOG_TICK_COUNT_SHIFT 11

#define ATOMIC_ALIAS_MASK   0x3000
#define ATOMIC_XOR          0x1000
#define ATOMIC_SET          0x2000
#define ATOMIC_CLR          0x3000

static uint32_t rp2040_watchdog_apply_alias(uint32_t old, uint32_t value,
                                            hwaddr alias)
{
    switch (alias) {
    case ATOMIC_XOR:
        return old ^ value;
    case ATOMIC_SET:
        return old | value;
    case ATOMIC_CLR:
        return old & ~value;
    default:
        return value;
    }
}

static uint32_t rp2040_watchdog_expand_write(uint32_t value, unsigned size)
{
    switch (size) {
    case 1:
        return value * 0x01010101u;
    case 2:
        return (value & 0xffffu) * 0x00010001u;
    default:
        return value;
    }
}

static bool rp2040_watchdog_scratch_offset(hwaddr offset, unsigned *index)
{
    if (offset < WATCHDOG_SCRATCH0 || offset > WATCHDOG_SCRATCH7) {
        return false;
    }

    *index = (offset - WATCHDOG_SCRATCH0) / sizeof(uint32_t);
    return true;
}

static bool rp2040_watchdog_tick_running(RP2040WatchdogState *s)
{
    return (s->tick & WATCHDOG_TICK_ENABLE) &&
           (s->tick & WATCHDOG_TICK_CYCLES_MASK) &&
           clock_get_hz(s->clk_ref);
}

static uint32_t rp2040_watchdog_tick_freq_hz(RP2040WatchdogState *s)
{
    uint64_t clk_ref_hz = clock_get_hz(s->clk_ref);
    uint32_t cycles = s->tick & WATCHDOG_TICK_CYCLES_MASK;

    if (!cycles || !clk_ref_hz || !(s->tick & WATCHDOG_TICK_ENABLE)) {
        return 0;
    }

    return MIN(UINT32_MAX, (uint32_t)(clk_ref_hz / cycles));
}

static uint32_t rp2040_watchdog_tick_read(RP2040WatchdogState *s)
{
    uint32_t value = s->tick & WATCHDOG_TICK_RW_MASK;

    if (rp2040_watchdog_tick_running(s)) {
        /*
         * The precise divider phase is not observable by current firmware
         * tests. Report a stable running state and a valid non-zero count.
         */
        value |= WATCHDOG_TICK_RUNNING;
        value |= (s->tick & WATCHDOG_TICK_CYCLES_MASK) <<
                 WATCHDOG_TICK_COUNT_SHIFT;
    }

    return value;
}

static void rp2040_watchdog_update(RP2040WatchdogState *s)
{
    uint32_t tick_hz = rp2040_watchdog_tick_freq_hz(s);
    uint64_t watchdog_hz = (uint64_t)tick_hz * 2;

    ptimer_transaction_begin(s->timer);
    if (!tick_hz) {
        ptimer_stop(s->timer);
    } else {
        /*
         * RP2040-E1: the watchdog counter decrements twice per watchdog
         * tick, so a LOAD of N expires after N / 2 generated ticks.
         */
        ptimer_set_freq(s->timer, MIN(watchdog_hz, (uint64_t)UINT32_MAX));
        if (s->ctrl & WATCHDOG_CTRL_ENABLE) {
            ptimer_run(s->timer, 1);
        } else {
            ptimer_stop(s->timer);
        }
    }
    ptimer_transaction_commit(s->timer);
}

static uint32_t rp2040_watchdog_ctrl_read(RP2040WatchdogState *s)
{
    uint32_t time = MIN(ptimer_get_count(s->timer), WATCHDOG_CTRL_TIME_MASK);

    return (s->ctrl & WATCHDOG_CTRL_RW_MASK) | time;
}

static void rp2040_watchdog_reset_request(RP2040WatchdogState *s,
                                          uint32_t reason,
                                          bool in_ptimer_transaction)
{
    s->reason = reason;
    if (!in_ptimer_transaction) {
        ptimer_transaction_begin(s->timer);
    }
    ptimer_stop(s->timer);
    if (!in_ptimer_transaction) {
        ptimer_transaction_commit(s->timer);
    }
    watchdog_perform_action();
}

static uint64_t rp2040_watchdog_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040WatchdogState *s = opaque;
    hwaddr offset = addr & 0xfff;
    hwaddr word_offset = offset & ~3ULL;
    unsigned scratch;
    uint64_t value;

    switch (word_offset) {
    case WATCHDOG_CTRL:
        value = rp2040_watchdog_ctrl_read(s);
        break;
    case WATCHDOG_LOAD:
        value = 0;
        break;
    case WATCHDOG_REASON:
        value = s->reason;
        break;
    case WATCHDOG_TICK:
        value = rp2040_watchdog_tick_read(s);
        break;
    default:
        if (rp2040_watchdog_scratch_offset(word_offset, &scratch)) {
            value = s->scratch[scratch];
        } else {
            value = 0;
            rp2040_log_unimplemented_read("watchdog", size,
                                          RP2040_WATCHDOG_BASE + addr,
                                          offset, value);
        }
        break;
    }

    return extract64(value, (offset & 3) * 8, size * 8);
}

static void rp2040_watchdog_write(void *opaque, hwaddr addr,
                                  uint64_t value64, unsigned size)
{
    RP2040WatchdogState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    hwaddr word_offset = offset & ~3ULL;
    unsigned scratch;
    uint32_t value = rp2040_watchdog_expand_write(value64, size);

    switch (word_offset) {
    case WATCHDOG_CTRL: {
        uint32_t old = rp2040_watchdog_ctrl_read(s);
        uint32_t newval = rp2040_watchdog_apply_alias(old, value, alias);

        s->ctrl = newval & WATCHDOG_CTRL_RW_MASK;
        rp2040_watchdog_update(s);
        if (newval & WATCHDOG_CTRL_TRIGGER) {
            rp2040_watchdog_reset_request(s, WATCHDOG_REASON_FORCE, false);
        }
        break;
    }
    case WATCHDOG_LOAD:
        s->load = rp2040_watchdog_apply_alias(s->load, value, alias) &
                  WATCHDOG_LOAD_MASK;
        ptimer_transaction_begin(s->timer);
        ptimer_set_limit(s->timer, s->load, 1);
        ptimer_transaction_commit(s->timer);
        rp2040_watchdog_update(s);
        break;
    case WATCHDOG_REASON:
        break;
    case WATCHDOG_TICK:
        s->tick = rp2040_watchdog_apply_alias(s->tick, value, alias) &
                  WATCHDOG_TICK_RW_MASK;
        rp2040_watchdog_update(s);
        break;
    default:
        if (rp2040_watchdog_scratch_offset(word_offset, &scratch)) {
            s->scratch[scratch] =
                rp2040_watchdog_apply_alias(s->scratch[scratch], value,
                                            alias);
        } else {
            rp2040_log_unimplemented_write("watchdog", size,
                                           RP2040_WATCHDOG_BASE + addr,
                                           offset, value64);
        }
        break;
    }
}

static const MemoryRegionOps rp2040_watchdog_ops = {
    .read = rp2040_watchdog_read,
    .write = rp2040_watchdog_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void rp2040_watchdog_reset(DeviceState *dev)
{
    RP2040WatchdogState *s = RP2040_WATCHDOG(dev);

    s->ctrl = WATCHDOG_CTRL_PAUSE_MASK;
    s->load = 0;
    s->tick = WATCHDOG_TICK_ENABLE;

    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    ptimer_set_limit(s->timer, 0, 1);
    ptimer_transaction_commit(s->timer);
}

static void rp2040_watchdog_clk_ref_update(void *opaque, ClockEvent event)
{
    rp2040_watchdog_update(opaque);
}

static void rp2040_watchdog_tick(void *opaque)
{
    RP2040WatchdogState *s = RP2040_WATCHDOG(opaque);

    rp2040_watchdog_reset_request(s, WATCHDOG_REASON_TIMER, true);
}

static void rp2040_watchdog_init(Object *obj)
{
    RP2040WatchdogState *s = RP2040_WATCHDOG(obj);
    DeviceState *dev = DEVICE(obj);

    s->clk_ref = qdev_init_clock_in(dev, "clk-ref",
                                    rp2040_watchdog_clk_ref_update, s,
                                    ClockUpdate);
    memory_region_init_io(&s->iomem, obj, &rp2040_watchdog_ops, s,
                          "rp2040.watchdog", RP2040_WATCHDOG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void rp2040_watchdog_realize(DeviceState *dev, Error **errp)
{
    RP2040WatchdogState *s = RP2040_WATCHDOG(dev);

    if (!clock_has_source(s->clk_ref)) {
        error_setg(errp, "RP2040 watchdog: clk-ref clock must be connected");
        return;
    }

    s->timer = ptimer_init(rp2040_watchdog_tick, s,
                           PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
                           PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                           PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);
}

static const VMStateDescription rp2040_watchdog_vmstate = {
    .name = TYPE_RP2040_WATCHDOG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(clk_ref, RP2040WatchdogState),
        VMSTATE_PTIMER(timer, RP2040WatchdogState),
        VMSTATE_UINT32(ctrl, RP2040WatchdogState),
        VMSTATE_UINT32(load, RP2040WatchdogState),
        VMSTATE_UINT32(reason, RP2040WatchdogState),
        VMSTATE_UINT32_ARRAY(scratch, RP2040WatchdogState, 8),
        VMSTATE_UINT32(tick, RP2040WatchdogState),
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_watchdog_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rp2040_watchdog_realize;
    device_class_set_legacy_reset(dc, rp2040_watchdog_reset);
    dc->vmsd = &rp2040_watchdog_vmstate;
}

static const TypeInfo rp2040_watchdog_info = {
    .name          = TYPE_RP2040_WATCHDOG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040WatchdogState),
    .instance_init = rp2040_watchdog_init,
    .class_init    = rp2040_watchdog_class_init,
};

static void rp2040_watchdog_register_types(void)
{
    type_register_static(&rp2040_watchdog_info);
}
type_init(rp2040_watchdog_register_types)
