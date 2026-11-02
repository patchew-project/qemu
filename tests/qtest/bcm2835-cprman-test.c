/*
 * QTest testcase for the BCM2835 clock manager
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"

#define CPRMAN_BASE     0x3f101000
#define A2W_PLLD_PER    0x1540
#define A2W_PASSWORD    0x5a000000
#define CPRMAN_PATH     "/machine/soc/peripherals/cprman"

/* The frequency of a clock, from its period in 2^-32 ns */
static uint64_t clock_hz(QTestState *qts, const char *path)
{
    QDict *rsp = qtest_qmp(qts, "{ 'execute': 'qom-get', 'arguments': "
                           "{ 'path': %s, 'property': 'qtest-clock-period' } }",
                           path);
    uint64_t period = qdict_get_int(rsp, "return");

    qobject_unref(rsp);
    g_assert_cmpuint(period, !=, 0);
    return (1000000000ULL << 32) / period;
}

/* A PLL channel divider of 0 divides by 256 */
static void test_channel_div_zero(void)
{
    QTestState *qts = qtest_init("-M raspi3b");
    uint32_t per = qtest_readl(qts, CPRMAN_BASE + A2W_PLLD_PER);
    uint64_t pll_hz, chan_hz;

    qtest_writel(qts, CPRMAN_BASE + A2W_PLLD_PER,
                 A2W_PASSWORD | (per & ~0xffu));
    pll_hz = clock_hz(qts, CPRMAN_PATH "/plld/out");
    chan_hz = clock_hz(qts, CPRMAN_PATH "/plld-per/out");
    g_assert_cmpuint((pll_hz + chan_hz / 2) / chan_hz, ==, 256);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/bcm2835-cprman/channel-div-zero", test_channel_div_zero);
    return g_test_run();
}
