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
#include "libqtest.h"
#include "libqos/libqos.h"
#include "libqos/i2c.h"

#include "ds-rtc-common.h"

/* read back and compare with current system time */
static
void test_rtc_current(void)
{
    struct tm tm_actual;
    time_t expected, actual;
    /* relax test to limit false positives when host may be overloaded.
     * Allow larger delta when running "-m quick"
     */
    time_t max_delta = g_test_slow() ? 1 : 30;

    unsigned wday_expect;

    actual = time(NULL);
    /* new second may start here */
    expected = rtc_gettime(&wday_expect);

    gmtime_r(&actual, &tm_actual);

    g_assert_cmpuint(expected, <=, actual + max_delta);
    g_assert_cmpuint(expected, >=, actual);
    g_assert_cmpuint(wday_expect, ==, tm_actual.tm_wday);
}

int main(int argc, char *argv[])
{
    int ret;
    const char *arch = qtest_get_arch();
    QTestState *s = NULL;

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "arm") == 0) {
        s = qtest_start("-machine imx25-pdk");
        i2c = imx_i2c_create(s, IMX25_I2C_0_BASE);
        addr = DS1338_ADDR;
        use_century = false;

    }

    qtest_add_func("/ds-rtc-i2c/current", test_rtc_current);

    ret = g_test_run();

    qtest_end();

    return ret;
}
