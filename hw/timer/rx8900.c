/*
 * Epson RX8900SA/CE Realtime Clock Module
 *
 * Copyright (c) 2016 IBM Corporation
 * Authors:
 *  Alastair D'Silva <alastair@d-silva.org>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * Datasheet available at:
 *  https://support.epson.biz/td/api/doc_check.php?dl=app_RX8900CE&lang=en
 *
 * Not implemented:
 *  Implement Timer Counters
 *  Implement i2c timeout
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/i2c/i2c.h"
#include "hw/timer/rx8900_regs.h"
#include "hw/ptimer.h"
#include "qemu/main-loop.h"
#include "qemu/bcd.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qapi/visitor.h"

 #include <sys/time.h>

 #include <execinfo.h>

#define TYPE_RX8900 "rx8900"
#define RX8900(obj) OBJECT_CHECK(RX8900State, (obj), TYPE_RX8900)

static bool log;

typedef struct RX8900State {
    I2CSlave parent_obj;

    ptimer_state *sec_timer; /* triggered once per second */
    ptimer_state *fout_timer;
    ptimer_state *countdown_timer;
    bool fout;
    int64_t offset;
    uint8_t weekday; /* Saved for deferred offset calculation, 0-6 */
    uint8_t wday_offset;
    uint8_t nvram[RX8900_NVRAM_SIZE];
    int32_t ptr; /* Wrapped to stay within RX8900_NVRAM_SIZE */
    bool addr_byte;
    uint8_t last_interrupt_seconds;
    uint8_t last_update_interrupt_minutes;
    qemu_irq interrupt_pin;
    qemu_irq fout_pin;
} RX8900State;

static const VMStateDescription vmstate_rx8900 = {
    .name = "rx8900",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, RX8900State),
        VMSTATE_PTIMER(sec_timer, RX8900State),
        VMSTATE_PTIMER(fout_timer, RX8900State),
        VMSTATE_PTIMER(countdown_timer, RX8900State),
        VMSTATE_BOOL(fout, RX8900State),
        VMSTATE_INT64(offset, RX8900State),
        VMSTATE_UINT8_V(weekday, RX8900State, 2),
        VMSTATE_UINT8_V(wday_offset, RX8900State, 2),
        VMSTATE_UINT8_ARRAY(nvram, RX8900State, RX8900_NVRAM_SIZE),
        VMSTATE_INT32(ptr, RX8900State),
        VMSTATE_BOOL(addr_byte, RX8900State),
        VMSTATE_UINT8_V(last_interrupt_seconds, RX8900State, 2),
        VMSTATE_UINT8_V(last_update_interrupt_minutes, RX8900State, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void rx8900_reset(DeviceState *dev);
static void disable_countdown_timer(RX8900State *s);
static void enable_countdown_timer(RX8900State *s);
static void disable_timer(RX8900State *s);
static void enable_timer(RX8900State *s);

#ifdef RX8900_TRACE
#define RX8900_TRACE_BUF_SIZE 256
/**
 * Emit a trace message
 * @param file the source filename
 * @param line the line number the message was emitted from
 * @param dev the RX8900 device
 * @param fmt a printf style format
 */
static void trace(const char *file, int line, const char *func,
        I2CSlave *dev, const char *fmt, ...)
{
    va_list ap;
    char buf[RX8900_TRACE_BUF_SIZE];
    char timestamp[32];
    int len;
    struct timeval now;
    struct tm *now2;

    gettimeofday(&now, NULL);
    now2 = localtime(&now.tv_sec);

    strftime(timestamp, sizeof(timestamp), "%F %T", now2);

    len = snprintf(buf, sizeof(buf), "\n\t%s.%03ld %s:%s:%d: RX8900 %s %s@0x%x: %s",
            timestamp, now.tv_usec / 1000,
            file, func, line, dev->qdev.id, dev->qdev.parent_bus->name,
            dev->address, fmt);
    if (len >= RX8900_TRACE_BUF_SIZE) {
        error_report("%s:%d: Trace buffer overflow", file, line);
    }

    va_start(ap, fmt);
    error_vreport(buf, ap);
    va_end(ap);
}

/**
 * Emit a trace message
 * @param dev the RX8900 device
 * @param fmt a printf format
 */
#define TRACE(dev, fmt, ...) \
    do { \
        if (log) { \
            trace(__FILE__, __LINE__, __func__, &dev, fmt, ## __VA_ARGS__); \
        } \
    } while (0)
#else
#define TRACE(dev, fmt, ...)
#endif

static void capture_current_time(RX8900State *s)
{
    /* Capture the current time into the secondary registers
     * which will be actually read by the data transfer operation.
     */
    struct tm now;
    qemu_get_timedate(&now, s->offset);
    s->nvram[SECONDS] = to_bcd(now.tm_sec);
    s->nvram[MINUTES] = to_bcd(now.tm_min);
    s->nvram[HOURS] = to_bcd(now.tm_hour);

    s->nvram[WEEKDAY] = 0x01 << ((now.tm_wday + s->wday_offset) % 7);
    s->nvram[DAY] = to_bcd(now.tm_mday);
    s->nvram[MONTH] = to_bcd(now.tm_mon + 1);
    s->nvram[YEAR] = to_bcd(now.tm_year % 100);

    s->nvram[EXT_SECONDS] = s->nvram[SECONDS];
    s->nvram[EXT_MINUTES] = s->nvram[MINUTES];
    s->nvram[EXT_HOURS] = s->nvram[HOURS];
    s->nvram[EXT_WEEKDAY] = s->nvram[WEEKDAY];
    s->nvram[EXT_DAY] = s->nvram[DAY];
    s->nvram[EXT_MONTH] = s->nvram[MONTH];
    s->nvram[EXT_YEAR] = s->nvram[YEAR];

    TRACE(s->parent_obj, "Update current time to %02d:%02d:%02d %d %d/%d/%d "
            "(0x%02x%02x%02x%02x%02x%02x%02x)",
            now.tm_hour, now.tm_min, now.tm_sec,
            (now.tm_wday + s->wday_offset) % 7,
            now.tm_mday, now.tm_mon, now.tm_year + 1900,
            s->nvram[HOURS], s->nvram[MINUTES], s->nvram[SECONDS],
            s->nvram[WEEKDAY],
            s->nvram[DAY], s->nvram[MONTH], s->nvram[YEAR]);
}

/**
 * Increment the internal register pointer, dealing with wrapping
 * @param s the RTC to operate on
 */
static void inc_regptr(RX8900State *s)
{
    /* The register pointer wraps around after 0x1F
     */
    s->ptr = (s->ptr + 1) & (RX8900_NVRAM_SIZE - 1);
    TRACE(s->parent_obj, "Operating on register 0x%02x", s->ptr);

    if (s->ptr == 0x00) {
        TRACE(s->parent_obj, "Register pointer has overflowed, wrapping to 0");
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
    case I2C_START_RECV:
        /* In h/w, time capture happens on any START condition, not just a
         * START_RECV. For the emulation, it doesn't actually matter,
         * since a START_RECV has to occur before the data can be read.
         */
        capture_current_time(s);
        break;
    case I2C_START_SEND:
        s->addr_byte = true;
        break;
    case I2C_FINISH:
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

            TRACE(s->parent_obj, "Set weekday to %d (0x%02x), wday_offset=%d",
                    s->weekday, BIT(s->weekday), s->wday_offset);

            s->weekday = 7;
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
    uint8_t res = s->nvram[s->ptr];
    TRACE(s->parent_obj, "Read register 0x%x = 0x%x", s->ptr, res);
    inc_regptr(s);
    return res;
}

/**
 * Validate the extension register and perform actions based on the bits
 * @param s the RTC to operate on
 * @param data the new data for the extension register
 */
static void update_extension_register(RX8900State *s, uint8_t data)
{
    if (data & EXT_MASK_TEST) {
        error_report("WARNING: RX8900 - "
            "Test bit is enabled but is forbidden by the manufacturer");
    }

    if ((data ^ s->nvram[EXTENSION_REGISTER]) &
            (EXT_MASK_FSEL0 | EXT_MASK_FSEL1)) {
        uint8_t fsel = (data & (EXT_MASK_FSEL0 | EXT_MASK_FSEL1))
                >> EXT_REG_FSEL0;
        /* FSELx has changed */
        switch (fsel) {
        case 0x01:
            TRACE(s->parent_obj, "Setting fout to 1024Hz");
            ptimer_set_limit(s->fout_timer, 32, 1);
            break;
        case 0x02:
            TRACE(s->parent_obj, "Setting fout to 1Hz");
            ptimer_set_limit(s->fout_timer, 32768, 1);
            break;
        default:
            TRACE(s->parent_obj, "Setting fout to 32768Hz");
            ptimer_set_limit(s->fout_timer, 1, 1);
            break;
        }
    }

    if ((data ^ s->nvram[EXTENSION_REGISTER]) &
            (EXT_MASK_TSEL0 | EXT_MASK_TSEL1)) {
        uint8_t tsel = (data & (EXT_MASK_TSEL0 | EXT_MASK_TSEL1))
                >> EXT_REG_TSEL0;
        /* TSELx has changed */
        switch (tsel) {
        case 0x00:
            TRACE(s->parent_obj, "Setting countdown timer to 64 Hz");
            ptimer_set_limit(s->countdown_timer, 4096 / 64, 1);
            break;
        case 0x01:
            TRACE(s->parent_obj, "Setting countdown timer to 1 Hz");
            ptimer_set_limit(s->countdown_timer, 4096, 1);
            break;
        case 0x02:
            TRACE(s->parent_obj,
                    "Setting countdown timer to per minute updates");
            ptimer_set_limit(s->countdown_timer, 4069 * 60, 1);
            break;
        case 0x03:
            TRACE(s->parent_obj, "Setting countdown timer to 4096Hz");
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
        error_report("WARNING: RX8900 - "
            "Attempt to write to write protected bit %d in control register",
            CTRL_REG_WP0);
    }

    if (diffmask & CTRL_MASK_WP1) {
        data &= ~CTRL_MASK_WP1;
        error_report("WARNING: RX8900 - "
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

        TRACE(s->parent_obj, "Enabling update timer");

        qemu_get_timedate(&now, s->offset);

        s->last_update_interrupt_minutes = now.tm_min;
        s->last_interrupt_seconds = now.tm_sec;
        enable_timer(s);
    }

    if (diffmask & CTRL_MASK_AIE) {
        /* Alarm interrupts were off and are now on */
        struct tm now;

        TRACE(s->parent_obj, "Enabling alarm");

        qemu_get_timedate(&now, s->offset);

        s->last_interrupt_seconds = now.tm_sec;
        enable_timer(s);
    }

    if (!(data & (CTRL_MASK_UIE | CTRL_MASK_AIE))) {
        disable_timer(s);
    }

    if (data & CTRL_MASK_TIE) {
        enable_countdown_timer(s);
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
        error_report("WARNING: RX8900 - "
            "Only 0 can be written to VDET bit %d in the flag register",
            FLAG_REG_VDET);
    }

    if (diffmask & FLAG_MASK_VLF) {
        *data &= ~FLAG_MASK_VLF;
        error_report("WARNING: RX8900 - "
            "Only 0 can be written to VLF bit %d in the flag register",
            FLAG_REG_VLF);
    }

    if (diffmask & FLAG_MASK_UNUSED_2) {
        *data &= ~FLAG_MASK_UNUSED_2;
        error_report("WARNING: RX8900 - "
            "Only 0 can be written to unused bit %d in the flag register",
            FLAG_REG_UNUSED_2);
    }

    if (diffmask & FLAG_MASK_UNUSED_6) {
        *data &= ~FLAG_MASK_UNUSED_6;
        error_report("WARNING: RX8900 - "
            "Only 0 can be written to unused bit %d in the flag register",
            FLAG_REG_UNUSED_6);
    }

    if (diffmask & FLAG_MASK_UNUSED_7) {
        *data &= ~FLAG_MASK_UNUSED_7;
        error_report("WARNING: RX8900 - "
            "Only 0 can be written to unused bit %d in the flag register",
            FLAG_REG_UNUSED_7);
    }
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

    qemu_get_timedate(&now, s->offset);

    if (now.tm_sec == s->last_interrupt_seconds) {
        return;
    }

    s->last_interrupt_seconds = now.tm_sec;

    TRACE(s->parent_obj, "Tick");

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
    if ((s->nvram[CONTROL_REGISTER] & CTRL_MASK_AIE) && now.tm_sec == 0) {
        if (s->nvram[ALARM_MINUTE] == to_bcd(now.tm_min) &&
                s->nvram[ALARM_HOUR] == to_bcd(now.tm_hour) &&
                s->nvram[ALARM_WEEK_DAY] ==
                        ((s->nvram[EXTENSION_REGISTER] & EXT_MASK_WADA) ?
                                to_bcd(now.tm_mday) :
                                0x01 << ((now.tm_wday + s->wday_offset) % 7))) {
            TRACE(s->parent_obj, "Triggering alarm");
            s->nvram[FLAG_REGISTER] |= FLAG_MASK_AF;
            fire_interrupt = true;
        }
    }

    if (fire_interrupt) {
        TRACE(s->parent_obj, "Pulsing interrupt");
        qemu_irq_pulse(s->interrupt_pin);
    }
}

/**
 * Disable the per second timer
 * @param s the RTC to operate on
 */
static void disable_timer(RX8900State *s)
{
    TRACE(s->parent_obj, "Disabling timer");
    ptimer_stop(s->sec_timer);
}

/**
 * Enable the per second timer
 * @param s the RTC to operate on
 */
static void enable_timer(RX8900State *s)
{
    TRACE(s->parent_obj, "Enabling timer");
    ptimer_run(s->sec_timer, 0);
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
        TRACE(s->parent_obj, "Enabling fout");
        ptimer_run(s->fout_timer, 0);
    } else {
        /* disable fout */
        TRACE(s->parent_obj, "Disabling fout");
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

    TRACE(s->parent_obj, "fout toggle");
    s->fout = !s->fout;

    if (s->fout) {
        qemu_irq_raise(s->fout_pin);
    } else {
        qemu_irq_lower(s->fout_pin);
    }
}


/**
 * Disable the countdown timer
 * @param s the RTC to operate on
 */
static void disable_countdown_timer(RX8900State *s)
{
    TRACE(s->parent_obj, "Disabling countdown timer");
    ptimer_stop(s->countdown_timer);
}

/**
 * Enable the per second timer
 * @param s the RTC to operate on
 */
static void enable_countdown_timer(RX8900State *s)
{
    TRACE(s->parent_obj, "Enabling countdown timer");
    ptimer_run(s->countdown_timer, 0);
}

/**
 * Tick the countdown timer
 * @param opaque the device instance
 */
static void rx8900_countdown_tick(void *opaque)
{
    RX8900State *s = (RX8900State *)opaque;

    uint16_t count = s->nvram[TIMER_COUNTER_0] +
            ((s->nvram[TIMER_COUNTER_1] & 0x0F) << 8);
    TRACE(s->parent_obj, "countdown tick, count=%d", count);
    count--;

    s->nvram[TIMER_COUNTER_0] = (uint8_t)(count & 0x00ff);
    s->nvram[TIMER_COUNTER_1] = (uint8_t)((count & 0x0f00) >> 8);

    if (count == 0) {
        TRACE(s->parent_obj, "Countdown has elapsed, pulsing interrupt");

        disable_countdown_timer(s);

        s->nvram[FLAG_REGISTER] |= FLAG_MASK_TF;
        qemu_irq_pulse(s->interrupt_pin);
    }
}


/**
 * Receive a byte of data from i2c
 * @param i2c the i2c device that is receiving data
 * @param data the data that was received
 */
static int rx8900_send(I2CSlave *i2c, uint8_t data)
{
    RX8900State *s = RX8900(i2c);
    struct tm now;

    TRACE(s->parent_obj, "Received I2C data 0x%02x", data);

    if (s->addr_byte) {
        s->ptr = data & (RX8900_NVRAM_SIZE - 1);
        TRACE(s->parent_obj, "Operating on register 0x%02x", s->ptr);
        s->addr_byte = false;
        return 0;
    }

    TRACE(s->parent_obj, "Set data 0x%02x=0x%02x", s->ptr, data);

    qemu_get_timedate(&now, s->offset);
    switch (s->ptr) {
    case SECONDS:
    case EXT_SECONDS:
        now.tm_sec = from_bcd(data & 0x7f);
        s->offset = qemu_timedate_diff(&now);
        break;

    case MINUTES:
    case EXT_MINUTES:
        now.tm_min = from_bcd(data & 0x7f);
        s->offset = qemu_timedate_diff(&now);
        break;

    case HOURS:
    case EXT_HOURS:
        now.tm_hour = from_bcd(data & 0x3f);
        s->offset = qemu_timedate_diff(&now);
        break;

    case WEEKDAY:
    case EXT_WEEKDAY: {
        int user_wday = ctz32(data);
        /* The day field is supposed to contain a value in
         * the range 0-6. Otherwise behavior is undefined.
         */
        switch (data) {
        case 0x01:
        case 0x02:
        case 0x04:
        case 0x08:
        case 0x10:
        case 0x20:
        case 0x40:
            break;
        default:
            error_report("WARNING: RX8900 - weekday data '%x' is out of range,"
                    " undefined behavior will result", data);
            break;
        }
        s->weekday = user_wday;
        break;
    }

    case DAY:
    case EXT_DAY:
        now.tm_mday = from_bcd(data & 0x3f);
        s->offset = qemu_timedate_diff(&now);
        break;

    case MONTH:
    case EXT_MONTH:
        now.tm_mon = from_bcd(data & 0x1f) - 1;
        s->offset = qemu_timedate_diff(&now);
        break;

    case YEAR:
    case EXT_YEAR:
        now.tm_year = from_bcd(data) + 100;
        s->offset = qemu_timedate_diff(&now);
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
        break;

    case CONTROL_REGISTER:
    case EXT_CONTROL_REGISTER:
        update_control_register(s, data);
        break;

    default:
        s->nvram[s->ptr] = data;
    }

    inc_regptr(s);
    return 0;
}

/**
 * Get the device temperature in Celcius as a property
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
    double value = (s->nvram[TEMPERATURE] * 2.0f - 187.1f) / 3.218f;

    TRACE(s->parent_obj, "Read temperature property, 0x%x = %f°C",
            s->nvram[TEMPERATURE], value);

    visit_type_number(v, name, &value, errp);
}

/**
 * Set the device temperature in Celcius as a property
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
    double temp; /* degrees Celcius */
    visit_type_number(v, name, &temp, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (temp >= 100 || temp < -58) {
        error_setg(errp, "value %f°C is out of range", temp);
        return;
    }

    s->nvram[TEMPERATURE] = (uint8_t) ((temp * 3.218f + 187.19f) / 2);

    TRACE(s->parent_obj, "Set temperature property, 0x%x = %f°C",
            s->nvram[TEMPERATURE], temp);
}


/**
 * Initialize the device
 * @param i2c the i2c device instance
 */
static int rx8900_init(I2CSlave *i2c)
{
    TRACE(*i2c, "Initialized");

    return 0;
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
}

/**
 * Reset the device
 * @param dev the RX8900 device to reset
 */
static void rx8900_reset(DeviceState *dev)
{
    RX8900State *s = RX8900(dev);

    TRACE(s->parent_obj, "Reset");

    /* The clock is running and synchronized with the host */
    s->offset = 0;
    s->weekday = 7; /* Set to an invalid value */

    /* Temperature formulation from the datasheet
     * ( TEMP[ 7:0 ] * 2 - 187.19) / 3.218
     *
     * Set the initial state to 25 degrees Celcius
     */
    s->nvram[TEMPERATURE] = 135; /* (25 * 3.218 + 187.19) / 2 */

    s->nvram[EXTENSION_REGISTER] = EXT_MASK_TSEL1;
    s->nvram[CONTROL_REGISTER] = CTRL_MASK_CSEL0;
    s->nvram[FLAG_REGISTER] = FLAG_MASK_VLF | FLAG_MASK_VDET;

    s->ptr = 0;
    TRACE(s->parent_obj, "Operating on register 0x%02x", s->ptr);

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
    char name[64];

    s->fout = false;

    memset(s->nvram, 0, RX8900_NVRAM_SIZE);

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
    ptimer_set_freq(s->countdown_timer, 4096);
    ptimer_set_limit(s->countdown_timer, 4096, 1);


    snprintf(name, sizeof(name), "rx8900-interrupt-out");
    qdev_init_gpio_out_named(&i2c->qdev, &s->interrupt_pin, name, 1);
    TRACE(s->parent_obj, "Interrupt pin is '%s'", name);

    snprintf(name, sizeof(name), "rx8900-fout-enable");
    qdev_init_gpio_in_named(&i2c->qdev, rx8900_fout_enable_handler, name, 1);
    TRACE(s->parent_obj, "Fout-enable pin is '%s'", name);

    snprintf(name, sizeof(name), "rx8900-fout");
    qdev_init_gpio_out_named(&i2c->qdev, &s->fout_pin, name, 1);
    TRACE(s->parent_obj, "Fout pin is '%s'", name);
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

    k->init = rx8900_init;
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
    log = getenv("RX8900_TRACE") != NULL;
    type_register_static(&rx8900_info);
}

type_init(rx8900_register_types)
