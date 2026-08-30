/*
 * QTest testcase for the RP2040 timer.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define TIMER_BASE          0x40054000
#define TIMER_TIMEHW        0x00
#define TIMER_TIMELW        0x04
#define TIMER_TIMEHR        0x08
#define TIMER_TIMELR        0x0c
#define TIMER_ALARM0        0x10
#define TIMER_ARMED         0x20
#define TIMER_TIMERAWH      0x24
#define TIMER_TIMERAWL      0x28
#define TIMER_DBGPAUSE      0x2c
#define TIMER_PAUSE         0x30
#define TIMER_INTR          0x34
#define TIMER_INTE          0x38
#define TIMER_INTF          0x3c
#define TIMER_INTS          0x40

#define TIMER_SET_ALIAS     0x2000
#define TIMER_CLR_ALIAS     0x3000
#define TIMER_ALARM_MASK    0x0f
#define TIMER_DBGPAUSE_MASK (BIT(2) | BIT(1))
#define TIMER_PAUSE_MASK    BIT(0)
#define NS_PER_US           1000

static QTestState *rp2040_timer_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_reset_values(void)
{
    QTestState *qts = rp2040_timer_start();

    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_ARMED), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_DBGPAUSE), ==,
                    TIMER_DBGPAUSE_MASK);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_PAUSE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTF), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_TIMEHR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_TIMERAWH), ==, 0);

    qtest_quit(qts);
}

static void test_counter_and_pause(void)
{
    QTestState *qts = rp2040_timer_start();
    uint32_t before = qtest_readl(qts, TIMER_BASE + TIMER_TIMERAWL);
    uint32_t paused;

    qtest_clock_step(qts, 123 * NS_PER_US);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_TIMERAWL) - before,
                    ==, 123);

    qtest_writel(qts, TIMER_BASE + TIMER_PAUSE, TIMER_PAUSE_MASK);
    paused = qtest_readl(qts, TIMER_BASE + TIMER_TIMERAWL);
    qtest_clock_step(qts, 100 * NS_PER_US);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_TIMERAWL), ==,
                    paused);

    qtest_writel(qts, TIMER_BASE + TIMER_TIMEHW, 0);
    qtest_writel(qts, TIMER_BASE + TIMER_TIMELW, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_TIMERAWL), ==,
                    0x12345678);

    qtest_writel(qts, TIMER_BASE + TIMER_CLR_ALIAS + TIMER_PAUSE,
                 TIMER_PAUSE_MASK);
    qtest_clock_step(qts, 10 * NS_PER_US);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_TIMELR), ==,
                    0x12345682);

    qtest_quit(qts);
}

static void test_alarm(void)
{
    QTestState *qts = rp2040_timer_start();
    uint32_t now = qtest_readl(qts, TIMER_BASE + TIMER_TIMERAWL);

    qtest_writel(qts, TIMER_BASE + TIMER_INTE, BIT(0));
    qtest_writel(qts, TIMER_BASE + TIMER_ALARM0, now + 100);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_ARMED), ==, BIT(0));

    qtest_clock_step(qts, 99 * NS_PER_US);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTR), ==, 0);
    qtest_clock_step(qts, NS_PER_US);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_ARMED), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTR), ==, BIT(0));
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTS), ==, BIT(0));

    qtest_writel(qts, TIMER_BASE + TIMER_INTR, BIT(0));
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTS), ==, 0);

    qtest_quit(qts);
}

static void test_interrupt_force_and_masks(void)
{
    QTestState *qts = rp2040_timer_start();

    qtest_writel(qts, TIMER_BASE + TIMER_SET_ALIAS + TIMER_INTE,
                 BIT(2) | BIT(8));
    qtest_writel(qts, TIMER_BASE + TIMER_SET_ALIAS + TIMER_INTF,
                 BIT(2) | BIT(9));
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTE), ==, BIT(2));
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTF), ==, BIT(2));
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTS), ==, BIT(2));

    qtest_writel(qts, TIMER_BASE + TIMER_CLR_ALIAS + TIMER_INTF, BIT(2));
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTF), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_INTS), ==, 0);

    qtest_writel(qts, TIMER_BASE + TIMER_ARMED, TIMER_ALARM_MASK);
    qtest_writel(qts, TIMER_BASE + TIMER_DBGPAUSE, UINT32_MAX);
    qtest_writel(qts, TIMER_BASE + TIMER_PAUSE, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_DBGPAUSE), ==,
                    TIMER_DBGPAUSE_MASK);
    g_assert_cmphex(qtest_readl(qts, TIMER_BASE + TIMER_PAUSE), ==,
                    TIMER_PAUSE_MASK);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-timer/reset-values", test_reset_values);
    qtest_add_func("/rp2040-timer/counter-and-pause",
                   test_counter_and_pause);
    qtest_add_func("/rp2040-timer/alarm", test_alarm);
    qtest_add_func("/rp2040-timer/interrupt-force-and-masks",
                   test_interrupt_force_and_masks);

    return g_test_run();
}
