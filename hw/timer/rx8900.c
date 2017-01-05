/*
 * Epson RX8900SA/CE Realtime Clock Module
 *
 * Copyright (c) 2016 IBM Corporation
 * Authors:
 *  Alastair D'Silva <alastair@d-silva.org>
 *  Chris Smart <chris@distroguy.com>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * Datasheet available at:
 *  https://support.epson.biz/td/api/doc_check.php?dl=app_RX8900CE&lang=en
 *
 * Not implemented:
 *  Implement i2c timeout
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/i2c/i2c.h"
#include "hw/timer/rx8900_regs.h"
#include "hw/ptimer.h"
#include "qemu/main-loop.h"
#include "qemu/bcd.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "trace.h"

#define TYPE_RX8900 "rx8900"
#define RX8900(obj) OBJECT_CHECK(RX8900State, (obj), TYPE_RX8900)

typedef struct RX8900State {
    I2CSlave parent_obj;

    ptimer_state *sec_timer; /* triggered once per second */
    ptimer_state *fout_timer;
    ptimer_state *countdown_timer;
    bool fout_state;
    int64_t offset;
#define INVALID_WEEKDAY 0xff
    uint8_t weekday; /* Saved for deferred offset calculation, 0-6 */
    uint8_t wday_offset;
    uint8_t nvram[RX8900_NVRAM_SIZE];
    int32_t nvram_offset; /* Wrapped to stay within RX8900_NVRAM_SIZE */
    bool addr_byte;
    uint8_t last_interrupt_seconds; /* The last time the second timer ticked */
    /* the last minute the timer update interrupt was triggered (if enabled) */
    uint8_t last_update_interrupt_minutes;
    double supply_voltage;
    qemu_irq interrupt_pin;
    qemu_irq fout_pin;
    struct tm now;
    bool time_altered; /* True if this transaction altered the time */
} RX8900State;

static const VMStateDescription vmstate_rx8900 = {
    .name = "rx8900",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, RX8900State),
        VMSTATE_PTIMER(sec_timer, RX8900State),
        VMSTATE_PTIMER(fout_timer, RX8900State),
        VMSTATE_PTIMER(countdown_timer, RX8900State),
        VMSTATE_BOOL(fout_state, RX8900State),
        VMSTATE_INT64(offset, RX8900State),
        VMSTATE_UINT8(weekday, RX8900State),
        VMSTATE_UINT8(wday_offset, RX8900State),
        VMSTATE_UINT8_ARRAY(nvram, RX8900State, RX8900_NVRAM_SIZE),
        VMSTATE_INT32(nvram_offset, RX8900State),
        VMSTATE_BOOL(addr_byte, RX8900State),
        VMSTATE_UINT8(last_interrupt_seconds, RX8900State),
        VMSTATE_UINT8(last_update_interrupt_minutes, RX8900State),
        VMSTATE_END_OF_LIST()
    }
};

static void rx8900_reset(DeviceState *dev);

static void capture_current_time(RX8900State *s)
{
    /* Capture the current time into the secondary registers
     * which will be actually read by the data transfer operation.
     */
    qemu_get_timedate(&s->now, s->offset);
    s->nvram[SECONDS] = to_bcd(s->now.tm_sec);
    s->nvram[MINUTES] = to_bcd(s->now.tm_min);
    s->nvram[HOURS] = to_bcd(s->now.tm_hour);

    s->nvram[WEEKDAY] = 0x01 << ((s->now.tm_wday + s->wday_offset) % 7);
    s->nvram[DAY] = to_bcd(s->now.tm_mday);
    s->nvram[MONTH] = to_bcd(s->now.tm_mon + 1);
    s->nvram[YEAR] = to_bcd(s->now.tm_year % 100);

    s->nvram[EXT_SECONDS] = s->nvram[SECONDS];
    s->nvram[EXT_MINUTES] = s->nvram[MINUTES];
    s->nvram[EXT_HOURS] = s->nvram[HOURS];
    s->nvram[EXT_WEEKDAY] = s->nvram[WEEKDAY];
    s->nvram[EXT_DAY] = s->nvram[DAY];
    s->nvram[EXT_MONTH] = s->nvram[MONTH];
    s->nvram[EXT_YEAR] = s->nvram[YEAR];

    trace_rx8900_capture_current_time(s->now.tm_hour, s->now.tm_min,
            s->now.tm_sec,
            (s->now.tm_wday + s->wday_offset) % 7,
            s->now.tm_mday, s->now.tm_mon + 1, s->now.tm_year + 1900,
            s->nvram[HOURS], s->nvram[MINUTES], s->nvram[SECONDS],
            s->nvram[WEEKDAY], s->nvram[DAY], s->nvram[MONTH], s->nvram[YEAR],
            s->offset);
}

/**
 * Increment the internal register pointer, dealing with wrapping
 * @param s the RTC to operate on
 */
static void inc_regptr(RX8900State *s)
{
    /* The register pointer wraps around after 0x1F
     */
    s->nvram_offset = (s->nvram_offset + 1) & (RX8900_NVRAM_SIZE - 1);
    trace_rx8900_regptr_update(s->nvram_offset);

    if (s->nvram_offset == START_ADDRESS) {
        trace_rx8900_regptr_overflow();
        capture_current_time(s);
    }
}

/**
 * Receive an I2C Event
 * @param i2c the i2c device instance
 * @param event the event to handle
 */
static void rx8900_event(I2CSlave *i2c, enum i2c_event event)
{
    RX8900State *s = RX8900(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->addr_byte = true;
        /* fall through */
    case I2C_START_RECV:
        capture_current_time(s);
        s->time_altered = false;
        break;
    case I2C_FINISH:
        if (s->time_altered) {
            s->offset = qemu_timedate_diff(&s->now);
        }

        if (s->weekday < 7) {
            /* We defer the weekday calculation as it is handed to us before
             * the date has been updated. If we calculate the weekday offset
             * when it is passed to us, we will incorrectly determine it
             * based on the current emulated date, rather than the date that
             * has been written.
             */
            struct tm now;
            qemu_get_timedate(&now, s->offset);

            s->wday_offset = (s->weekday - now.tm_wday + 7) % 7;

            trace_rx8900_event_weekday(s->weekday, BIT(s->weekday),
                    s->wday_offset);

            s->weekday = INVALID_WEEKDAY;
        }
        break;

    default:
        break;
    }
}

/**
 * Perform an i2c receive action
 * @param i2c the i2c device instance
 * @return the value of the current register
 * @post the internal register pointer is incremented
 */
static int rx8900_recv(I2CSlave *i2c)
{
    RX8900State *s = RX8900(i2c);
    uint8_t res = s->nvram[s->nvram_offset];
    trace_rx8900_read_register(s->nvram_offset, res);
    inc_regptr(s);
    return res;
}

/**
 * Disable the countdown timer
 * @param s the RTC to operate on
 */
static void disable_countdown_timer(RX8900State *s)
{
    trace_rx8900_disable_countdown_timer();
    ptimer_stop(s->countdown_timer);
}

/**
 * Enable the countdown timer
 * @param s the RTC to operate on
 */
static void enable_countdown_timer(RX8900State *s)
{
    trace_rx8900_enable_countdown_timer();
    ptimer_run(s->countdown_timer, 0);
}

/**
 * Tick the countdown timer
 * @param opaque the device instance
 */
static void rx8900_countdown_tick(void *opaque)
{
    RX8900State *s = (RX8900State *)opaque;

    uint16_t count = s->nvram[TIMER_COUNTER_0] |
            ((s->nvram[TIMER_COUNTER_1] & 0x0F) << 8);
    trace_rx8900_countdown_tick(count);
    count--;

    s->nvram[TIMER_COUNTER_0] = (uint8_t)(count & 0x00ff);
    s->nvram[TIMER_COUNTER_1] = (uint8_t)((count & 0x0f00) >> 8);

    if (count == 0) {
        trace_rx8900_countdown_elapsed();

        disable_countdown_timer(s);

        s->nvram[FLAG_REGISTER] |= FLAG_MASK_TF;

        if (s->nvram[CONTROL_REGISTER] & CTRL_MASK_TIE) {
            trace_rx8900_fire_interrupt();
            qemu_irq_pulse(s->interrupt_pin);
        }
    }
}

/**
 * Disable the per second timer
 * @param s the RTC to operate on
 */
static void disable_timer(RX8900State *s)
{
    trace_rx8900_disable_timer();
    ptimer_stop(s->sec_timer);
}

/**
 * Enable the per second timer
 * @param s the RTC to operate on
 */
static void enable_timer(RX8900State *s)
{
    trace_rx8900_enable_timer();
    ptimer_run(s->sec_timer, 0);
}

/**
 * Tick the per second timer (can be called more frequently as it early exits
 * if the wall clock has not progressed)
 * @param opaque the RTC to tick
 */
static void rx8900_timer_tick(void *opaque)
{
    RX8900State *s = (RX8900State *)opaque;
    struct tm now;
    bool fire_interrupt = false;
    bool alarm_week_day_matches;

    qemu_get_timedate(&now, s->offset);

    if (now.tm_sec == s->last_interrupt_seconds) {
        return;
    }

    s->last_interrupt_seconds = now.tm_sec;

    trace_rx8900_tick();

    /* Update timer interrupt */
    if (s->nvram[CONTROL_REGISTER] & CTRL_MASK_UIE) {
        if ((s->nvram[EXTENSION_REGISTER] & EXT_MASK_USEL) &&
                now.tm_min != s->last_update_interrupt_minutes) {
            s->last_update_interrupt_minutes = now.tm_min;
            s->nvram[FLAG_REGISTER] |= FLAG_MASK_UF;
            fire_interrupt = true;
        } else if (!(s->nvram[EXTENSION_REGISTER] & EXT_MASK_USEL)) {
            /* per second update interrupt */
            s->nvram[FLAG_REGISTER] |= FLAG_MASK_UF;
            fire_interrupt = true;
        }
    }

    /* Alarm interrupt */
    if ((s->nvram[EXTENSION_REGISTER] & EXT_MASK_WADA)) {
        alarm_week_day_matches =
            s->nvram[ALARM_WEEK_DAY] == to_bcd(now.tm_mday);
    } else {
        alarm_week_day_matches =
            s->nvram[ALARM_WEEK_DAY] ==
                0x01 << ((now.tm_wday + s->wday_offset) % 7);
    }

    if ((s->nvram[CONTROL_REGISTER] & CTRL_MASK_AIE) && now.tm_sec == 0 &&
            s->nvram[ALARM_MINUTE] == to_bcd(now.tm_min) &&
            s->nvram[ALARM_HOUR] == to_bcd(now.tm_hour) &&
            alarm_week_day_matches) {
        trace_rx8900_trigger_alarm();
        s->nvram[FLAG_REGISTER] |= FLAG_MASK_AF;
        fire_interrupt = true;
    }

    if (fire_interrupt) {
        trace_rx8900_fire_interrupt();
        qemu_irq_pulse(s->interrupt_pin);
    }
}


#define COUNTDOWN_TIMER_FREQ 4096

/**
 * Validate the extension register and perform actions based on the bits
 * @param s the RTC to operate on
 * @param data the new data for the extension register
 */
static void update_extension_register(RX8900State *s, uint8_t data)
{
    if (data & EXT_MASK_TEST) {
        qemu_log_mask(LOG_GUEST_ERROR,
                "Test bit is enabled but is forbidden by the manufacturer");
    }

    if ((data ^ s->nvram[EXTENSION_REGISTER]) &
            (EXT_MASK_FSEL0 | EXT_MASK_FSEL1)) {
        /* FSELx has changed */

        switch (data & (EXT_MASK_FSEL0 | EXT_MASK_FSEL1)) {
        case EXT_MASK_FSEL0:
            trace_rx8900_set_fout(1024);
            ptimer_set_limit(s->fout_timer, 32, 1);
            break;
        case EXT_MASK_FSEL1:
            trace_rx8900_set_fout(1);
            ptimer_set_limit(s->fout_timer, 32768, 1);
            break;
        case 0:
        case (EXT_MASK_FSEL0 | EXT_MASK_FSEL1):
            trace_rx8900_set_fout(32768);
            ptimer_set_limit(s->fout_timer, 1, 1);
            break;
        }
    }

    if ((data ^ s->nvram[EXTENSION_REGISTER]) &
            (EXT_MASK_TSEL0 | EXT_MASK_TSEL1)) {
        /* TSELx has changed */
        switch (data & (EXT_MASK_TSEL0 | EXT_MASK_TSEL1)) {
        case 0:
            trace_rx8900_set_countdown_timer(64);
            ptimer_set_limit(s->countdown_timer, COUNTDOWN_TIMER_FREQ / 64, 1);
            break;
        case EXT_MASK_TSEL0:
            trace_rx8900_set_countdown_timer(1);
            ptimer_set_limit(s->countdown_timer, COUNTDOWN_TIMER_FREQ, 1);
            break;
        case EXT_MASK_TSEL1:
            trace_rx8900_set_countdown_timer_per_minute();
            ptimer_set_limit(s->countdown_timer, COUNTDOWN_TIMER_FREQ * 60, 1);
            break;
        case (EXT_MASK_TSEL0 | EXT_MASK_TSEL1):
            trace_rx8900_set_countdown_timer(COUNTDOWN_TIMER_FREQ);
            ptimer_set_limit(s->countdown_timer, 1, 1);
            break;
        }
    }

    if (data & EXT_MASK_TE) {
        enable_countdown_timer(s);
    }

    s->nvram[EXTENSION_REGISTER] = data;
    s->nvram[EXT_EXTENSION_REGISTER] = data;

}
/**
 * Validate the control register and perform actions based on the bits
 * @param s the RTC to operate on
 * @param data the new value for the control register
 */

static void update_control_register(RX8900State *s, uint8_t data)
{
    uint8_t diffmask = ~s->nvram[CONTROL_REGISTER] & data;

    if (diffmask & CTRL_MASK_WP0) {
        data &= ~CTRL_MASK_WP0;
        qemu_log_mask(LOG_GUEST_ERROR,
            "Attempt to write to write protected bit %d in control register",
            CTRL_REG_WP0);
    }

    if (diffmask & CTRL_MASK_WP1) {
        data &= ~CTRL_MASK_WP1;
        qemu_log_mask(LOG_GUEST_ERROR,
            "Attempt to write to write protected bit %d in control register",
            CTRL_REG_WP1);
    }

    if (data & CTRL_MASK_RESET) {
        data &= ~CTRL_MASK_RESET;
        rx8900_reset(DEVICE(s));
    }

    if (diffmask & CTRL_MASK_UIE) {
        /* Update interrupts were off and are now on */
        struct tm now;

        trace_rx8900_enable_update_timer();

        qemu_get_timedate(&now, s->offset);

        s->last_update_interrupt_minutes = now.tm_min;
        s->last_interrupt_seconds = now.tm_sec;
        enable_timer(s);
    }

    if (diffmask & CTRL_MASK_AIE) {
        /* Alarm interrupts were off and are now on */
        struct tm now;

        trace_rx8900_enable_alarm();

        qemu_get_timedate(&now, s->offset);

        s->last_interrupt_seconds = now.tm_sec;
        enable_timer(s);
    }

    if (!(data & (CTRL_MASK_UIE | CTRL_MASK_AIE))) {
        disable_timer(s);
    }

    s->nvram[CONTROL_REGISTER] = data;
    s->nvram[EXT_CONTROL_REGISTER] = data;
}

/**
 * Validate the flag register
 * @param s the RTC to operate on
 * @param data the new value for the flag register
 */
static void validate_flag_register(RX8900State *s, uint8_t *data)
{
    uint8_t diffmask = ~s->nvram[FLAG_REGISTER] & *data;

    if (diffmask & FLAG_MASK_VDET) {
        *data &= ~FLAG_MASK_VDET;
        qemu_log_mask(LOG_GUEST_ERROR,
            "Only 0 can be written to VDET bit %d in the flag register",
            FLAG_REG_VDET);
    }

    if (diffmask & FLAG_MASK_VLF) {
        *data &= ~FLAG_MASK_VLF;
        qemu_log_mask(LOG_GUEST_ERROR,
            "Only 0 can be written to VLF bit %d in the flag register",
            FLAG_REG_VLF);
    }

    if (diffmask & FLAG_MASK_UNUSED_2) {
        *data &= ~FLAG_MASK_UNUSED_2;
        qemu_log_mask(LOG_GUEST_ERROR,
            "Only 0 can be written to unused bit %d in the flag register",
            FLAG_REG_UNUSED_2);
    }

    if (diffmask & FLAG_MASK_UNUSED_6) {
        *data &= ~FLAG_MASK_UNUSED_6;
        qemu_log_mask(LOG_GUEST_ERROR,
            "Only 0 can be written to unused bit %d in the flag register",
            FLAG_REG_UNUSED_6);
    }

    if (diffmask & FLAG_MASK_UNUSED_7) {
        *data &= ~FLAG_MASK_UNUSED_7;
        qemu_log_mask(LOG_GUEST_ERROR,
            "Only 0 can be written to unused bit %d in the flag register",
            FLAG_REG_UNUSED_7);
    }
}

/**
 * Handle FOUT_ENABLE (FOE) line
 * Enables/disables the FOUT line
 * @param opaque the device instance
 * @param n the IRQ number
 * @param level true if the line has been raised
 */
static void rx8900_fout_enable_handler(void *opaque, int n, int level)
{
    RX8900State *s = RX8900(opaque);

    if (level) {
        trace_rx8900_enable_fout();
        ptimer_run(s->fout_timer, 0);
    } else {
        /* disable fout */
        trace_rx8900_disable_fout();
        ptimer_stop(s->fout_timer);
    }
}

/**
 * Tick the FOUT timer
 * @param opaque the device instance
 */
static void rx8900_fout_tick(void *opaque)
{
    RX8900State *s = (RX8900State *)opaque;

    trace_rx8900_fout_toggle();
    s->fout_state = !s->fout_state;

    qemu_set_irq(s->fout_pin, s->fout_state ? 1 : 0);
}

/**
 * Verify the current voltage and raise flags if it is low
 * @param s the RTC to operate on
 */
static void check_voltage(RX8900State *s)
{
    if (!(s->nvram[BACKUP_FUNCTION] & BACKUP_MASK_VDETOFF)) {
        if (s->supply_voltage < 2.0) {
            s->nvram[FLAG_REGISTER] |= FLAG_MASK_VDET;
        }

        if (s->supply_voltage < 1.6) {
            s->nvram[FLAG_REGISTER] |= FLAG_MASK_VLF;
        }
    }
}

/**
 * Determine if we have a valid weekday mask
 * @return true if the weekday is valid
 */
static bool weekday_is_valid(uint8_t weekday)
{
    if (ctpop8(weekday) == 1 && weekday <= 0x40) {
        return true;
    }

    return false;
}

/**
 * Receive a byte of data from i2c
 * @param i2c the i2c device that is receiving data
 * @param data the data that was received
 */
static int rx8900_send(I2CSlave *i2c, uint8_t data)
{
    RX8900State *s = RX8900(i2c);

    trace_rx8900_i2c_data_receive(data);

    if (s->addr_byte) {
        s->nvram_offset = data & (RX8900_NVRAM_SIZE - 1);
        trace_rx8900_regptr_update(s->nvram_offset);
        s->addr_byte = false;
        return 0;
    }

    trace_rx8900_set_register(s->nvram_offset, data);

    switch (s->nvram_offset) {
    case SECONDS:
    case EXT_SECONDS:
        s->time_altered = true;
        s->now.tm_sec = from_bcd(data & 0x7f);
        if (s->now.tm_sec > 59) { /* RX8900 does not support leap seconds */
            qemu_log_mask(LOG_GUEST_ERROR,
                "RX8900 - second data '%x' is out of range, "
                    "undefined behavior will result", data);
        }
        break;

    case MINUTES:
    case EXT_MINUTES:
        s->time_altered = true;
        s->now.tm_min = from_bcd(data & 0x7f);
        if (s->now.tm_min > 59) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "RX8900 - minute data '%x' is out of range, "
                    "undefined behavior will result", data);
        }
        break;

    case HOURS:
    case EXT_HOURS:
        s->time_altered = true;
        s->now.tm_hour = from_bcd(data & 0x3f);
        if (s->now.tm_hour > 24) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "RX8900 - hour data '%x' is out of range, "
                    "undefined behavior will result", data);
        }
        break;

    case WEEKDAY:
    case EXT_WEEKDAY: {
        int user_wday = ctz32(data);
        /* The day field is supposed to contain a value in
         * with only 1 of bits 0-6 set. Otherwise behavior is undefined.
         */
        if (!weekday_is_valid(data)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "RX8900 - weekday data '%x' is out of range, "
                        "undefined behavior will result", data);
        }
        s->weekday = user_wday;
        break;
    }

    case DAY:
    case EXT_DAY:
        s->time_altered = true;
        s->now.tm_mday = from_bcd(data & 0x3f);
        break;

    case MONTH:
    case EXT_MONTH:
        s->time_altered = true;
        s->now.tm_mon = from_bcd(data & 0x1f) - 1;
        break;

    case YEAR:
    case EXT_YEAR:
        s->time_altered = true;
        s->now.tm_year = from_bcd(data) + 100;
        break;

    case EXTENSION_REGISTER:
    case EXT_EXTENSION_REGISTER:
        update_extension_register(s, data);
        break;

    case FLAG_REGISTER:
    case EXT_FLAG_REGISTER:
        validate_flag_register(s, &data);

        s->nvram[FLAG_REGISTER] = data;
        s->nvram[EXT_FLAG_REGISTER] = data;

        check_voltage(s);
        break;

    case CONTROL_REGISTER:
    case EXT_CONTROL_REGISTER:
        update_control_register(s, data);
        break;

    default:
        s->nvram[s->nvram_offset] = data;
    }

    inc_regptr(s);
    return 0;
}

/**
 * Get the device temperature in Celsius as a property
 * @param obj the device
 * @param v
 * @param name the property name
 * @param opaque
 * @param errp an error object to populate on failure
 */
static void rx8900_get_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    RX8900State *s = RX8900(obj);
    double value = (s->nvram[TEMPERATURE] * 2.0 - 187.1) / 3.218;

    trace_rx8900_get_temperature(s->nvram[TEMPERATURE], value);

    visit_type_number(v, name, &value, errp);
}

/**
 * Encode a temperature in Celsius
 * @param celsius the temperature
 * @return the encoded temperature
 */
static inline uint8_t encode_temperature(double celsius)
{
    return (uint8_t) ((celsius * 3.218 + 187.19) / 2);
}

/**
 * Set the device temperature in Celsius as a property
 * @param obj the device
 * @param v
 * @param name the property name
 * @param opaque
 * @param errp an error object to populate on failure
 */
static void rx8900_set_temperature(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    RX8900State *s = RX8900(obj);
    Error *local_err = NULL;
    double temp; /* degrees Celsius */
    visit_type_number(v, name, &temp, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (temp >= 100 || temp < -58) {
        error_setg(errp, "value %fC is out of range", temp);
        return;
    }

    s->nvram[TEMPERATURE] = encode_temperature(temp);

    trace_rx8900_set_temperature(s->nvram[TEMPERATURE], temp);
}

/**
 * Get the device supply voltage as a property
 * @param obj the device
 * @param v
 * @param name the property name
 * @param opaque
 * @param errp an error object to populate on failure
 */
static void rx8900_get_voltage(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    RX8900State *s = RX8900(obj);

    visit_type_number(v, name, &s->supply_voltage, errp);
}

/**
 * Set the device supply voltage as a property
 * @param obj the device
 * @param v
 * @param name the property name
 * @param opaque
 * @param errp an error object to populate on failure
 */
static void rx8900_set_voltage(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    RX8900State *s = RX8900(obj);
    Error *local_err = NULL;
    double voltage;
    visit_type_number(v, name, &voltage, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    s->supply_voltage = voltage;
    trace_rx8900_set_voltage(s->supply_voltage);

    check_voltage(s);
}


/**
 * Configure device properties
 * @param obj the device
 */
static void rx8900_initfn(Object *obj)
{
    object_property_add(obj, "temperature", "number",
                        rx8900_get_temperature,
                        rx8900_set_temperature, NULL, NULL, NULL);

    object_property_add(obj, "voltage", "number",
                        rx8900_get_voltage,
                        rx8900_set_voltage, NULL, NULL, NULL);
}

/**
 * Reset the device
 * @param dev the RX8900 device to reset
 */
static void rx8900_reset(DeviceState *dev)
{
    RX8900State *s = RX8900(dev);

    trace_rx8900_reset();

    /* The clock is running and synchronized with the host */
    s->offset = 0;
    s->weekday = INVALID_WEEKDAY;

    s->nvram[EXTENSION_REGISTER] = EXT_MASK_TSEL1;
    s->nvram[CONTROL_REGISTER] = CTRL_MASK_CSEL0;
    s->nvram[FLAG_REGISTER] &= FLAG_MASK_VDET | FLAG_MASK_VLF;

    s->nvram_offset = 0;

    trace_rx8900_regptr_update(s->nvram_offset);

    s->addr_byte = false;
}

/**
 * Realize an RX8900 device instance
 * Set up timers
 * Configure GPIO lines
 * @param dev the device instance to realize
 * @param errp an error object to populate on error
 */
static void rx8900_realize(DeviceState *dev, Error **errp)
{
    RX8900State *s = RX8900(dev);
    I2CSlave *i2c = I2C_SLAVE(dev);
    QEMUBH *bh;
    const char *name;

    s->fout_state = false;

    memset(s->nvram, 0, RX8900_NVRAM_SIZE);
    /* Set the initial state to 25 degrees Celsius */
    s->nvram[TEMPERATURE] = encode_temperature(25.0);

    /* Set up timers */
    bh = qemu_bh_new(rx8900_timer_tick, s);
    s->sec_timer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    /* we trigger the timer at 10Hz and check for rollover, as the qemu
     * clock does not advance in realtime in the test environment,
     * leading to unstable test results
     */
    ptimer_set_freq(s->sec_timer, 10);
    ptimer_set_limit(s->sec_timer, 1, 1);

    bh = qemu_bh_new(rx8900_fout_tick, s);
    s->fout_timer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    /* frequency doubled to generate 50% duty cycle square wave */
    ptimer_set_freq(s->fout_timer, 32768 * 2);
    ptimer_set_limit(s->fout_timer, 1, 1);

    bh = qemu_bh_new(rx8900_countdown_tick, s);
    s->countdown_timer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    ptimer_set_freq(s->countdown_timer, COUNTDOWN_TIMER_FREQ);
    ptimer_set_limit(s->countdown_timer, COUNTDOWN_TIMER_FREQ, 1);


    /* set up GPIO */
    name = "rx8900-interrupt-out";
    qdev_init_gpio_out_named(&i2c->qdev, &s->interrupt_pin, name, 1);
    trace_rx8900_pin_name("Interrupt", name);

    name = "rx8900-fout-enable";
    qdev_init_gpio_in_named(&i2c->qdev, rx8900_fout_enable_handler, name, 1);
    trace_rx8900_pin_name("Fout-enable", name);

    name = "rx8900-fout";
    qdev_init_gpio_out_named(&i2c->qdev, &s->fout_pin, name, 1);
    trace_rx8900_pin_name("Fout", name);

    /* Set up default voltage */
    s->supply_voltage = 3.3f;
    trace_rx8900_set_voltage(s->supply_voltage);

    s->time_altered = false;
}

/**
 * Set up the device callbacks
 * @param klass the device class
 * @param data
 */
static void rx8900_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = rx8900_event;
    k->recv = rx8900_recv;
    k->send = rx8900_send;
    dc->realize = rx8900_realize;
    dc->reset = rx8900_reset;
    dc->vmsd = &vmstate_rx8900;
}

static const TypeInfo rx8900_info = {
    .name = TYPE_RX8900,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(RX8900State),
    .instance_init = rx8900_initfn,
    .class_init = rx8900_class_init,
};

/**
 * Register the device with QEMU
 */
static void rx8900_register_types(void)
{
    type_register_static(&rx8900_info);
}

type_init(rx8900_register_types)
