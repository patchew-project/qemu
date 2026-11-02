/*
 * QTest testcase for the BCM2835 system timer
 *
 * A compare match raises the interrupt of its channel; a reset clears the
 * matches, lowers the interrupts and cancels the pending compares.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"

#define SYSTMR_BASE     0xfe003000
#define SYSTMR_PATH     "/machine/soc/peripherals/systimer"
#define CS              0x00
#define CLO             0x04
#define C(n)            (0x0c + (n) * 4)

#define US              1000    /* the timer counts microseconds */

static void test_reset(void)
{
    QTestState *qts = qtest_init("-M raspi4b");
    uint32_t clo;

    qtest_irq_intercept_out_named(qts, SYSTMR_PATH, "sysbus-irq");

    clo = qtest_readl(qts, SYSTMR_BASE + CLO);
    qtest_writel(qts, SYSTMR_BASE + C(1), clo + 1000);
    qtest_writel(qts, SYSTMR_BASE + C(3), clo + 100000);
    qtest_clock_step(qts, 2000 * US);
    g_assert_cmphex(qtest_readl(qts, SYSTMR_BASE + CS), ==, BIT(1));
    g_assert_true(qtest_get_irq(qts, 1));
    g_assert_false(qtest_get_irq(qts, 3));

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SYSTMR_BASE + CS), ==, 0);
    g_assert_false(qtest_get_irq(qts, 1));

    /* the compare of channel 3 was set before the reset */
    qtest_clock_step(qts, 200000 * US);
    g_assert_cmphex(qtest_readl(qts, SYSTMR_BASE + CS), ==, 0);
    g_assert_false(qtest_get_irq(qts, 3));

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/bcm2835-systmr/reset", test_reset);
    return g_test_run();
}
