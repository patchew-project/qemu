/* Emulation of various Dallas/Maxim RTCs accessed via I2C bus
 *
 * Copyright (c) 2017 Michael Davidsaver
 * Copyright (c) 2009 CodeSourcery
 *
 * Authors: Michael Davidsaver
 *          Paul Brook
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the LICENSE file in the top-level directory.
 *
 * Models real time read/set and NVRAM.
 * Does not model alarms, or control/status registers.
 *
 * Generalized register map is:
 *   [Current time]
 *   [Alarm settings] (optional)
 *   [Control/Status] (optional)
 *   [Non-volatile memory] (optional)
 *
 * The current time registers are almost always the same,
 * with the exception being that some have a CENTURY bit
 * in the month register.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qemu/bcd.h"
#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/i2c/i2c.h"
#include "sysemu/qtest.h"
#include "qemu/error-report.h"

/* #define DEBUG_DSRTC */

#ifdef DEBUG_DSRTC
#define DPRINTK(FMT, ...) info_report(TYPE_DSRTC " : " FMT, ## __VA_ARGS__)
#else
#define DPRINTK(FMT, ...) do {} while (0)
#endif

#define LOG(MSK, FMT, ...) qemu_log_mask(MSK, TYPE_DSRTC " : " FMT "\n", \
                            ## __VA_ARGS__)

#define DSRTC_REGSIZE (0x40)

/* values stored in BCD */
/* 00-59 */
#define R_SEC   (0x0)
/* 00-59 */
#define R_MIN   (0x1)
#define R_HOUR  (0x2)
/* 1-7 */
#define R_WDAY  (0x3)
/* 0-31 */
#define R_DATE  (0x4)
#define R_MONTH (0x5)
/* 0-99 */
#define R_YEAR  (0x6)

/* use 12 hour mode when set */
FIELD(HOUR, SET12, 6, 1)
/* 00-23 */
FIELD(HOUR, HOUR24, 0, 6)
FIELD(HOUR, AMPM, 5, 1)
/* 1-12 (not 0-11!) */
FIELD(HOUR, HOUR12, 0, 5)

/* 1-12 */
FIELD(MONTH, MONTH, 0, 5)
FIELD(MONTH, CENTURY, 7, 1)

typedef struct DSRTCInfo {
    /* if bit 7 of the Month register is set after Y2K */
    bool has_century;
    /* address of first non-volatile memory cell.
     * nv_start >= reg_end means no NV memory.
     */
    uint8_t nv_start;
    /* total size of register range.  When address counter rolls over. */
    uint8_t reg_size;
} DSRTCInfo;

typedef struct DSRTCState {
    I2CSlave parent_obj;

    const DSRTCInfo *info;

    qemu_irq alarm_irq;

    /* register address counter */
    uint8_t addr;
    /* when writing, whether the address has been sent */
    bool addrd;

    int64_t time_offset;
    int8_t wday_offset;

    uint8_t regs[DSRTC_REGSIZE];
} DSRTCState;

typedef struct DSRTCClass {
    I2CSlaveClass parent_class;

    const DSRTCInfo *info;
} DSRTCClass;

#define TYPE_DSRTC "ds-rtc-i2c"
#define DSRTC(obj) OBJECT_CHECK(DSRTCState, (obj), TYPE_DSRTC)
#define DSRTC_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DSRTCClass, obj, TYPE_DSRTC)
#define DSRTC_CLASS(klass) \
    OBJECT_CLASS_CHECK(DSRTCClass, klass, TYPE_DSRTC)

static const VMStateDescription vmstate_dsrtc = {
    .name = TYPE_DSRTC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, DSRTCState),
        VMSTATE_INT64(time_offset, DSRTCState),
        VMSTATE_INT8_V(wday_offset, DSRTCState, 2),
        VMSTATE_UINT8_ARRAY(regs, DSRTCState, DSRTC_REGSIZE),
        VMSTATE_UINT8(addr, DSRTCState),
        VMSTATE_BOOL(addrd, DSRTCState),
        VMSTATE_END_OF_LIST()
    }
};

static void dsrtc_reset(DeviceState *device);

/* update current time registers */
static
void dsrtc_latch(DSRTCState *ds)
{
    struct tm now;
    bool use12;

    qemu_get_timedate(&now, ds->time_offset);

    DPRINTK("Current Time %3u/%02u/%02u %02u:%02u:%02u (wday %u)",
            1900u + now.tm_year, now.tm_mon, now.tm_mday,
            now.tm_hour, now.tm_min, now.tm_sec,
            now.tm_wday);

    use12 = ARRAY_FIELD_EX32(ds->regs, HOUR, SET12);

    /* ensure unused bits are zero */
    memset(ds->regs, 0, R_YEAR + 1);

    ds->regs[R_SEC] = to_bcd(now.tm_sec);
    ds->regs[R_MIN] = to_bcd(now.tm_min);

    if (!use12) {
        /* 24 hour (0-23) */
        ARRAY_FIELD_DP32(ds->regs, HOUR, HOUR24, to_bcd(now.tm_hour));
    } else {
        /* 12 hour am/pm (1-12) */
        ARRAY_FIELD_DP32(ds->regs, HOUR, SET12, 1);
        ARRAY_FIELD_DP32(ds->regs, HOUR, AMPM, now.tm_hour >= 12u);
        now.tm_hour %= 12u; /* wrap 0-23 to 0-11 */
        if (now.tm_hour == 0u) {
            /* midnight/noon stored as 12 */
            now.tm_hour = 12u;
        }
        ARRAY_FIELD_DP32(ds->regs, HOUR, HOUR12, to_bcd(now.tm_hour));
    }

    ds->regs[R_WDAY] = (now.tm_wday + ds->wday_offset) % 7u + 1u;
    ds->regs[R_DATE] = to_bcd(now.tm_mday);

    ARRAY_FIELD_DP32(ds->regs, MONTH, MONTH, to_bcd(now.tm_mon + 1));
    if (ds->info->has_century) {
        ARRAY_FIELD_DP32(ds->regs, MONTH, CENTURY, now.tm_year >= 100u);
    }

    ds->regs[R_YEAR] = to_bcd(now.tm_year % 100u);

    DPRINTK("Latched time");
}

/* call after guest writes to current time registers
 * to re-compute our offset from host time.
 */
static
void dsrtc_update(DSRTCState *ds)
{
    int user_wday;
    struct tm now;

    now.tm_sec = from_bcd(ds->regs[R_SEC]);
    now.tm_min = from_bcd(ds->regs[R_MIN]);

    if (ARRAY_FIELD_EX32(ds->regs, HOUR, SET12)) {
        /* 12 hour (1-12) */
        /* read and wrap 1-12 -> 0-11 */
        now.tm_hour = from_bcd(ARRAY_FIELD_EX32(ds->regs, HOUR, HOUR12)) % 12u;
        if (ARRAY_FIELD_EX32(ds->regs, HOUR, AMPM)) {
            now.tm_hour += 12;
        }

    } else {
        /* 23 hour (0-23) */
        now.tm_hour = from_bcd(ARRAY_FIELD_EX32(ds->regs, HOUR, HOUR24));
    }

    now.tm_wday = from_bcd(ds->regs[R_WDAY]) - 1u;
    now.tm_mday = from_bcd(ds->regs[R_DATE]);
    now.tm_mon = from_bcd(ARRAY_FIELD_EX32(ds->regs, MONTH, MONTH)) - 1;

    now.tm_year = from_bcd(ds->regs[R_YEAR]);
    if (ARRAY_FIELD_EX32(ds->regs, MONTH, CENTURY) || !ds->info->has_century) {
        now.tm_year += 100;
    }

    DPRINTK("New Time %3u/%02u/%02u %02u:%02u:%02u (wday %u)",
            1900u + now.tm_year, now.tm_mon, now.tm_mday,
            now.tm_hour, now.tm_min, now.tm_sec,
            now.tm_wday);

    /* round trip to get real wday_offset based on time delta */
    user_wday = now.tm_wday;
    ds->time_offset = qemu_timedate_diff(&now);
    /* race possible if we run at midnight
     * TODO: make qemu_timedate_diff() calculate wday offset as well?
     */
    qemu_get_timedate(&now, ds->time_offset);
    /* calculate wday_offset to achieve guest requested wday */
    ds->wday_offset = user_wday - now.tm_wday;

    DPRINTK("Update offset = %" PRId64 ", wday_offset = %d",
            ds->time_offset, ds->wday_offset);
}

static
void dsrtc_advance(DSRTCState *ds)
{
    ds->addr = (ds->addr + 1) % ds->info->reg_size;
    if (ds->addr == 0) {
        /* latch time on roll over */
        dsrtc_latch(ds);
    }
}

static
int dsrtc_event(I2CSlave *s, enum i2c_event event)
{
    DSRTCState *ds = DSRTC(s);

    switch (event) {
    case I2C_START_SEND:
        ds->addrd = false;
        /* fall through */
    case I2C_START_RECV:
        dsrtc_latch(ds);
        /* fall through */
    case I2C_FINISH:
        DPRINTK("Event %d", (int)event);
        /* fall through */
    case I2C_NACK:
        break;
    }
    return 0;
}

static
int dsrtc_recv(I2CSlave *s)
{
    DSRTCState *ds = DSRTC(s);
    int ret = 0;

    ret = ds->regs[ds->addr];

    if (ds->addr > R_YEAR && ds->addr < ds->info->nv_start) {
        LOG(LOG_UNIMP, "Read from unimplemented (%02x) %02x", ds->addr, ret);
    }

    DPRINTK("Recv (%02x) %02x", ds->addr, ret);

    dsrtc_advance(ds);

    return ret;
}

static
int dsrtc_send(I2CSlave *s, uint8_t data)
{
    DSRTCState *ds = DSRTC(s);

    if (!ds->addrd) {
        if (data == 0xff && qtest_enabled()) {
            /* allow test runner to zero offsets */
            DPRINTK("Testing reset");
            dsrtc_reset(DEVICE(s));
            return 0;
        }
        ds->addr = data % DSRTC_REGSIZE;
        ds->addrd = true;
        DPRINTK("Set address pointer %02x", data);
        return 0;
    }

    DPRINTK("Send (%02x) %02x", ds->addr, data);

    if (ds->addr <= R_YEAR) {
        ds->regs[ds->addr] = data;
        dsrtc_update(ds);

    } else if (ds->addr >= ds->info->nv_start) {
        ds->regs[ds->addr] = data;

    } else {
        LOG(LOG_UNIMP, "Register not modeled");
    }

    dsrtc_advance(ds);

    return 0;
}

static
void dsrtc_reset(DeviceState *device)
{
    DSRTCState *ds = DSRTC(device);

    memset(ds->regs, 0, sizeof(ds->regs));

    ds->addr = 0;
    ds->addrd = false;
    ds->time_offset = 0;
    ds->wday_offset = 0;

    DPRINTK("Reset");
}

static
void dsrtc_realize(DeviceState *device, Error **errp)
{
    DSRTCState *ds = DSRTC(device);
    DSRTCClass *r = DSRTC_GET_CLASS(device);

    ds->info = r->info;

    /* Alarms not yet implemented, but allow
     * board code to wire up the alarm interrupt
     * output anyway.
     */
    qdev_init_gpio_out(device, &ds->alarm_irq, 1);
}

static
void dsrtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    DSRTCClass *r = DSRTC_CLASS(klass);

    r->info = data;

    k->event = &dsrtc_event;
    k->recv = &dsrtc_recv;
    k->send = &dsrtc_send;

    dc->vmsd = &vmstate_dsrtc;
    dc->realize = dsrtc_realize;
    dc->reset = dsrtc_reset;
    dc->user_creatable = true;
}

static
const TypeInfo ds_rtc_base_type = {
    .abstract = true,
    .name = TYPE_DSRTC,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DSRTCState),
};

#define DSRTC_CONFIG(NAME) \
static const TypeInfo NAME##_type = { \
    .name = #NAME, \
    .parent = TYPE_DSRTC, \
    .class_size = sizeof(I2CSlaveClass), \
    .class_init = dsrtc_class_init, \
    .class_data = (void *)&NAME##_info, \
};

/* ds3231 - alarms, no eeprom */
static const DSRTCInfo ds3231_info = {
    .has_century = true,
    .nv_start    = 0x13, /* no nv memory */
    .reg_size    = 0x13,
};
DSRTC_CONFIG(ds3231)

/* only model block 0 (RTC), blocks 1,2 (eeprom) not modeled.
 * blocks have different i2c addresses
 */
static const DSRTCInfo ds1388_info = {
    .has_century = false,
    .nv_start    = 0x0d,
    .reg_size    = 0x0d,
};
DSRTC_CONFIG(ds1388)

/* alarms, eeprom */
static const DSRTCInfo ds1375_info = {
    .has_century = true,
    .nv_start    = 0x10,
    .reg_size    = 0x20,
};
DSRTC_CONFIG(ds1375)

/* no alarms, no eeprom */
static const DSRTCInfo ds1340_info = {
    .has_century = false,
    .nv_start    = 0x10,
    .reg_size    = 0x10,
};
DSRTC_CONFIG(ds1340)

/* alarms, no eeprom */
static const DSRTCInfo ds1339_info = {
    .has_century = false,
    .nv_start    = 0x11,
    .reg_size    = 0x11,
};
DSRTC_CONFIG(ds1339)

/* no alarms, eeprom */
static const DSRTCInfo ds1338_info = {
    .has_century = false,
    .nv_start    = 0x08,
    .reg_size    = 0x40,
};
DSRTC_CONFIG(ds1338)

/* alarms, no eeprom */
static const DSRTCInfo ds1337_info = {
    .has_century = true,
    .nv_start    = 0x10,
    .reg_size    = 0x10,
};
DSRTC_CONFIG(ds1337)

/* ds1307 registers are identical to ds1338 */
static
const TypeInfo ds1307_type = {
    .name = "ds1307",
    .parent = "ds1338",
};

static void ds_rtc_i2c_register(void)
{
    type_register_static(&ds_rtc_base_type);
    type_register_static(&ds3231_type);
    type_register_static(&ds1388_type);
    type_register_static(&ds1375_type);
    type_register_static(&ds1340_type);
    type_register_static(&ds1339_type);
    type_register_static(&ds1338_type);
    type_register_static(&ds1337_type);
    type_register_static(&ds1307_type);
}

type_init(ds_rtc_i2c_register)
