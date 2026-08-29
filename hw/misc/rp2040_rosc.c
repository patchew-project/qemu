/*
 * RP2040 ring oscillator emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_rosc.h"
#include "migration/vmstate.h"
#include "qemu/guest-random.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"

#define ROSC_NOMINAL_HZ        6500000

#define ROSC_CTRL              0x00
#define ROSC_FREQA             0x04
#define ROSC_FREQB             0x08
#define ROSC_DORMANT           0x0c
#define ROSC_DIV               0x10
#define ROSC_PHASE             0x14
#define ROSC_STATUS            0x18
#define ROSC_RANDOMBIT         0x1c
#define ROSC_COUNT             0x20

#define ROSC_ENABLE_DISABLE    0xd1e
#define ROSC_ENABLE_ENABLE     0xfab
#define ROSC_FREQ_RANGE_LOW    0xfa4
#define ROSC_FREQ_RANGE_MEDIUM 0xfa5
#define ROSC_FREQ_RANGE_HIGH   0xfa7
#define ROSC_FREQ_RANGE_TOOHIGH 0xfa6
#define ROSC_DORMANT_VALUE     0x636f6d61
#define ROSC_WAKE_VALUE        0x77616b65
#define ROSC_FREQ_PASSWD       0x9696
#define ROSC_DIV_PASS          0xaa0
#define ROSC_PHASE_PASSWD      0xaa

#define ROSC_STATUS_STABLE     BIT(31)
#define ROSC_STATUS_BADWRITE   BIT(24)
#define ROSC_STATUS_DIV_RUNNING BIT(16)
#define ROSC_STATUS_ENABLED    BIT(12)

#define ATOMIC_ALIAS_MASK      0x3000
#define ATOMIC_XOR             0x1000
#define ATOMIC_SET             0x2000
#define ATOMIC_CLR             0x3000

static uint32_t rp2040_rosc_apply_alias(uint32_t old, uint32_t value,
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

static bool rp2040_rosc_valid_range(uint32_t range)
{
    return range == ROSC_FREQ_RANGE_LOW || range == ROSC_FREQ_RANGE_MEDIUM ||
           range == ROSC_FREQ_RANGE_HIGH || range == ROSC_FREQ_RANGE_TOOHIGH;
}

static bool rp2040_rosc_enabled(RP2040RoscState *s)
{
    return extract32(s->ctrl, 12, 12) != ROSC_ENABLE_DISABLE;
}

static bool rp2040_rosc_running(RP2040RoscState *s)
{
    return rp2040_rosc_enabled(s) && s->dormant == ROSC_WAKE_VALUE;
}

static uint32_t rp2040_rosc_divisor(RP2040RoscState *s)
{
    uint32_t encoded = s->div & 0xfff;
    uint32_t div;

    if (encoded < ROSC_DIV_PASS) {
        return 31;
    }

    div = encoded - ROSC_DIV_PASS;
    if (div == 0) {
        return 32;
    }
    if (div > 31) {
        return 31;
    }
    return div;
}

static uint32_t rp2040_rosc_hz(RP2040RoscState *s)
{
    if (!rp2040_rosc_running(s)) {
        return 0;
    }

    return ROSC_NOMINAL_HZ / rp2040_rosc_divisor(s);
}

static void rp2040_rosc_update_clock(RP2040RoscState *s)
{
    clock_update_hz(s->clk, rp2040_rosc_hz(s));
}

static uint32_t rp2040_rosc_count_read(RP2040RoscState *s)
{
    uint64_t elapsed_ns;
    uint64_t elapsed_ticks;

    if (!s->count || !rp2040_rosc_running(s)) {
        return s->count;
    }

    elapsed_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->count_start_ns;
    elapsed_ticks = elapsed_ns * ROSC_NOMINAL_HZ / NANOSECONDS_PER_SECOND;
    if (elapsed_ticks >= s->count) {
        s->count = 0;
    } else {
        s->count -= elapsed_ticks;
        s->count_start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }

    return s->count;
}

static uint32_t rp2040_rosc_status(RP2040RoscState *s)
{
    uint32_t value = 0;

    if (rp2040_rosc_enabled(s)) {
        value |= ROSC_STATUS_ENABLED;
        if (s->dormant == ROSC_WAKE_VALUE) {
            value |= ROSC_STATUS_STABLE | ROSC_STATUS_DIV_RUNNING;
        }
    }
    if (s->badwrite) {
        value |= ROSC_STATUS_BADWRITE;
    }

    return value;
}

static uint32_t rp2040_rosc_random_bit(RP2040RoscState *s)
{
    uint64_t x;
    uint32_t bit;

    if (!rp2040_rosc_running(s)) {
        return 0;
    }

    if (!s->random_seed_set) {
        if (!s->random_pool_bits) {
            qemu_guest_getrandom_nofail(&s->random_pool,
                                        sizeof(s->random_pool));
            s->random_pool_bits = 64;
        }

        bit = s->random_pool & 1;
        s->random_pool >>= 1;
        s->random_pool_bits--;
        return bit;
    }

    x = s->random_prng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    s->random_prng_state = x;
    return (x * 0x2545f4914f6cdd1dull) >> 63;
}

static uint64_t rp2040_rosc_read(void *opaque, hwaddr addr, unsigned size)
{
    RP2040RoscState *s = opaque;
    hwaddr offset = addr & 0xfff;
    uint64_t value;

    switch (offset) {
    case ROSC_CTRL:
        value = s->ctrl;
        break;
    case ROSC_FREQA:
        value = s->freqa;
        break;
    case ROSC_FREQB:
        value = s->freqb;
        break;
    case ROSC_DORMANT:
        value = s->dormant;
        break;
    case ROSC_DIV:
        value = s->div;
        break;
    case ROSC_PHASE:
        value = s->phase;
        break;
    case ROSC_STATUS:
        value = rp2040_rosc_status(s);
        break;
    case ROSC_RANDOMBIT:
        value = rp2040_rosc_random_bit(s);
        break;
    case ROSC_COUNT:
        value = rp2040_rosc_count_read(s);
        break;
    default:
        value = 0;
        rp2040_log_unimplemented_read("rosc", size,
                                      RP2040_ROSC_BASE + addr, offset,
                                      value);
        break;
    }

    return value;
}

static void rp2040_rosc_write_ctrl(RP2040RoscState *s, uint32_t value)
{
    uint32_t enable = extract32(value, 12, 12);
    uint32_t range = extract32(value, 0, 12);

    if (enable != ROSC_ENABLE_DISABLE && enable != ROSC_ENABLE_ENABLE) {
        s->badwrite = true;
        value = deposit32(value, 12, 12, ROSC_ENABLE_ENABLE);
    }
    if (!rp2040_rosc_valid_range(range)) {
        s->badwrite = true;
        value = deposit32(value, 0, 12, ROSC_FREQ_RANGE_LOW);
    }

    s->ctrl = value & 0x00ffffff;
    rp2040_rosc_update_clock(s);
}

static void rp2040_rosc_write_freq(RP2040RoscState *s, uint32_t *reg,
                                   uint32_t value)
{
    if (extract32(value, 16, 16) != ROSC_FREQ_PASSWD) {
        s->badwrite = true;
        *reg = 0;
        return;
    }

    *reg = value & 0x96967777;
}

static void rp2040_rosc_write_div(RP2040RoscState *s, uint32_t value)
{
    if ((value & 0xfff) < ROSC_DIV_PASS) {
        s->badwrite = true;
        value = ROSC_DIV_PASS + 31;
    }

    s->div = value & 0xfff;
    rp2040_rosc_update_clock(s);
}

static void rp2040_rosc_write_phase(RP2040RoscState *s, uint32_t value)
{
    if (extract32(value, 4, 8) != ROSC_PHASE_PASSWD) {
        s->badwrite = true;
        value = 0;
    }

    s->phase = value & 0x00000fff;
}

static void rp2040_rosc_write(void *opaque, hwaddr addr,
                              uint64_t value64, unsigned size)
{
    RP2040RoscState *s = opaque;
    hwaddr alias = addr & ATOMIC_ALIAS_MASK;
    hwaddr offset = addr & 0xfff;
    uint32_t value = value64;

    switch (offset) {
    case ROSC_CTRL:
        rp2040_rosc_write_ctrl(s, rp2040_rosc_apply_alias(s->ctrl, value,
                                                          alias));
        break;
    case ROSC_FREQA:
        rp2040_rosc_write_freq(s, &s->freqa,
                               rp2040_rosc_apply_alias(s->freqa, value,
                                                       alias));
        break;
    case ROSC_FREQB:
        rp2040_rosc_write_freq(s, &s->freqb,
                               rp2040_rosc_apply_alias(s->freqb, value,
                                                       alias));
        break;
    case ROSC_DORMANT:
        s->dormant = value == ROSC_DORMANT_VALUE ? value : ROSC_WAKE_VALUE;
        if (value != ROSC_DORMANT_VALUE && value != ROSC_WAKE_VALUE) {
            s->badwrite = true;
        }
        rp2040_rosc_update_clock(s);
        break;
    case ROSC_DIV:
        rp2040_rosc_write_div(s, rp2040_rosc_apply_alias(s->div, value,
                                                         alias));
        break;
    case ROSC_PHASE:
        rp2040_rosc_write_phase(s, rp2040_rosc_apply_alias(s->phase, value,
                                                           alias));
        break;
    case ROSC_STATUS:
        if (value & ROSC_STATUS_BADWRITE) {
            s->badwrite = false;
        }
        break;
    case ROSC_COUNT:
        s->count = value & 0xff;
        s->count_start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        break;
    default:
        rp2040_log_unimplemented_write("rosc", size,
                                       RP2040_ROSC_BASE + addr, offset,
                                       value64);
        break;
    }
}

static const MemoryRegionOps rp2040_rosc_ops = {
    .read = rp2040_rosc_read,
    .write = rp2040_rosc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_rosc_reset(DeviceState *dev)
{
    RP2040RoscState *s = RP2040_ROSC(dev);

    s->ctrl = (ROSC_ENABLE_ENABLE << 12) | ROSC_FREQ_RANGE_LOW;
    s->freqa = 0;
    s->freqb = 0;
    s->dormant = ROSC_WAKE_VALUE;
    s->div = ROSC_DIV_PASS + 16;
    s->phase = 0x00000aa8;
    s->count = 0;
    s->count_start_ns = 0;
    s->random_pool = 0;
    s->random_pool_bits = 0;
    s->random_prng_state = s->random_seed;
    if (!s->random_prng_state) {
        s->random_prng_state = 0x9e3779b97f4a7c15ull;
    }
    s->badwrite = false;

    rp2040_rosc_update_clock(s);
}

static void rp2040_rosc_init(Object *obj)
{
    RP2040RoscState *s = RP2040_ROSC(obj);
    DeviceState *dev = DEVICE(obj);

    s->clk = qdev_init_clock_out(dev, "clk");
    memory_region_init_io(&s->iomem, obj, &rp2040_rosc_ops, s,
                          "rp2040.rosc", RP2040_ROSC_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_rosc_vmstate = {
    .name = TYPE_RP2040_ROSC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, RP2040RoscState),
        VMSTATE_UINT32(freqa, RP2040RoscState),
        VMSTATE_UINT32(freqb, RP2040RoscState),
        VMSTATE_UINT32(dormant, RP2040RoscState),
        VMSTATE_UINT32(div, RP2040RoscState),
        VMSTATE_UINT32(phase, RP2040RoscState),
        VMSTATE_UINT32(count, RP2040RoscState),
        VMSTATE_INT64(count_start_ns, RP2040RoscState),
        VMSTATE_UINT64(random_pool, RP2040RoscState),
        VMSTATE_UINT64(random_prng_state, RP2040RoscState),
        VMSTATE_UINT64(random_seed, RP2040RoscState),
        VMSTATE_UINT8(random_pool_bits, RP2040RoscState),
        VMSTATE_BOOL(random_seed_set, RP2040RoscState),
        VMSTATE_BOOL(badwrite, RP2040RoscState),
        VMSTATE_CLOCK(clk, RP2040RoscState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property rp2040_rosc_properties[] = {
    DEFINE_PROP_UINT64("random-seed", RP2040RoscState, random_seed, 0),
    DEFINE_PROP_BOOL("random-seed-set", RP2040RoscState, random_seed_set,
                     false),
};

static void rp2040_rosc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rp2040_rosc_reset);
    dc->vmsd = &rp2040_rosc_vmstate;
    device_class_set_props(dc, rp2040_rosc_properties);
}

static const TypeInfo rp2040_rosc_info = {
    .name          = TYPE_RP2040_ROSC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040RoscState),
    .instance_init = rp2040_rosc_init,
    .class_init    = rp2040_rosc_class_init,
};

static void rp2040_rosc_register_types(void)
{
    type_register_static(&rp2040_rosc_info);
}
type_init(rp2040_rosc_register_types)
