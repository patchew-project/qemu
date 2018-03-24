/* Testing of Dallas/Maxim I2C bus RTC devices
 *
 * Copyright (c) 2018 Michael Davidsaver
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

#include "ds-rtc-common.h"

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
    /* relax test to limit false positives when host may be overloaded.
     * Allow larger delta when running "-m quick"
     */
    time_t max_delta = g_test_slow() ? 1 : 30;

    const uint8_t *testtime = raw;
    time_t expected, actual;

    /* skip address pointer and parse remainder */
    expected = rtc_parse(&testtime[1]);

    i2c_send(i2c, addr, testtime, 8);
    /* host may start new second here */
    actual = rtc_gettime();

    g_assert_cmpuint(expected, <=, actual);
    g_assert_cmpuint(expected + max_delta, >=, actual);
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

    ret = g_test_run();

    qtest_end();

    return ret;
}
