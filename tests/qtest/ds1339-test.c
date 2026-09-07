/*
 * QTest testcase for the DS1339 RTC
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bcd.h"
#include "libqos/i2c.h"

#define DS1339_ADDR 0x68

/* DS1339 register map */
#define DS1339_SECONDS   0x00
#define DS1339_MONTH     0x05
#define DS1339_YEAR      0x06
#define DS1339_ALARM1    0x07
#define DS1339_ALARM2    0x0b
#define DS1339_CONTROL   0x0e
#define DS1339_STATUS    0x0f
#define DS1339_TRICKLE   0x10
#define DS1339_NUM_REGS  0x11

#define DS1339_STATUS_OSF 0x80

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
    qos_add_test("stopped-reserved-bits", "ds1339",
                 test_stopped_reserved_bits, NULL);
    qos_add_test("address-wrap", "ds1339", test_address_wrap, NULL);
    qos_add_test("block-wrap", "ds1339", test_block_wrap, NULL);
    qos_add_test("alarm-registers", "ds1339", test_alarm_registers,
                 NULL);
}

libqos_init(ds1339_register_nodes);
