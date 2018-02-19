/* Testing of Dallas/Maxim I2C bus RTC devices
 *
 * Copyright (c) 2017 Michael Davidsaver
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the LICENSE file in the top-level directory.
 */
#include <stdio.h>

#include "qemu/osdep.h"
#include "qemu/bcd.h"
#include "qemu/cutils.h"
#include "qemu/timer.h"
#include "libqtest.h"
#include "libqos/libqos.h"
#include "libqos/i2c.h"

#define IMX25_I2C_0_BASE 0x43F80000
#define DS1338_ADDR 0x68

static I2CAdapter *i2c;
static uint8_t addr;
static bool use_century;

static
time_t rtc_gettime(void)
{
    struct tm parts;
    uint8_t buf[7];

    buf[0] = 0;
    i2c_send(i2c, addr, buf, 1);
    i2c_recv(i2c, addr, buf, 7);

    parts.tm_sec = from_bcd(buf[0]);
    parts.tm_min = from_bcd(buf[1]);
    if (buf[2] & 0x40) {
        /* 12 hour */
        /* HOUR register is 1-12. */
        parts.tm_hour = from_bcd(buf[2] & 0x1f);
        g_assert_cmpuint(parts.tm_hour, >=, 1);
        g_assert_cmpuint(parts.tm_hour, <=, 12);
        parts.tm_hour %= 12u; /* wrap 12 -> 0 */
        if (buf[2] & 0x20) {
            parts.tm_hour += 12u;
        }
    } else {
        /* 24 hour */
        parts.tm_hour = from_bcd(buf[2] & 0x3f);
    }
    parts.tm_wday = from_bcd(buf[3]);
    parts.tm_mday = from_bcd(buf[4]);
    parts.tm_mon =  from_bcd((buf[5] & 0x1f) - 1u);
    parts.tm_year = from_bcd(buf[6]);
    if (!use_century || (buf[5] & 0x80)) {
        parts.tm_year += 100u;
    }

    return mktimegm(&parts);
}

/* read back and compare with current system time */
static
void test_rtc_current(void)
{
    uint8_t buf;
    time_t expected, actual;

    /* magic address to zero RTC time offset
     * as tests may be run in any order
     */
    buf = 0xff;
    i2c_send(i2c, addr, &buf, 1);

    actual = time(NULL);
    /* new second may start here */
    expected = rtc_gettime();
    g_assert_cmpuint(expected, <=, actual + 1);
    g_assert_cmpuint(expected, >=, actual);
}


static uint8_t test_time_24_12am[8] = {
    0, /* address */
    /* Wed, 22 Nov 2017 00:30:53 +0000 */
    0x53,
    0x30,
    0x00, /* 12 AM in 24 hour mode */
    0x03, /* monday is our day 1 */
    0x22,
    0x11 | 0x80,
    0x17,
};

static uint8_t test_time_12_12am[8] = {
    0, /* address */
    /* Wed, 22 Nov 2017 00:30:53 +0000 */
    0x53,
    0x30,
    0x52, /* 12 AM in 12 hour mode */
    0x03, /* monday is our day 1 */
    0x22,
    0x11 | 0x80,
    0x17,
};

static uint8_t test_time_24_6am[8] = {
    0, /* address */
    /* Wed, 22 Nov 2017 06:30:53 +0000 */
    0x53,
    0x30,
    0x06, /* 6 AM in 24 hour mode */
    0x03, /* monday is our day 1 */
    0x22,
    0x11 | 0x80,
    0x17,
};

static uint8_t test_time_12_6am[8] = {
    0, /* address */
    /* Wed, 22 Nov 2017 06:30:53 +0000 */
    0x53,
    0x30,
    0x46, /* 6 AM in 12 hour mode */
    0x03, /* monday is our day 1 */
    0x22,
    0x11 | 0x80,
    0x17,
};

static uint8_t test_time_24_12pm[8] = {
    0, /* address */
    /* Wed, 22 Nov 2017 12:30:53 +0000 */
    0x53,
    0x30,
    0x12, /* 12 PM in 24 hour mode */
    0x03, /* monday is our day 1 */
    0x22,
    0x11 | 0x80,
    0x17,
};

static uint8_t test_time_12_12pm[8] = {
    0, /* address */
    /* Wed, 22 Nov 2017 12:30:53 +0000 */
    0x53,
    0x30,
    0x72, /* 12 PM in 24 hour mode */
    0x03, /* monday is our day 1 */
    0x22,
    0x11 | 0x80,
    0x17,
};

static uint8_t test_time_24_6pm[8] = {
    0, /* address */
    /* Wed, 22 Nov 2017 18:30:53 +0000 */
    0x53,
    0x30,
    0x18, /* 6 PM in 24 hour mode */
    0x03, /* monday is our day 1 */
    0x22,
    0x11 | 0x80,
    0x17,
};

static uint8_t test_time_12_6pm[8] = {
    0, /* address */
    /* Wed, 22 Nov 2017 18:30:53 +0000 */
    0x53,
    0x30,
    0x66, /* 6 PM in 12 hour mode */
    0x03, /* monday is our day 1 */
    0x22,
    0x11 | 0x80,
    0x17,
};

/* write in and read back known time */
static
void test_rtc_set(const void *raw)
{
    const uint8_t *testtime = raw;
    uint8_t buf[7];
    unsigned retry = 2;

    for (; retry; retry--) {
        i2c_send(i2c, addr, testtime, 8);
        /* new second may start here */
        i2c_send(i2c, addr, testtime, 1);
        i2c_recv(i2c, addr, buf, 7);

        if (testtime[1] == buf[0]) {
            break;
        }
        /* we raced start of second, retry */
    };

    g_assert_cmpuint(testtime[1], ==, buf[0]); /* SEC */
    g_assert_cmpuint(testtime[2], ==, buf[1]); /* MIN */
    g_assert_cmpuint(testtime[3], ==, buf[2]); /* HOUR */
    g_assert_cmpuint(testtime[4], ==, buf[3]); /* DoW */
    g_assert_cmpuint(testtime[5], ==, buf[4]); /* DoM */
    if (use_century) {
        g_assert_cmpuint(testtime[6], ==, buf[5]); /* MON+century */
    } else {
        g_assert_cmpuint(testtime[6] & 0x7f, ==, buf[5]); /* MON */
    }
    g_assert_cmpuint(testtime[7], ==, buf[6]); /* YEAR */

    g_assert_cmpuint(retry, >, 0);
}

int main(int argc, char *argv[])
{
    int ret;
    const char *arch = qtest_get_arch();
    QTestState *s = NULL;

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "arm") == 0) {
        s = qtest_start("-display none -machine imx25-pdk");
        i2c = imx_i2c_create(s, IMX25_I2C_0_BASE);
        addr = DS1338_ADDR;
        use_century = false;

    }

    qtest_add_data_func("/ds-rtc-i2c/set24_12am", test_time_24_12am, test_rtc_set);
    qtest_add_data_func("/ds-rtc-i2c/set24_6am", test_time_24_6am, test_rtc_set);
    qtest_add_data_func("/ds-rtc-i2c/set24_12pm", test_time_24_12pm, test_rtc_set);
    qtest_add_data_func("/ds-rtc-i2c/set24_6pm", test_time_24_6pm, test_rtc_set);
    qtest_add_data_func("/ds-rtc-i2c/set12_12am", test_time_12_12am, test_rtc_set);
    qtest_add_data_func("/ds-rtc-i2c/set12_6am", test_time_12_6am, test_rtc_set);
    qtest_add_data_func("/ds-rtc-i2c/set12_12pm", test_time_12_12pm, test_rtc_set);
    qtest_add_data_func("/ds-rtc-i2c/set12_6pm", test_time_12_6pm, test_rtc_set);
    qtest_add_func("/ds-rtc-i2c/current", test_rtc_current);

    ret = g_test_run();

    qtest_end();

    return ret;
}
