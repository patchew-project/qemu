/*
 * QTests for the PCF8563 RTC
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "libqtest-single.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"
#include "sysemu/rtc.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnum.h"
#include "qemu/bitops.h"
#include "qemu/bcd.h"

#define TEST_ID "pcf8563-test"
#define NANOSECONDS_PER_SECOND 1000000000LL

#define  PCF8563_CS1            0x00
#define  PCF8563_CS2            0x01
#define  PCF8563_VLS            0x02
#define  PCF8563_MINUTES        0x03
#define  PCF8563_HOURS          0x04
#define  PCF8563_DAYS           0x05
#define  PCF8563_WEEKDAYS       0x06
#define  PCF8563_CENTURY_MONTHS 0x07
#define  PCF8563_YEARS          0x08
#define  PCF8563_MINUTE_A       0x09
#define  PCF8563_HOUR_A         0x0A
#define  PCF8563_DAY_A          0x0B
#define  PCF8563_WEEKDAY_A      0x0C
#define  PCF8563_CLKOUT_CTL     0x0D
#define  PCF8563_TIMER_CTL      0x0E
#define  PCF8563_TIMER          0x0F

static void set_time(QI2CDevice *i2cdev, struct tm *tm)
{
    tm->tm_sec = 30;
    tm->tm_min = 45;
    tm->tm_hour = 14;
    tm->tm_mday = 25;
    tm->tm_mon = 11;
    tm->tm_year = 125;
    tm->tm_wday = 1;

    i2c_set8(i2cdev, PCF8563_VLS, to_bcd(tm->tm_sec));
    i2c_set8(i2cdev, PCF8563_MINUTES, to_bcd(tm->tm_min));
    i2c_set8(i2cdev, PCF8563_HOURS, to_bcd(tm->tm_hour));
    i2c_set8(i2cdev, PCF8563_DAYS, to_bcd(tm->tm_mday));
    i2c_set8(i2cdev, PCF8563_CENTURY_MONTHS, to_bcd(tm->tm_mon));
    i2c_set8(i2cdev, PCF8563_YEARS, to_bcd(tm->tm_year));
    i2c_set8(i2cdev, PCF8563_WEEKDAYS, to_bcd(tm->tm_wday));
}

static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    /* CS1 */
    i2c_value = i2c_get8(i2cdev, PCF8563_CS1);
    g_assert_cmphex(i2c_value, ==, 0x8);

    /* CS2 */
    i2c_value = i2c_get8(i2cdev, PCF8563_CS2);
    g_assert_cmphex(from_bcd(i2c_value), ==, 0x0);

    /* Minute alarm */
    i2c_value = i2c_get8(i2cdev, PCF8563_MINUTE_A);
    g_assert_cmphex(i2c_value, ==, 0x80);

    /* Hour alarm */
    i2c_value = i2c_get8(i2cdev, PCF8563_HOUR_A);
    g_assert_cmphex(i2c_value, ==, 0x80);

    /* Day alarm */
    i2c_value = i2c_get8(i2cdev, PCF8563_DAY_A);
    g_assert_cmphex(i2c_value, ==, 0x80);

    /* Weekend alarm */
    i2c_value = i2c_get8(i2cdev, PCF8563_WEEKDAY_A);
    g_assert_cmphex(i2c_value, ==, 0x80);

    /* Clkout CTL */
    i2c_value = i2c_get8(i2cdev, PCF8563_CLKOUT_CTL);
    g_assert_cmphex(i2c_value, ==, 0x80);

    /* Timer CTL */
    i2c_value = i2c_get8(i2cdev, PCF8563_TIMER_CTL);
    g_assert_cmphex(i2c_value, ==, 0x3);

    /* Timer CNT */
    i2c_value = i2c_get8(i2cdev, PCF8563_TIMER);
    g_assert_cmphex(i2c_value, ==, 0x0);
}

static void test_check_time(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    struct tm tm;

    tm.tm_sec = 20;
    tm.tm_min = 48;
    tm.tm_hour = 8;
    tm.tm_mday = 24;
    tm.tm_mon = 11;
    tm.tm_year = (2024 - 1900) % 100;
    tm.tm_wday = 2;

    i2c_set8(i2cdev, PCF8563_VLS, to_bcd(tm.tm_sec));
    i2c_value = i2c_get8(i2cdev, PCF8563_VLS);
    i2c_value = extract32(i2c_value, 0, 7);
    g_assert_cmphex(from_bcd(i2c_value), ==, tm.tm_sec);

    i2c_set8(i2cdev, PCF8563_MINUTES, to_bcd(tm.tm_min));
    i2c_value = i2c_get8(i2cdev, PCF8563_MINUTES);
    i2c_value = extract32(i2c_value, 0, 7);
    g_assert_cmphex(from_bcd(i2c_value), ==, tm.tm_min);

    i2c_set8(i2cdev, PCF8563_HOURS, to_bcd(tm.tm_hour));
    i2c_value = i2c_get8(i2cdev, PCF8563_HOURS);
    i2c_value = extract32(i2c_value, 0, 6);
    g_assert_cmphex(from_bcd(i2c_value), ==, tm.tm_hour);

    i2c_set8(i2cdev, PCF8563_DAYS, to_bcd(tm.tm_mday));
    i2c_value = i2c_get8(i2cdev, PCF8563_DAYS);
    i2c_value = extract32(i2c_value, 0, 6);
    g_assert_cmphex(from_bcd(i2c_value), ==, tm.tm_mday);

    i2c_set8(i2cdev, PCF8563_CENTURY_MONTHS, to_bcd(tm.tm_mon));
    i2c_value = i2c_get8(i2cdev, PCF8563_CENTURY_MONTHS);
    i2c_value = extract32(i2c_value, 0, 5);
    g_assert_cmphex(from_bcd(i2c_value), ==, tm.tm_mon + 1);

    i2c_set8(i2cdev, PCF8563_YEARS, to_bcd(tm.tm_year));
    i2c_value = i2c_get8(i2cdev, PCF8563_YEARS);
    i2c_value = extract32(i2c_value, 0, 8);
    g_assert_cmphex(from_bcd(i2c_value), ==, tm.tm_year);

    i2c_set8(i2cdev, PCF8563_WEEKDAYS, to_bcd(tm.tm_wday));
    i2c_value = i2c_get8(i2cdev, PCF8563_WEEKDAYS);
    i2c_value = extract32(i2c_value, 0, 3);
    g_assert_cmphex(from_bcd(i2c_value), ==, tm.tm_wday);
}

static void test_set_minute_alarm(void *obj, void *data, QGuestAllocator *alloc)
{
    QTestState *qts = global_qtest;
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t irq_line = 0;
    uint64_t alarm_min = 1;
    uint64_t alarm_sec = alarm_min * 60;
    uint8_t reg_min_a = 0x0;
    struct tm tm;

    qtest_irq_intercept_out(qts, "/machine/peripheral/pcf8563-test/");
    set_time(i2cdev, &tm);

    /* Enable alarm interrupt */
    i2c_set8(i2cdev, PCF8563_CS2, 0x2);

    /* Set minute alarm & enable it */
    alarm_min = tm.tm_min + alarm_min;
    reg_min_a = to_bcd(alarm_min) & ~0x80;
    i2c_set8(i2cdev, PCF8563_MINUTE_A, reg_min_a);

    /* Check when half of supposed alarm time passed */
    clock_step((alarm_sec - 1) * NANOSECONDS_PER_SECOND);

    g_assert_false(qtest_get_irq(global_qtest, irq_line));

    /* Check when alarm time passed */
    clock_step((2) * NANOSECONDS_PER_SECOND);

    i2c_value = i2c_get8(i2cdev, PCF8563_MINUTE_A);
    g_assert_cmphex(i2c_value, ==, reg_min_a);
    g_assert_true(qtest_get_irq(global_qtest, irq_line));
}

static void test_set_hour_alarm(void *obj, void *data, QGuestAllocator *alloc)
{
    QTestState *qts = global_qtest;
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t irq_line = 0;
    uint64_t alarm_hour = 3;
    uint64_t alarm_sec = alarm_hour * 60 * 60;
    uint8_t reg_hour_a = 0x0;
    struct tm tm;

    qtest_irq_intercept_out(qts, "/machine/peripheral/pcf8563-test/");
    set_time(i2cdev, &tm);

    /* Enable alarm interrupt */
    i2c_set8(i2cdev, PCF8563_CS2, 0x2);

    /* Set hour alarm & enable it */
    alarm_hour = tm.tm_hour + alarm_hour;
    reg_hour_a = to_bcd(alarm_hour) & ~0x80;
    i2c_set8(i2cdev, PCF8563_HOUR_A, reg_hour_a);

    /* Check when half of supposed alarm time passed */
    clock_step((alarm_sec - 1) * NANOSECONDS_PER_SECOND);

    g_assert_false(qtest_get_irq(qts, irq_line));

    /* Check when alarm time passed */
    clock_step((2) * NANOSECONDS_PER_SECOND);

    i2c_value = i2c_get8(i2cdev, PCF8563_HOUR_A);
    g_assert_cmphex(i2c_value, ==, reg_hour_a);
    g_assert_true(qtest_get_irq(qts, irq_line));
}

static void test_set_day_alarm(void *obj, void *data, QGuestAllocator *alloc)
{
    QTestState *qts = global_qtest;
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t irq_line = 0;
    uint64_t alarm_day = 1;
    uint64_t alarm_sec = alarm_day * 24 * 60 * 60;
    uint8_t reg_day_a = 0x0;
    struct tm tm;

    qtest_irq_intercept_out(qts, "/machine/peripheral/pcf8563-test/");
    set_time(i2cdev, &tm);

    /* Enable alarm interrupt */
    i2c_set8(i2cdev, PCF8563_CS2, 0x2);

    /* Set hour alarm & enable it */
    alarm_day = tm.tm_mday + alarm_day;
    reg_day_a = to_bcd(alarm_day) & ~0x80;
    i2c_set8(i2cdev, PCF8563_DAY_A, reg_day_a);

    /* Check when half of supposed alarm time passed */
    clock_step((alarm_sec - 1) * NANOSECONDS_PER_SECOND);

    g_assert_false(qtest_get_irq(qts, irq_line));

    /* Check when alarm time passed */
    clock_step((2) * NANOSECONDS_PER_SECOND);

    i2c_value = i2c_get8(i2cdev, PCF8563_DAY_A);
    g_assert_cmphex(i2c_value, ==, reg_day_a);
    g_assert_true(qtest_get_irq(qts, irq_line));
}

static void test_set_wday_alarm(void *obj, void *data, QGuestAllocator *alloc)
{
    QTestState *qts = global_qtest;
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t irq_line = 0;
    uint64_t alarm_wday = 1;
    uint64_t alarm_sec = alarm_wday * 24 * 60 * 60;
    uint8_t reg_wday_a = 0x0;
    struct tm tm;

    qtest_irq_intercept_out(qts, "/machine/peripheral/pcf8563-test/");
    set_time(i2cdev, &tm);

    /* Enable alarm interrupt */
    i2c_set8(i2cdev, PCF8563_CS2, 0x2);

    /* Set hour alarm & enable it */
    alarm_wday = tm.tm_wday + alarm_wday;
    reg_wday_a = to_bcd(alarm_wday) & ~0x80;
    i2c_set8(i2cdev, PCF8563_WEEKDAY_A, reg_wday_a);

    /* Check when half of supposed alarm time passed */
    clock_step((alarm_sec - 1) * NANOSECONDS_PER_SECOND);

    g_assert_false(qtest_get_irq(qts, irq_line));

    /* Check when alarm time passed */
    clock_step((2) * NANOSECONDS_PER_SECOND);

    i2c_value = i2c_get8(i2cdev, PCF8563_WEEKDAY_A);
    g_assert_cmphex(i2c_value, ==, reg_wday_a);
    g_assert_true(qtest_get_irq(qts, irq_line));
}

static void test_set_min_and_hour_alarm(void *obj,
                                        void *data,
                                        QGuestAllocator *alloc)
{
    QTestState *qts = global_qtest;
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t irq_line = 0;
    uint64_t alarm_min = 1;
    uint8_t reg_min_a = 0x0;
    uint64_t alarm_hour = 3;
    uint8_t reg_hour_a = 0x0;
    uint64_t alarm_sec = (alarm_hour * 60 * 60) + (alarm_min * 60);
    struct tm tm;

    qtest_irq_intercept_out(qts, "/machine/peripheral/pcf8563-test/");
    set_time(i2cdev, &tm);

    /* Enable alarm interrupt */
    i2c_set8(i2cdev, PCF8563_CS2, 0x2);

    /* Set hour alarm & enable it */
    alarm_hour = tm.tm_hour + alarm_hour;
    reg_hour_a = to_bcd(alarm_hour) & ~0x80;
    i2c_set8(i2cdev, PCF8563_HOUR_A, reg_hour_a);

    /* Set minute alarm & enable it */
    alarm_min = tm.tm_min + alarm_min;
    reg_min_a = to_bcd(alarm_min) & ~0x80;
    i2c_set8(i2cdev, PCF8563_MINUTE_A, reg_min_a);

    /* Check when half of supposed alarm time passed */
    clock_step((alarm_sec - 1) * NANOSECONDS_PER_SECOND);

    g_assert_false(qtest_get_irq(global_qtest, irq_line));

    /* Check when alarm time passed */
    clock_step((2) * NANOSECONDS_PER_SECOND);

    i2c_value = i2c_get8(i2cdev, PCF8563_MINUTE_A);
    g_assert_cmphex(i2c_value, ==, reg_min_a);
    g_assert_true(qtest_get_irq(global_qtest, irq_line));
}

static void test_set_min_and_day_alarm(void *obj,
                                       void *data,
                                       QGuestAllocator *alloc)
{
    QTestState *qts = global_qtest;
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t irq_line = 0;
    uint64_t alarm_min = 1;
    uint8_t reg_min_a = 0x0;
    uint64_t alarm_day = 2;
    uint8_t reg_day_a = 0x0;
    uint64_t alarm_sec = (alarm_day * 24 * 60 * 60) + (alarm_min * 60);
    struct tm tm;

    qtest_irq_intercept_out(qts, "/machine/peripheral/pcf8563-test/");
    set_time(i2cdev, &tm);

    /* Enable alarm interrupt */
    i2c_set8(i2cdev, PCF8563_CS2, 0x2);

    /* Set day alarm & enable it */
    alarm_day = tm.tm_mday + alarm_day;
    reg_day_a = to_bcd(alarm_day) & ~0x80;
    i2c_set8(i2cdev, PCF8563_DAY_A, reg_day_a);

    /* Set minute alarm & enable it */
    alarm_min = tm.tm_min + alarm_min;
    reg_min_a = to_bcd(alarm_min) & ~0x80;
    i2c_set8(i2cdev, PCF8563_MINUTE_A, reg_min_a);

    /* Check when half of supposed alarm time passed */
    clock_step((alarm_sec - 1) * NANOSECONDS_PER_SECOND);

    g_assert_false(qtest_get_irq(global_qtest, irq_line));

    /* Check when alarm time passed */
    clock_step((2) * NANOSECONDS_PER_SECOND);

    i2c_value = i2c_get8(i2cdev, PCF8563_MINUTE_A);
    g_assert_cmphex(i2c_value, ==, reg_min_a);
    g_assert_true(qtest_get_irq(global_qtest, irq_line));
}

static void test_set_day_and_wday_alarm(void *obj,
                                        void *data,
                                        QGuestAllocator *alloc)
{
    QTestState *qts = global_qtest;
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t irq_line = 0;
    uint64_t alarm_day = 1;
    uint8_t reg_day_a = 0x0;
    uint64_t alarm_wday = 2;
    uint8_t reg_wday_a = 0x0;
    uint64_t alarm_sec = (alarm_day * 24 * 60 * 60) +
                         (alarm_wday * 24 * 60 * 60);
    struct tm tm;

    qtest_irq_intercept_out(qts, "/machine/peripheral/pcf8563-test/");
    set_time(i2cdev, &tm);

    /* Enable alarm interrupt */
    i2c_set8(i2cdev, PCF8563_CS2, 0x2);

    /* Set day alarm & enable it */
    alarm_day = tm.tm_mday + alarm_day;
    reg_day_a = to_bcd(alarm_day) & ~0x80;
    i2c_set8(i2cdev, PCF8563_DAY_A, reg_day_a);

    /* Set wday alarm & enable it */
    alarm_wday = tm.tm_wday + alarm_wday;
    reg_wday_a = to_bcd(alarm_wday) & ~0x80;
    i2c_set8(i2cdev, PCF8563_WEEKDAY_A, reg_wday_a);

    /* Check when half of supposed alarm time passed */
    clock_step((alarm_sec - 1) * NANOSECONDS_PER_SECOND);

    g_assert_false(qtest_get_irq(global_qtest, irq_line));

    /* Check when alarm time passed */
    clock_step((2) * NANOSECONDS_PER_SECOND);

    i2c_value = i2c_get8(i2cdev, PCF8563_WEEKDAY_A);
    g_assert_cmphex(i2c_value, ==, reg_wday_a);
    g_assert_true(qtest_get_irq(global_qtest, irq_line));
}

static void test_set_timer(void *obj, void *data, QGuestAllocator *alloc)
{
    QTestState *qts = global_qtest;
    uint16_t i2c_value = 0;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t irq_line = 0;
    uint64_t src_clk_freq = 64;
    uint8_t reg_timer_ctl = 0x81;
    uint8_t reg_timer = 0xff;
    uint64_t countdown_period = reg_timer / src_clk_freq;

    qtest_irq_intercept_out(qts, "/machine/peripheral/pcf8563-test/");
    g_assert_false(qtest_get_irq(global_qtest, irq_line));

    i2c_set8(i2cdev, PCF8563_CS2, 0x1);
    i2c_set8(i2cdev, PCF8563_TIMER_CTL, reg_timer_ctl);
    i2c_set8(i2cdev, PCF8563_TIMER, reg_timer);

    clock_step((countdown_period - 1) * NANOSECONDS_PER_SECOND);

    g_assert_false(qtest_get_irq(global_qtest, irq_line));

    clock_step((2) * NANOSECONDS_PER_SECOND);

    i2c_value = i2c_get8(i2cdev, PCF8563_TIMER_CTL);
    g_assert_cmphex(i2c_value, ==, reg_timer_ctl);
    i2c_value = i2c_get8(i2cdev, PCF8563_TIMER);
    g_assert_cmphex(i2c_value, ==, 0);
    g_assert_true(qtest_get_irq(global_qtest, irq_line));
}

static void pcf8563_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" TEST_ID ",address=0x10",
        .before_cmd_line = "-rtc clock=vm"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { 0x10 });

    qos_node_create_driver("pcf8563", i2c_device_create);

    qos_node_consumes("pcf8563", "i2c-bus", &opts);

    qos_add_test("test_defaults",
                 "pcf8563",
                 test_defaults,
                 NULL);
    qos_add_test("test_check_time",
                 "pcf8563",
                 test_check_time,
                 NULL);
    qos_add_test("test_set_minute_alarm",
                 "pcf8563",
                 test_set_minute_alarm,
                 NULL);
    qos_add_test("test_set_hour_alarm",
                 "pcf8563",
                 test_set_hour_alarm,
                 NULL);
    qos_add_test("test_set_day_alarm",
                 "pcf8563",
                 test_set_day_alarm,
                 NULL);
    qos_add_test("test_set_wday_alarm",
                 "pcf8563",
                 test_set_wday_alarm,
                 NULL);
    qos_add_test("test_set_min_and_hour_alarm",
                 "pcf8563",
                 test_set_min_and_hour_alarm,
                 NULL);
    qos_add_test("test_set_min_and_day_alarm",
                 "pcf8563",
                 test_set_min_and_day_alarm,
                 NULL);
    qos_add_test("test_set_day_and_wday_alarm",
                 "pcf8563",
                 test_set_day_and_wday_alarm,
                 NULL);
    qos_add_test("test_set_timer",
                 "pcf8563",
                 test_set_timer,
                 NULL);
}
libqos_init(pcf8563_register_nodes);
