/*
 * QTest testcase for the DS1338 RTC
 *
 * Copyright (c) 2013 Jean-Christophe Dubois
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/bcd.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "libqos/i2c.h"
#include "qobject/qdict.h"

#define DS1338_ADDR 0x68

/* DS1338 register map */
#define DS1338_SECONDS   0x00
#define DS1338_YEAR      0x06
#define DS1338_CONTROL   0x07
#define DS1338_NVRAM     0x08
#define DS1338_NUM_REGS  0x40

#define DS1338_CTRL_OSF  0x20
#define DS1338_CTRL_POR  0xb3
#define DS1338_SEC_CH    0x80

/* The clock and calendar come up on the host time. */
static void send_and_receive(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    uint8_t resp[7];
    time_t now = time(NULL);
    struct tm *tm_ptr = gmtime(&now);

    i2c_read_block(i2cdev, DS1338_SECONDS, resp, sizeof(resp));

    /* check retrieved time against local time */
    g_assert_cmpuint(from_bcd(resp[4]), == , tm_ptr->tm_mday);
    g_assert_cmpuint(from_bcd(resp[5]), == , 1 + tm_ptr->tm_mon);
    g_assert_cmpuint(2000 + from_bcd(resp[6]), == , 1900 + tm_ptr->tm_year);
}

/* The control register comes up in its documented power-on state. */
static void test_reset_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL), ==, DS1338_CTRL_POR);
}

/* Writable control bits round-trip; the reserved ones read back zero. */
static void test_control_register(void *obj, void *data,
                                  QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, DS1338_CONTROL, 0x93);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL), ==, 0x93);

    i2c_set8(i2cdev, DS1338_CONTROL, 0x4c);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL), ==, 0x00);
}

/* The oscillator-stop flag can only be cleared by a write, never set. */
static void test_osf_write_protect(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL) & DS1338_CTRL_OSF,
                    ==, DS1338_CTRL_OSF);
    i2c_set8(i2cdev, DS1338_CONTROL, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL) & DS1338_CTRL_OSF, ==, 0);
    i2c_set8(i2cdev, DS1338_CONTROL, DS1338_CTRL_OSF);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL) & DS1338_CTRL_OSF, ==, 0);
}

/* Halting the clock freezes the counters; clearing the halt resumes them. */
static void test_clock_halt(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t frozen[7], again[7];
    unsigned idx;

    /* Clear OSF first, so that halting the clock is what sets it again. */
    i2c_set8(i2cdev, DS1338_CONTROL, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL) & DS1338_CTRL_OSF, ==, 0);

    i2c_set8(i2cdev, DS1338_SECONDS, DS1338_SEC_CH | 0x30);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_SECONDS), ==,
                    DS1338_SEC_CH | 0x30);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL) & DS1338_CTRL_OSF,
                    ==, DS1338_CTRL_OSF);

    i2c_read_block(i2cdev, DS1338_SECONDS, frozen, sizeof(frozen));
    g_usleep(1200 * 1000);
    i2c_read_block(i2cdev, DS1338_SECONDS, again, sizeof(again));
    g_assert_cmpmem(frozen, sizeof(frozen), again, sizeof(again));

    i2c_set8(i2cdev, DS1338_SECONDS, 0x30);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_SECONDS) & DS1338_SEC_CH, ==, 0);
    for (idx = 0; idx < 50; idx++) {
        if (i2c_get8(i2cdev, DS1338_SECONDS) != 0x30) {
            break;
        }
        g_usleep(100 * 1000);
    }
    g_assert_cmpuint(from_bcd(i2c_get8(i2cdev, DS1338_SECONDS)), >, 30);

    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL) & DS1338_CTRL_OSF,
                    ==, DS1338_CTRL_OSF);
}

/* Reserved time-register bits read back zero even with the clock halted. */
static void test_stopped_reserved_bits(void *obj, void *data,
                                       QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    const uint8_t all_ones[7] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t resp[7];

    i2c_write_block(i2cdev, DS1338_SECONDS, all_ones, sizeof(all_ones));

    i2c_read_block(i2cdev, DS1338_SECONDS, resp, sizeof(resp));
    g_assert_cmphex(resp[0], ==, 0xff);   /* the halt bit is writable here */
    g_assert_cmphex(resp[1], ==, 0x7f);
    g_assert_cmphex(resp[2], ==, 0x7f);
    g_assert_cmphex(resp[3], ==, 0x07);
    g_assert_cmphex(resp[4], ==, 0x3f);
    g_assert_cmphex(resp[5], ==, 0x1f);   /* no century bit on this part */
    g_assert_cmphex(resp[6], ==, 0xff);
}

/* The user RAM returns what the guest wrote to it. */
static void test_nvram(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, 0x08, 0xaa);
    i2c_set8(i2cdev, 0x20, 0x55);
    i2c_set8(i2cdev, 0x3f, 0xc3);

    g_assert_cmphex(i2c_get8(i2cdev, 0x08), ==, 0xaa);
    g_assert_cmphex(i2c_get8(i2cdev, 0x20), ==, 0x55);
    g_assert_cmphex(i2c_get8(i2cdev, 0x3f), ==, 0xc3);
}

/* A reset restores the power-on state, unless persist-on-reset is set. */
static void check_reset(QI2CDevice *i2cdev, bool persists)
{
    uint8_t year = i2c_get8(i2cdev, DS1338_YEAR);
    /* A year the clock is not already showing, so the check discriminates. */
    uint8_t marker = year == 0x42 ? 0x77 : 0x42;
    QDict *rsp;

    i2c_set8(i2cdev, DS1338_NVRAM, 0xa5);
    i2c_set8(i2cdev, DS1338_CONTROL, 0x00);
    i2c_set8(i2cdev, DS1338_YEAR, marker);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_NVRAM), ==, 0xa5);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL), ==, 0x00);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_YEAR), ==, marker);

    rsp = qmp("{ 'execute': 'system_reset' }");
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    qmp_eventwait("RESET");

    if (persists) {
        g_assert_cmphex(i2c_get8(i2cdev, DS1338_NVRAM), ==, 0xa5);
        g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL), ==, 0x00);
        g_assert_cmphex(i2c_get8(i2cdev, DS1338_YEAR), ==, marker);
    } else {
        g_assert_cmphex(i2c_get8(i2cdev, DS1338_NVRAM), ==, 0x00);
        g_assert_cmphex(i2c_get8(i2cdev, DS1338_CONTROL), ==, DS1338_CTRL_POR);
        /* Back on host time, which is where the year came from. */
        g_assert_cmphex(i2c_get8(i2cdev, DS1338_YEAR), ==, year);
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

/* The register pointer wraps at the end of the map. */
static void test_address_wrap(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_set8(i2cdev, DS1338_NVRAM, 0x5a);
    g_assert_cmphex(i2c_get8(i2cdev, DS1338_NVRAM + DS1338_NUM_REGS), ==, 0x5a);
}

static void ds1338_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "address=0x68"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { DS1338_ADDR });

    qos_node_create_driver("ds1338", i2c_device_create);
    qos_node_consumes("ds1338", "i2c-bus", &opts);

    qos_add_test("tx-rx", "ds1338", send_and_receive, NULL);
    qos_add_test("reset-defaults", "ds1338", test_reset_defaults, NULL);
    qos_add_test("control-register", "ds1338", test_control_register, NULL);
    qos_add_test("osf-write-protect", "ds1338", test_osf_write_protect, NULL);
    qos_add_test("clock-halt", "ds1338", test_clock_halt, NULL);
    qos_add_test("stopped-reserved-bits", "ds1338",
                 test_stopped_reserved_bits, NULL);
    qos_add_test("nvram", "ds1338", test_nvram, NULL);
    qos_add_test("address-wrap", "ds1338", test_address_wrap, NULL);
    qos_add_test("reset-clears", "ds1338", test_reset_clears, NULL);

    opts.extra_device_opts = "address=0x68,persist-on-reset=on";
    qos_node_create_driver_named("ds1338-persist", "ds1338", i2c_device_create);
    qos_node_consumes("ds1338-persist", "i2c-bus", &opts);

    qos_add_test("reset-persists", "ds1338-persist", test_reset_persists, NULL);
}

libqos_init(ds1338_register_nodes);
