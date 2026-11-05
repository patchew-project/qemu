/*
 * QTest testcase for the DS1339 RTC
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bcd.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "libqos/i2c.h"
#include "qobject/qdict.h"

#define DS1339_ADDR 0x68

/* DS1339 register map */
#define DS1339_SECONDS   0x00
#define DS1339_HOURS     0x02
#define DS1339_DAY       0x03
#define DS1339_DATE      0x04
#define DS1339_MONTH     0x05
#define DS1339_YEAR      0x06
#define DS1339_ALARM1    0x07
#define DS1339_ALARM2    0x0b
#define DS1339_CONTROL   0x0e
#define DS1339_STATUS    0x0f
#define DS1339_TRICKLE   0x10
#define DS1339_NUM_REGS  0x11

#define DS1339_STATUS_OSF 0x80

#define DS1339_HOURS_12  0x40
#define DS1339_HOURS_PM  0x20

/* The clock and calendar come up on the host time. */
static void test_time(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t resp[7];
    time_t now = time(NULL);
    struct tm *tm_ptr = gmtime(&now);

    i2c_read_block(i2cdev, DS1339_SECONDS, resp, sizeof(resp));

    g_assert_cmpuint(from_bcd(resp[4]), ==, tm_ptr->tm_mday);
    g_assert_cmpuint(from_bcd(resp[5]), ==, 1 + tm_ptr->tm_mon);
    g_assert_cmpuint(2000 + from_bcd(resp[6]), ==, 1900 + tm_ptr->tm_year);
}

/* Writable control bits round-trip; the reserved one reads back zero. */
static void test_control_register(void *obj, void *data,
                                  QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, DS1339_CONTROL, 0x1c);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_CONTROL), ==, 0x1c);

    i2c_set8(i2cdev, DS1339_CONTROL, 0xff);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_CONTROL), ==, 0xbf);

    i2c_set8(i2cdev, DS1339_CONTROL, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_CONTROL), ==, 0x00);
}

/* The oscillator-stop flag can only be cleared by a write, never set. */
static void test_osf_write_protect(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS) & DS1339_STATUS_OSF,
                    ==, DS1339_STATUS_OSF);
    i2c_set8(i2cdev, DS1339_STATUS, DS1339_STATUS_OSF);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS) & DS1339_STATUS_OSF,
                    ==, DS1339_STATUS_OSF);

    i2c_set8(i2cdev, DS1339_STATUS, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS) & DS1339_STATUS_OSF, ==, 0);
    i2c_set8(i2cdev, DS1339_STATUS, DS1339_STATUS_OSF);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS) & DS1339_STATUS_OSF, ==, 0);
}

/* The control and status registers come up in their power-on state. */
static void test_reset_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(i2cdev, DS1339_CONTROL), ==, 0x18);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS) & DS1339_STATUS_OSF,
                    ==, DS1339_STATUS_OSF);
}

/* Neither the reserved status bits nor the alarm flags are guest-writable. */
static void test_status_register(void *obj, void *data,
                                 QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, DS1339_STATUS, 0x7c);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS), ==, 0x00);

    i2c_set8(i2cdev, DS1339_STATUS, 0x03);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS), ==, 0x00);

    i2c_set8(i2cdev, DS1339_STATUS, 0xff);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS), ==, 0x00);
}

/* Reserved time-register bits read back zero even with the clock stopped. */
static void test_stopped_reserved_bits(void *obj, void *data,
                                       QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    const uint8_t all_ones[7] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t resp[7];

    i2c_set8(i2cdev, DS1339_CONTROL, 0x18 | 0x80);
    i2c_write_block(i2cdev, DS1339_SECONDS, all_ones, sizeof(all_ones));

    i2c_read_block(i2cdev, DS1339_SECONDS, resp, sizeof(resp));
    g_assert_cmphex(resp[0], ==, 0x7f);
    g_assert_cmphex(resp[1], ==, 0x7f);
    g_assert_cmphex(resp[2], ==, 0x7f);
    g_assert_cmphex(resp[3], ==, 0x07);
    g_assert_cmphex(resp[4], ==, 0x3f);
    g_assert_cmphex(resp[5], ==, 0x9f);
    g_assert_cmphex(resp[6], ==, 0xff);
}

/* Setting EOSC stops the oscillator and sets OSF; OSF outlives the restart. */
static void test_eosc(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, DS1339_STATUS, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS) & DS1339_STATUS_OSF, ==, 0);

    i2c_set8(i2cdev, DS1339_CONTROL, 0x18 | 0x80);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS) & DS1339_STATUS_OSF,
                    ==, DS1339_STATUS_OSF);

    i2c_set8(i2cdev, DS1339_CONTROL, 0x18);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS) & DS1339_STATUS_OSF,
                    ==, DS1339_STATUS_OSF);
    i2c_set8(i2cdev, DS1339_STATUS, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS) & DS1339_STATUS_OSF, ==, 0);
}

/* A stopped clock resumes from the time it was programmed with. */
static void test_eosc_restart(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    /* 10:20:30 on the 12th of July 2031, the day counter reading 2. */
    const uint8_t tod[7] = { 0x30, 0x20, 0x10, 0x02, 0x12, 0x07, 0x31 };
    uint8_t resp[7];
    unsigned idx;

    i2c_set8(i2cdev, DS1339_CONTROL, 0x18 | 0x80);
    i2c_write_block(i2cdev, DS1339_SECONDS, tod, sizeof(tod));

    i2c_read_block(i2cdev, DS1339_SECONDS, resp, sizeof(resp));
    g_assert_cmpmem(resp, sizeof(resp), tod, sizeof(tod));

    i2c_set8(i2cdev, DS1339_CONTROL, 0x18);
    /* The seconds counter steps within a second of the restart. */
    for (idx = 0; idx < 15 && i2c_get8(i2cdev, DS1339_SECONDS) == 0x30; idx++) {
        g_usleep(100 * 1000);
    }

    i2c_read_block(i2cdev, DS1339_SECONDS, resp, sizeof(resp));
    g_assert_cmpuint(from_bcd(resp[0]), >, 30);
    g_assert_cmphex(resp[2], ==, 0x10);
    g_assert_cmphex(resp[4], ==, 0x12);
    g_assert_cmphex(resp[5] & 0x1f, ==, 0x07);
    g_assert_cmphex(resp[6], ==, 0x31);
}

/* The day counter is the guest's own numbering, and steps at midnight. */
static void test_day_of_week(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    /* 23:59:59 on 14 June 2024, the day counter reading 7. */
    const uint8_t tod[7] = { 0x59, 0x59, 0x23, 0x07, 0x14, 0x06, 0x24 };
    unsigned idx;

    for (idx = 1; idx <= 7; idx++) {
        i2c_set8(i2cdev, DS1339_DAY, idx);
        g_assert_cmphex(i2c_get8(i2cdev, DS1339_DAY), ==, idx);
    }

    i2c_set8(i2cdev, DS1339_DAY, 0x03);
    i2c_set8(i2cdev, DS1339_DATE, 0x14);
    i2c_set8(i2cdev, DS1339_MONTH, 0x06);
    i2c_set8(i2cdev, DS1339_YEAR, 0x24);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_DAY), ==, 0x03);

    i2c_write_block(i2cdev, DS1339_SECONDS, tod, sizeof(tod));
    for (idx = 0; idx < 50; idx++) {
        if (i2c_get8(i2cdev, DS1339_DAY) != 0x07) {
            break;
        }
        g_usleep(100 * 1000);
    }
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_DAY), ==, 0x01);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_DATE), ==, 0x15);
}

/* Midnight and noon are the hours the 12-hour encoding special-cases. */
static void test_hour_mode_12(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    const uint8_t tod[3] = {
        0x59, 0x59, DS1339_HOURS_12 | DS1339_HOURS_PM | 0x11 /* 11:59:59 PM */
    };
    unsigned idx;

    i2c_set8(i2cdev, DS1339_HOURS, DS1339_HOURS_12 | 0x12);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_HOURS), ==,
                    DS1339_HOURS_12 | 0x12);          /* 12 AM is midnight */

    i2c_set8(i2cdev, DS1339_HOURS, DS1339_HOURS_12 | DS1339_HOURS_PM | 0x12);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_HOURS), ==,
                    DS1339_HOURS_12 | DS1339_HOURS_PM | 0x12); /* 12 PM noon */

    i2c_set8(i2cdev, DS1339_HOURS, DS1339_HOURS_12 | DS1339_HOURS_PM | 0x01);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_HOURS), ==,
                    DS1339_HOURS_12 | DS1339_HOURS_PM | 0x01);

    i2c_set8(i2cdev, DS1339_HOURS, DS1339_HOURS_12 | 0x11);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_HOURS), ==,
                    DS1339_HOURS_12 | 0x11);

    i2c_set8(i2cdev, DS1339_HOURS, 0x13);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_HOURS), ==, 0x13);

    /* An elapsed midnight moves 11 PM on to 12 AM, not to 00 or 13. */
    i2c_write_block(i2cdev, DS1339_SECONDS, tod, sizeof(tod));
    for (idx = 0; idx < 50; idx++) {
        if (i2c_get8(i2cdev, DS1339_HOURS) != tod[2]) {
            break;
        }
        g_usleep(100 * 1000);
    }
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_HOURS), ==,
                    DS1339_HOURS_12 | 0x12);
}

/* Program 23:59:59 on the last day of a month and run the clock past it. */
static void check_month_rollover(QI2CDevice *i2cdev,
                                 uint8_t date, uint8_t month, uint8_t year,
                                 uint8_t next_date, uint8_t next_month,
                                 uint8_t next_year)
{
    const uint8_t tod[7] = { 0x59, 0x59, 0x23, 0x01, date, month, year };
    uint8_t resp[7];
    unsigned idx;

    i2c_write_block(i2cdev, DS1339_SECONDS, tod, sizeof(tod));

    for (idx = 0; idx < 50; idx++) {
        if (i2c_get8(i2cdev, DS1339_DATE) != date) {
            break;
        }
        g_usleep(100 * 1000);
    }

    i2c_read_block(i2cdev, DS1339_SECONDS, resp, sizeof(resp));
    g_assert_cmphex(resp[4], ==, next_date);
    g_assert_cmphex(resp[5] & 0x1f, ==, next_month);
    g_assert_cmphex(resp[6], ==, next_year);
}

/* A 31-day month rolls over to the first of the next. */
static void test_month_31_days(void *obj, void *data, QGuestAllocator *alloc)
{
    check_month_rollover((QI2CDevice *)obj,
                         0x31, 0x01, 0x23, 0x01, 0x02, 0x23);
}

/* A 30-day month rolls over to the first of the next. */
static void test_month_30_days(void *obj, void *data, QGuestAllocator *alloc)
{
    check_month_rollover((QI2CDevice *)obj,
                         0x30, 0x04, 0x23, 0x01, 0x05, 0x23);
}

/* Outside a leap year, February rolls over after the 28th. */
static void test_month_28_days(void *obj, void *data, QGuestAllocator *alloc)
{
    check_month_rollover((QI2CDevice *)obj,
                         0x28, 0x02, 0x23, 0x01, 0x03, 0x23);
}

/* In a leap year the 28th is not the last day of February. */
static void test_month_29_days(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    check_month_rollover(i2cdev, 0x28, 0x02, 0x24, 0x29, 0x02, 0x24);
    check_month_rollover(i2cdev, 0x29, 0x02, 0x24, 0x01, 0x03, 0x24);
}

/* The Century bit is guest-writable and survives an oscillator restart. */
static void test_century_bit(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, DS1339_MONTH, 0x86); /* Century | June */
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_MONTH) & 0x80, ==, 0x80);

    i2c_set8(i2cdev, DS1339_CONTROL, 0x18 | 0x80);
    i2c_set8(i2cdev, DS1339_CONTROL, 0x18);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_MONTH) & 0x80, ==, 0x80);

    i2c_set8(i2cdev, DS1339_MONTH, 0x06); /* June, century clear */
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_MONTH) & 0x80, ==, 0x00);
}

/* A guest write never toggles the Century bit. */
static void test_century_write_no_toggle(void *obj, void *data,
                                         QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, DS1339_MONTH, 0x86);
    i2c_set8(i2cdev, DS1339_YEAR, 0x99);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_MONTH) & 0x80, ==, 0x80);
    i2c_set8(i2cdev, DS1339_YEAR, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_MONTH) & 0x80, ==, 0x80);

    i2c_set8(i2cdev, DS1339_MONTH, 0x06);
    i2c_set8(i2cdev, DS1339_YEAR, 0x99);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_MONTH) & 0x80, ==, 0x00);
    i2c_set8(i2cdev, DS1339_YEAR, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_MONTH) & 0x80, ==, 0x00);
}

/* A clock-driven year rollover from 99 to 00 toggles the Century bit. */
static void test_century_rollover(void *obj, void *data,
                                  QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    const uint8_t tod[7] = { 0x59, 0x59, 0x23, 0x01, 0x31, 0x12, 0x99 };
    unsigned idx;

    i2c_write_block(i2cdev, DS1339_SECONDS, tod, sizeof(tod));

    for (idx = 0; idx < 50; idx++) {
        if (i2c_get8(i2cdev, DS1339_YEAR) == 0x00) {
            break;
        }
        g_usleep(100 * 1000);
    }

    g_assert_cmphex(i2c_get8(i2cdev, DS1339_YEAR), ==, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_MONTH) & 0x80, ==, 0x80);
}

/* The register pointer wraps at the end of the map. */
static void test_address_wrap(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, DS1339_CONTROL, 0x2a);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_CONTROL + DS1339_NUM_REGS),
                    ==, 0x2a);
}

/* A block transfer off the end of the map carries on from the start. */
static void test_block_wrap(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    /* The alarm 2 block, then on into the time registers once it wraps. */
    const uint8_t out[11] = {
        0x0b, 0x0c, 0x0d,              /* alarm 2 */
        0x18,                          /* control */
        0x00,                          /* status */
        0xa5,                          /* trickle charger */
        0x30, 0x20, 0x10, 0x05, 0x12,  /* 10:20:30, day 5, the 12th */
    };
    uint8_t resp[11];

    /* A month and year the wrapping write never reaches. */
    i2c_set8(i2cdev, DS1339_MONTH, 0x07);
    i2c_set8(i2cdev, DS1339_YEAR, 0x31);

    i2c_write_block(i2cdev, DS1339_ALARM2, out, sizeof(out));

    i2c_read_block(i2cdev, DS1339_SECONDS, resp, 7);
    g_assert_cmphex(resp[2], ==, 0x10);
    g_assert_cmphex(resp[3], ==, 0x05);
    g_assert_cmphex(resp[4], ==, 0x12);
    g_assert_cmphex(resp[5] & 0x1f, ==, 0x07);
    g_assert_cmphex(resp[6], ==, 0x31);

    i2c_read_block(i2cdev, DS1339_ALARM2, resp, sizeof(resp));
    g_assert_cmphex(resp[0], ==, 0x0b);
    g_assert_cmphex(resp[1], ==, 0x0c);
    g_assert_cmphex(resp[2], ==, 0x0d);
    g_assert_cmphex(resp[3], ==, 0x18);
    g_assert_cmphex(resp[4], ==, 0x00);
    g_assert_cmphex(resp[5], ==, 0xa5);
    g_assert_cmphex(resp[8], ==, 0x10);
    g_assert_cmphex(resp[9], ==, 0x05);
    g_assert_cmphex(resp[10], ==, 0x12);
}

/* The alarm and trickle registers are stored but never evaluated. */
static void test_alarm_registers(void *obj, void *data,
                                 QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    static const uint8_t regs[] = {
        DS1339_ALARM1, 0x08, 0x09, 0x0a,        /* alarm 1 */
        DS1339_ALARM2, 0x0c, 0x0d,              /* alarm 2 */
        DS1339_TRICKLE,
    };
    static const uint8_t vals[] = { 0xff, 0x00, 0x5a, 0xa5 };
    unsigned reg, val;

    for (reg = 0; reg < ARRAY_SIZE(regs); reg++) {
        for (val = 0; val < ARRAY_SIZE(vals); val++) {
            i2c_set8(i2cdev, regs[reg], vals[val]);
            g_assert_cmphex(i2c_get8(i2cdev, regs[reg]), ==, vals[val]);
        }
    }
}

/* A reset restores the power-on state, unless persist-on-reset is set. */
static void check_reset(QI2CDevice *i2cdev, bool persists)
{
    uint8_t year = i2c_get8(i2cdev, DS1339_YEAR);
    /* A year the clock is not already showing, so the check discriminates. */
    uint8_t marker = year == 0x42 ? 0x77 : 0x42;
    QDict *rsp;

    i2c_set8(i2cdev, DS1339_ALARM1, 0xa5);
    i2c_set8(i2cdev, DS1339_STATUS, 0x00);
    i2c_set8(i2cdev, DS1339_YEAR, marker);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_ALARM1), ==, 0xa5);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS), ==, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1339_YEAR), ==, marker);

    rsp = qmp("{ 'execute': 'system_reset' }");
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    qmp_eventwait("RESET");

    if (persists) {
        g_assert_cmphex(i2c_get8(i2cdev, DS1339_ALARM1), ==, 0xa5);
        g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS), ==, 0x00);
        g_assert_cmphex(i2c_get8(i2cdev, DS1339_YEAR), ==, marker);
    } else {
        g_assert_cmphex(i2c_get8(i2cdev, DS1339_ALARM1), ==, 0x00);
        g_assert_cmphex(i2c_get8(i2cdev, DS1339_CONTROL), ==, 0x18);
        g_assert_cmphex(i2c_get8(i2cdev, DS1339_STATUS), ==,
                        DS1339_STATUS_OSF);
        /* Back on host time, which is where the year came from. */
        g_assert_cmphex(i2c_get8(i2cdev, DS1339_YEAR), ==, year);
    }
}

/* Without persist-on-reset, a reset takes the part back to power-on. */
static void test_reset_clears(void *obj, void *data, QGuestAllocator *alloc)
{
    check_reset((QI2CDevice *)obj, false);
}

/* With persist-on-reset, the registers and the clock survive a reset. */
static void test_reset_persists(void *obj, void *data, QGuestAllocator *alloc)
{
    check_reset((QI2CDevice *)obj, true);
}

static void ds1339_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "address=0x68"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { DS1339_ADDR });

    qos_node_create_driver("ds1339", i2c_device_create);
    qos_node_consumes("ds1339", "i2c-bus", &opts);

    qos_add_test("time", "ds1339", test_time, NULL);
    qos_add_test("reset-defaults", "ds1339", test_reset_defaults, NULL);
    qos_add_test("control-register", "ds1339", test_control_register, NULL);
    qos_add_test("status-register", "ds1339", test_status_register, NULL);
    qos_add_test("osf-write-protect", "ds1339", test_osf_write_protect, NULL);
    qos_add_test("eosc", "ds1339", test_eosc, NULL);
    qos_add_test("eosc-restart", "ds1339", test_eosc_restart, NULL);
    qos_add_test("stopped-reserved-bits", "ds1339",
                 test_stopped_reserved_bits, NULL);
    qos_add_test("day-of-week", "ds1339", test_day_of_week, NULL);
    qos_add_test("hour-mode-12", "ds1339", test_hour_mode_12, NULL);
    qos_add_test("month-28-days", "ds1339", test_month_28_days, NULL);
    qos_add_test("month-29-days", "ds1339", test_month_29_days, NULL);
    qos_add_test("month-30-days", "ds1339", test_month_30_days, NULL);
    qos_add_test("month-31-days", "ds1339", test_month_31_days, NULL);
    qos_add_test("century-bit", "ds1339", test_century_bit, NULL);
    qos_add_test("century-write-no-toggle", "ds1339",
                 test_century_write_no_toggle, NULL);
    qos_add_test("century-rollover", "ds1339", test_century_rollover, NULL);
    qos_add_test("address-wrap", "ds1339", test_address_wrap, NULL);
    qos_add_test("block-wrap", "ds1339", test_block_wrap, NULL);
    qos_add_test("alarm-registers", "ds1339", test_alarm_registers,
                 NULL);
    qos_add_test("reset-clears", "ds1339", test_reset_clears, NULL);

    opts.extra_device_opts = "address=0x68,persist-on-reset=on";
    qos_node_create_driver_named("ds1339-persist", "ds1339", i2c_device_create);
    qos_node_consumes("ds1339-persist", "i2c-bus", &opts);

    qos_add_test("reset-persists", "ds1339-persist", test_reset_persists, NULL);
}

libqos_init(ds1339_register_nodes);
