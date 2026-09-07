/*
 * MAXIM DS1338/DS1339 I2C RTC+NVRAM
 *
 * Copyright (c) 2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 *
 * Limitations:
 * - The DS1339 alarm registers, the alarm-enable control bits, the A1F/A2F
 *   status flags and the trickle charger are stored but never evaluated: no
 *   alarm matching is performed, no flag is ever set by hardware and no
 *   charging happens.
 * - The INT#/SQW output pin is not exposed: neither alarm interrupts nor
 *   square-wave generation are produced. No board wires this pin and the guest
 *   does not use the RTC wakealarm here.
 * - Writing the seconds register does not reset the internal countdown chain,
 *   so the sub-second phase of the clock is not modelled.
 * - The user registers are refreshed on a START and on a read that wraps the
 *   register pointer, but not on a STOP.
 * - Leap years follow the Gregorian rule, where the parts stop at 2100: they
 *   derive the leap year from the two-digit year alone.
 * - The model comes up with the clock running; real hardware comes up halted.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/rtc/ds1338.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/bcd.h"
#include "qom/object.h"
#include "system/rtc.h"
#include "trace.h"

/* Size of NVRAM including both the user-accessible area and the
 * secondary register area.
 */
#define NVRAM_SIZE 64

/* Flags definitions */
#define SECONDS_CH 0x80
#define HOURS_12   0x40
#define HOURS_PM   0x20
#define CTRL_OSF   0x20

/* DS1338 register map */
#define DS1338_NUM_REGS    NVRAM_SIZE   /* 0x00..0x3f, then wraps */
#define DS1338_CONTROL     0x07
#define DS1338_CTRL_MASK   0xb3         /* bits 2, 3 and 6 read back as zero */
/* POR: OUT, OSF, SQWE and both rate-select bits come up set. */
#define DS1338_CTRL_RESET  0xb3

/* Sentinel register address meaning "this variant has no such register". */
#define DS1338_NO_REG      0xff

/* DS1339 register map: 0x00..0x10 only, no general-purpose NVRAM */
#define DS1339_NUM_REGS    0x11         /* 0x00..0x10, then wraps */
#define DS1339_CONTROL     0x0e
#define DS1339_CTRL_MASK   0xbf         /* bit 6 is reserved, reads back zero */
#define DS1339_CTRL_EOSC   0x80         /* EOSC: 1 stops the oscillator */
#define DS1339_CTRL_RESET  0x18         /* POR: RS2=RS1=1 (32 kHz), rest zero */
#define DS1339_MONTH_CENTURY 0x80       /* Century is bit 7 of the month reg */
#define DS1339_STATUS      0x0f
#define DS1339_STATUS_OSF  0x80         /* OSF is bit 7 of the status reg */
/* Status bits that exist (OSF | A2F | A1F); bits 6..2 are reserved, read 0. */
#define DS1339_STATUS_MASK 0x83
/* All three status flags are hardware-set and clear-only (write 0). */
#define DS1339_STATUS_W0   0x83

OBJECT_DECLARE_TYPE(DS1338State, DS1338Class, DS1338)

struct DS1338State {
    I2CSlave parent_obj;

    int64_t offset;
    uint8_t wday_offset;
    uint8_t nvram[NVRAM_SIZE];
    int32_t ptr;
    bool addr_byte;
    bool osc_stopped;      /* oscillator halted: time is frozen */
    uint8_t prev_century;  /* century index (tm_year/100) at last capture */
    bool persist_on_reset; /* keep the registers across a reset */
};

struct DS1338Class {
    I2CSlaveClass parent_class;

    uint8_t num_regs;       /* register-pointer wrap boundary */
    uint8_t ctrl_addr;      /* control register address */
    uint8_t ctrl_mask;      /* writable/read-back bits of the control reg */
    uint8_t osf_addr;       /* register holding the OSF flag */
    uint8_t osf_mask;       /* OSF bit within osf_addr */
    uint8_t status_addr;    /* status register address */
    uint8_t status_mask;    /* bits that exist; reserved bits read 0 */
    uint8_t status_w0_mask; /* flags that can only be cleared */
    uint8_t ctrl_reset;     /* control-register power-on value */
    uint8_t status_reset;   /* status-register power-on value */
    uint8_t eosc_mask;      /* oscillator-disable bit in the control reg */
    uint8_t century_mask;   /* Century bit in the month register */
    uint8_t ch_mask;        /* Clock-halt bit in the seconds reg */
};

static int ds1338_post_load(void *opaque, int version_id)
{
    DS1338State *s = opaque;
    DS1338Class *k = DS1338_GET_CLASS(s);

    /* Both accessors index nvram[] with ptr before incrementing it. */
    if (s->ptr < 0 || s->ptr >= k->num_regs) {
        return -EINVAL;
    }

    /*
     * prev_century did not exist before version 3, so an older incoming stream
     * leaves it zero.
     */
    if (version_id < 3) {
        struct tm now;

        qemu_get_timedate(&now, s->offset);
        s->prev_century = now.tm_year / 100;
    }
    return 0;
}

static const VMStateField vmstate_ds1338_fields[] = {
    VMSTATE_I2C_SLAVE(parent_obj, DS1338State),
    VMSTATE_INT64(offset, DS1338State),
    VMSTATE_UINT8_V(wday_offset, DS1338State, 2),
    VMSTATE_UINT8_ARRAY(nvram, DS1338State, NVRAM_SIZE),
    VMSTATE_INT32(ptr, DS1338State),
    VMSTATE_BOOL(addr_byte, DS1338State),
    VMSTATE_BOOL_V(osc_stopped, DS1338State, 3),
    VMSTATE_UINT8_V(prev_century, DS1338State, 3),
    VMSTATE_END_OF_LIST()
};

static const VMStateDescription vmstate_ds1338 = {
    .name = "ds1338",
    .version_id = 3,
    .minimum_version_id = 1,
    .post_load = ds1338_post_load,
    .fields = vmstate_ds1338_fields,
};

/*
 * The DS1339 reuses the DS1338 migration field list, so it migrates the full
 * NVRAM_SIZE-byte nvram[] array even though it only implements registers
 * 0x00..0x10.
 */
static const VMStateDescription vmstate_ds1339 = {
    .name = "ds1339",
    .version_id = 3,
    .minimum_version_id = 3,
    .post_load = ds1338_post_load,
    .fields = vmstate_ds1338_fields,
};

/* Reconstruct a struct tm from the BCD time registers in nvram. */
static void ds1338_time_from_regs(DS1338State *s, struct tm *now)
{
    qemu_get_timedate(now, s->offset);
    now->tm_sec = from_bcd(s->nvram[0] & 0x7f);
    now->tm_min = from_bcd(s->nvram[1] & 0x7f);
    if (s->nvram[2] & HOURS_12) {
        int tmp = from_bcd(s->nvram[2] & (HOURS_PM - 1));
        if (s->nvram[2] & HOURS_PM) {
            tmp += 12;
        }
        if (tmp % 12 == 0) {
            tmp -= 12;
        }
        now->tm_hour = tmp;
    } else {
        now->tm_hour = from_bcd(s->nvram[2] & (HOURS_12 - 1));
    }
    now->tm_mday = from_bcd(s->nvram[4] & 0x3f);
    now->tm_mon = from_bcd(s->nvram[5] & 0x1f) - 1;
    now->tm_year = s->prev_century * 100 + from_bcd(s->nvram[6]);
}

static void ds1338_resync_from_regs(DS1338State *s)
{
    struct tm now;
    int user_wday;

    ds1338_time_from_regs(s, &now);
    s->offset = qemu_timedate_diff(&now);
    qemu_get_timedate(&now, s->offset);
    user_wday = (s->nvram[3] & 7) - 1;
    s->wday_offset = (user_wday - now.tm_wday + 7) % 7;
}

static void ds1338_capture_current_time(DS1338State *s)
{
    /* Capture the current time into the secondary registers
     * which will be actually read by the data transfer operation.
     */
    DS1338Class *k = DS1338_GET_CLASS(s);
    struct tm now;
    uint8_t century;

    if (s->osc_stopped) {
        return;
    }

    qemu_get_timedate(&now, s->offset);

    century = now.tm_year / 100;
    if (k->century_mask && ((century ^ s->prev_century) & 1)) {
        s->nvram[5] ^= k->century_mask;
    }
    s->prev_century = century;

    s->nvram[0] = to_bcd(now.tm_sec);
    s->nvram[1] = to_bcd(now.tm_min);
    if (s->nvram[2] & HOURS_12) {
        int tmp = now.tm_hour;
        if (tmp % 12 == 0) {
            tmp += 12;
        }
        if (tmp <= 12) {
            s->nvram[2] = HOURS_12 | to_bcd(tmp);
        } else {
            s->nvram[2] = HOURS_12 | HOURS_PM | to_bcd(tmp - 12);
        }
    } else {
        s->nvram[2] = to_bcd(now.tm_hour);
    }
    s->nvram[3] = (now.tm_wday + s->wday_offset) % 7 + 1;
    s->nvram[4] = to_bcd(now.tm_mday);
    s->nvram[5] = to_bcd(now.tm_mon + 1) | (s->nvram[5] & k->century_mask);
    s->nvram[6] = to_bcd(now.tm_year % 100);
}

static void ds1338_inc_regptr(DS1338State *s)
{
    DS1338Class *k = DS1338_GET_CLASS(s);

    s->ptr = (s->ptr + 1) % k->num_regs;
}

static int ds1338_event(I2CSlave *i2c, enum i2c_event event)
{
    DS1338State *s = DS1338(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->addr_byte = true;
        /* fall through */
    case I2C_START_RECV:
        /*
         * In h/w the time is transferred to the user registers on any START
         * condition, so a write modifies the running time rather than
         * whatever the last read left behind.
         */
        ds1338_capture_current_time(s);
        break;
    default:
        break;
    }

    return 0;
}

static uint8_t ds1338_recv(I2CSlave *i2c)
{
    DS1338State *s = DS1338(i2c);
    uint8_t res;

    res  = s->nvram[s->ptr];

    trace_ds1338_recv(s->ptr, res);

    ds1338_inc_regptr(s);
    if (!s->ptr) {
        /* Read-only: a wrapping write must keep the bytes it just sent. */
        ds1338_capture_current_time(s);
    }
    return res;
}

/* OSF is raised by hardware whenever the oscillator stops. */
static void ds1338_raise_osf(DS1338State *s)
{
    DS1338Class *k = DS1338_GET_CLASS(s);

    if (k->osf_addr != DS1338_NO_REG) {
        s->nvram[k->osf_addr] |= k->osf_mask;
    }
}

static int ds1338_send(I2CSlave *i2c, uint8_t data)
{
    DS1338State *s = DS1338(i2c);
    DS1338Class *k = DS1338_GET_CLASS(i2c);

    trace_ds1338_send(s->ptr, data);

    if (s->addr_byte) {
        s->ptr = data % k->num_regs;
        s->addr_byte = false;
        return 0;
    }
    if (s->ptr < 7) {
        /*
         * Time register. The write lands in the register file and the clock
         * is then rebuilt from it as a whole: a transfer that programs the
         * date before the month it belongs to must not be normalised away
         * against the month still held from the previous date.
         */
        static const uint8_t valid[7] = {
            0x7f, 0x7f, 0x7f, 0x07, 0x3f, 0x1f, 0xff
        };
        uint8_t mask = valid[s->ptr];
        bool halt = s->osc_stopped;

        if (s->ptr == 0) {
            mask |= k->ch_mask;
            if (k->ch_mask) {
                halt = data & k->ch_mask;
            }
        } else if (s->ptr == 5) {
            mask |= k->century_mask;
        }

        if (halt && !s->osc_stopped) {
            /* CH set: freeze the counters before the register is stored. */
            ds1338_capture_current_time(s);
            ds1338_raise_osf(s);
        }
        s->nvram[s->ptr] = data & mask;
        s->osc_stopped = halt;
        if (!halt) {
            ds1338_resync_from_regs(s);
        }
    } else {
        if (s->ptr == k->ctrl_addr) {
            /* Control register: reserved bits read back as zero. */
            data &= k->ctrl_mask;
            if (k->eosc_mask) {
                bool now_stop = data & k->eosc_mask;
                bool was_stop = s->nvram[k->ctrl_addr] & k->eosc_mask;
                if (now_stop && !was_stop) {
                    ds1338_capture_current_time(s);
                    s->osc_stopped = true;
                    ds1338_raise_osf(s);
                } else if (!now_stop && was_stop) {
                    /* EOSC cleared: resume counting from the frozen time. */
                    ds1338_resync_from_regs(s);
                    s->osc_stopped = false;
                }
            }
        }
        if (s->ptr == k->osf_addr) {
            /*
             * Attempting to write the OSF flag to logic 1 leaves the
             * value unchanged.
             */
            data = (data & ~k->osf_mask) |
                   (data & s->nvram[s->ptr] & k->osf_mask);
        }
        if (s->ptr == k->status_addr) {
            /* Reserved bits read zero; flags are clear-only, OSF included. */
            uint8_t cur = s->nvram[s->ptr];
            data &= k->status_mask;
            data = (data & ~k->status_w0_mask) |
                   (data & cur & k->status_w0_mask);
        }
        s->nvram[s->ptr] = data;
    }
    ds1338_inc_regptr(s);
    return 0;
}

static void ds1338_power_on(DS1338State *s)
{
    DS1338Class *k = DS1338_GET_CLASS(s);
    struct tm now;

    /* The clock is running and synchronized with the host */
    s->offset = 0;
    s->wday_offset = 0;
    memset(s->nvram, 0, NVRAM_SIZE);
    s->nvram[k->ctrl_addr] = k->ctrl_reset;
    if (k->status_addr != DS1338_NO_REG) {
        s->nvram[k->status_addr] = k->status_reset;
    }
    s->osc_stopped = false;
    qemu_get_timedate(&now, 0);
    s->prev_century = now.tm_year / 100;
}

static void ds1338_realize(DeviceState *dev, Error **errp)
{
    ds1338_power_on(DS1338(dev));
}

static void ds1338_reset_hold(Object *obj, ResetType type)
{
    DS1338State *s = DS1338(obj);

    if (!s->persist_on_reset) {
        ds1338_power_on(s);
    }
    s->ptr = 0;
    s->addr_byte = false;
}

static const Property ds1338_properties[] = {
    DEFINE_PROP_BOOL("persist-on-reset", DS1338State, persist_on_reset, false),
};

static void ds1338_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DS1338Class *dsc = DS1338_CLASS(klass);

    k->event = ds1338_event;
    k->recv = ds1338_recv;
    k->send = ds1338_send;
    rc->phases.hold = ds1338_reset_hold;
    dc->realize = ds1338_realize;
    device_class_set_props(dc, ds1338_properties);
    dc->desc = "DS1338 I2C RTC with 56-byte NV RAM";
    dc->vmsd = &vmstate_ds1338;

    dsc->num_regs   = DS1338_NUM_REGS;
    dsc->ctrl_addr  = DS1338_CONTROL;
    dsc->ctrl_mask  = DS1338_CTRL_MASK;
    dsc->osf_addr   = DS1338_CONTROL;
    dsc->osf_mask   = CTRL_OSF;
    dsc->status_addr = DS1338_NO_REG;   /* the DS1338 has no status register */
    dsc->status_mask = 0;
    dsc->status_w0_mask = 0;
    dsc->ctrl_reset = DS1338_CTRL_RESET;
    dsc->status_reset = 0;
    dsc->eosc_mask = 0;
    dsc->century_mask = 0;
    dsc->ch_mask = SECONDS_CH;
}

static void ds1339_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    DS1338Class *dsc = DS1338_CLASS(klass);

    dc->desc = "DS1339 I2C serial real-time clock";
    dc->vmsd = &vmstate_ds1339;

    dsc->num_regs   = DS1339_NUM_REGS;
    dsc->ctrl_addr  = DS1339_CONTROL;
    dsc->ctrl_mask  = DS1339_CTRL_MASK;
    dsc->osf_addr   = DS1339_STATUS;   /* OSF lives in the status register */
    dsc->osf_mask   = DS1339_STATUS_OSF;
    dsc->status_addr = DS1339_STATUS;
    dsc->status_mask = DS1339_STATUS_MASK;
    dsc->status_w0_mask = DS1339_STATUS_W0;
    dsc->ctrl_reset = DS1339_CTRL_RESET;
    dsc->status_reset = DS1339_STATUS_OSF;   /* OSF set at power-on */
    dsc->eosc_mask = DS1339_CTRL_EOSC;
    dsc->century_mask = DS1339_MONTH_CENTURY;
    dsc->ch_mask = 0;   /* bit 7 of the seconds register reads back zero */
}

static const TypeInfo ds1338_types[] = {
    {
        .name          = TYPE_DS1338,
        .parent        = TYPE_I2C_SLAVE,
        .instance_size = sizeof(DS1338State),
        .class_size    = sizeof(DS1338Class),
        .class_init    = ds1338_class_init,
    },
    {
        .name          = TYPE_DS1339,
        .parent        = TYPE_DS1338,
        .class_init    = ds1339_class_init,
    },
};

DEFINE_TYPES(ds1338_types)
