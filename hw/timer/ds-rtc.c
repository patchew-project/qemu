/*
 * MAXIM/Dallas DS1338 and DS1375 I2C RTC+NVRAM
 *
 * Copyright (c) 2018 Michael Davidsaver
 * Copyright (c) 2009 CodeSourcery.
 * Written by Paul Brook, Michael Davidsaver
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/i2c/i2c.h"
#include "hw/registerfields.h"
#include "qemu/bcd.h"

/* Size of NVRAM including both the user-accessible area and the
 * secondary register area.
 */
#define NVRAM_SIZE 64

#define TYPE_DSRTC "dsrtc"
#define DSRTC(obj) OBJECT_CHECK(DSRTCState, (obj), TYPE_DSRTC)
#define DSRTC_CLASS(klass) OBJECT_CLASS_CHECK(DSRTCClass, (klass), TYPE_DSRTC)
#define DSRTC_GET_CLASS(obj) OBJECT_GET_CLASS(DSRTCClass, (obj), TYPE_DSRTC)

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

#define R_DS1338_CTRL (0x7)
#define R_DS1375_CTRL (0xe)

/* use 12 hour mode when set */
FIELD(HOUR, SET12, 6, 1)
/* 00-23 */
FIELD(HOUR, HOUR24, 0, 6)
/* PM when set */
FIELD(HOUR, AMPM, 5, 1)
/* 1-12 (not 0-11!) */
FIELD(HOUR, HOUR12, 0, 5)

/* 1-12 */
FIELD(MONTH, MONTH, 0, 5)
FIELD(MONTH, CENTURY, 7, 1)

FIELD(CTRL, OSF, 5, 1)

typedef struct DSRTCState {
    I2CSlave parent_obj;

    int64_t offset;
    uint8_t wday_offset;
    uint8_t nvram[NVRAM_SIZE];
    int32_t ptr;
    bool addr_byte;
} DSRTCState;

typedef struct DSRTCClass {
    I2CSlaveClass parent_obj;

    bool has_century;
    /* actual address space size must be <= NVRAM_SIZE */
    unsigned addr_size;
    unsigned ctrl_offset;
    void (*ctrl_write)(DSRTCState *s, uint8_t);
} DSRTCClass;

static const VMStateDescription vmstate_dsrtc = {
    .name = "ds1338",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, DSRTCState),
        VMSTATE_INT64(offset, DSRTCState),
        VMSTATE_UINT8_V(wday_offset, DSRTCState, 2),
        VMSTATE_UINT8_ARRAY(nvram, DSRTCState, NVRAM_SIZE),
        VMSTATE_INT32(ptr, DSRTCState),
        VMSTATE_BOOL(addr_byte, DSRTCState),
        VMSTATE_END_OF_LIST()
    }
};

static void capture_current_time(DSRTCState *s, DSRTCClass *k)
{
    /* Capture the current time into the secondary registers
     * which will be actually read by the data transfer operation.
     */
    struct tm now;
    bool mode12 = ARRAY_FIELD_EX32(s->nvram, HOUR, SET12);
    qemu_get_timedate(&now, s->offset);

    s->nvram[R_SEC] = to_bcd(now.tm_sec);
    s->nvram[R_MIN] = to_bcd(now.tm_min);
    s->nvram[R_HOUR] = 0;
    if (mode12) {
        /* map 0-23 to 1-12 am/pm */
        ARRAY_FIELD_DP32(s->nvram, HOUR, SET12, 1);
        ARRAY_FIELD_DP32(s->nvram, HOUR, AMPM, now.tm_hour >= 12u);
        now.tm_hour %= 12u; /* wrap 0-23 to 0-11 */
        if (now.tm_hour == 0u) {
            /* midnight/noon stored as 12 */
            now.tm_hour = 12u;
        }
        ARRAY_FIELD_DP32(s->nvram, HOUR, HOUR12, to_bcd(now.tm_hour));

    } else {
        ARRAY_FIELD_DP32(s->nvram, HOUR, HOUR24, to_bcd(now.tm_hour));
    }
    s->nvram[R_WDAY] = (now.tm_wday + s->wday_offset) % 7;
    if (s->nvram[R_WDAY] == 0) {
        s->nvram[R_WDAY] = 7;
    }
    s->nvram[R_DATE] = to_bcd(now.tm_mday);
    s->nvram[R_MONTH] = to_bcd(now.tm_mon + 1);
    s->nvram[R_YEAR] = to_bcd(now.tm_year % 100u);

    ARRAY_FIELD_DP32(s->nvram, MONTH, CENTURY,
                     k->has_century && now.tm_year >= 100)
}

static void inc_regptr(DSRTCState *s, DSRTCClass *k)
{
    /* The register pointer wraps around after k->addr_size-1; wraparound
     * causes the current time/date to be retransferred into
     * the secondary registers.
     */
    s->ptr = (s->ptr + 1) % k->addr_size;
    if (!s->ptr) {
        capture_current_time(s, k);
    }
}

static int dsrtc_event(I2CSlave *i2c, enum i2c_event event)
{
    DSRTCState *s = DSRTC(i2c);
    DSRTCClass *k = DSRTC_GET_CLASS(s);

    switch (event) {
    case I2C_START_RECV:
        /* In h/w, capture happens on any START condition, not just a
         * START_RECV, but there is no need to actually capture on
         * START_SEND, because the guest can't get at that data
         * without going through a START_RECV which would overwrite it.
         */
        capture_current_time(s, k);
        break;
    case I2C_START_SEND:
        s->addr_byte = true;
        break;
    default:
        break;
    }

    return 0;
}

static int dsrtc_recv(I2CSlave *i2c)
{
    DSRTCState *s = DSRTC(i2c);
    DSRTCClass *k = DSRTC_GET_CLASS(s);
    uint8_t res;

    res  = s->nvram[s->ptr];
    inc_regptr(s, k);
    return res;
}

/* call after guest writes to current time registers
 * to re-compute our offset from host time.
 */
static void dsrtc_update(DSRTCState *s)
{

    struct tm now;
    memset(&now, 0, sizeof(now));

    /* TODO: Implement CH (stop) bit?  */
    now.tm_sec = from_bcd(s->nvram[R_SEC] & 0x7f);
    now.tm_min = from_bcd(s->nvram[R_MIN] & 0x7f);
    if (ARRAY_FIELD_EX32(s->nvram, HOUR, SET12)) {
        /* 12 hour (1-12) */
        /* read and wrap 1-12 -> 0-11 */
        now.tm_hour = from_bcd(ARRAY_FIELD_EX32(s->nvram, HOUR, HOUR12)) % 12u;
        if (ARRAY_FIELD_EX32(s->nvram, HOUR, AMPM)) {
            now.tm_hour += 12;
        }

    } else {
        now.tm_hour = from_bcd(ARRAY_FIELD_EX32(s->nvram, HOUR, HOUR24));
    }
    now.tm_wday = from_bcd(s->nvram[R_WDAY]) - 1u;
    now.tm_mday = from_bcd(s->nvram[R_DATE] & 0x3f);
    now.tm_mon = from_bcd(s->nvram[R_MONTH] & 0x1f) - 1;
    now.tm_year = from_bcd(s->nvram[R_YEAR]) + 100;
    s->offset = qemu_timedate_diff(&now);

    {
        /* Round trip to get real wday_offset based on time delta and
         * ref. timezone.
         * Race if midnight (in ref. timezone) happens here.
         */
        int user_wday = now.tm_wday;
        qemu_get_timedate(&now, s->offset);

        s->wday_offset = (user_wday - now.tm_wday) % 7 + 1;
    }
}

static int dsrtc_send(I2CSlave *i2c, uint8_t data)
{
    DSRTCState *s = DSRTC(i2c);
    DSRTCClass *k = DSRTC_GET_CLASS(s);

    if (s->addr_byte) {
        s->ptr = data % k->addr_size;
        s->addr_byte = false;
        return 0;
    }
    if (s->ptr == k->ctrl_offset) {
        (k->ctrl_write)(s, data);
    }
    s->nvram[s->ptr] = data;
    if (s->ptr <= R_YEAR) {
        dsrtc_update(s);
    }
    inc_regptr(s, k);
    return 0;
}

static void dsrtc_reset(DeviceState *dev)
{
    DSRTCState *s = DSRTC(dev);

    /* The clock is running and synchronized with the host */
    s->offset = 0;
    s->wday_offset = 0;
    memset(s->nvram, 0, NVRAM_SIZE);
    s->ptr = 0;
    s->addr_byte = false;
}

static void dsrtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = dsrtc_event;
    k->recv = dsrtc_recv;
    k->send = dsrtc_send;
    dc->reset = dsrtc_reset;
    dc->vmsd = &vmstate_dsrtc;
}

static const TypeInfo dsrtc_info = {
    .abstract      = true,
    .name          = TYPE_DSRTC,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DSRTCState),
    .class_init    = dsrtc_class_init,
};

static void ds1338_control_write(DSRTCState *s, uint8_t data)
{
    /* Control register. */

    /* allow guest to set no-op controls for clock out pin */
    s->nvram[R_DS1338_CTRL] = data & 0x93;
}

static void ds1338_class_init(ObjectClass *klass, void *data)
{
    DSRTCClass *k = DSRTC_CLASS(klass);

    k->has_century = false;
    k->addr_size = 0x40;
    k->ctrl_offset = R_DS1338_CTRL;
    k->ctrl_write = ds1338_control_write;
}

static const TypeInfo ds1338_info = {
    .name          = "ds1338",
    .parent        = TYPE_DSRTC,
    .class_size    = sizeof(DSRTCClass),
    .class_init    = ds1338_class_init,
};

static void ds1375_control_write(DSRTCState *s, uint8_t data)
{
    /* just store it, we don't model any features */
    s->nvram[R_DS1375_CTRL] = data;
}

static void ds1375_class_init(ObjectClass *klass, void *data)
{
    DSRTCClass *k = DSRTC_CLASS(klass);

    k->has_century = true;
    k->addr_size = 0x20;
    k->ctrl_offset = R_DS1375_CTRL;
    k->ctrl_write = ds1375_control_write;
}

static const TypeInfo ds1375_info = {
    .name          = "ds1375",
    .parent        = TYPE_DSRTC,
    .class_size    = sizeof(DSRTCClass),
    .class_init    = ds1375_class_init,
};

static void dsrtc_register_types(void)
{
    type_register_static(&dsrtc_info);
    type_register_static(&ds1338_info);
    type_register_static(&ds1375_info);
}

type_init(dsrtc_register_types)
