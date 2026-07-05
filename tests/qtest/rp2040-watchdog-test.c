/*
 * QTest testcase for the RP2040 watchdog.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "qobject/qdict.h"

#define WATCHDOG_BASE       0x40058000
#define WATCHDOG_CTRL       0x00
#define WATCHDOG_LOAD       0x04
#define WATCHDOG_REASON     0x08
#define WATCHDOG_SCRATCH0   0x0c
#define WATCHDOG_TICK       0x2c

#define CTRL_TRIGGER        BIT(31)
#define CTRL_ENABLE         BIT(30)
#define CTRL_PAUSE_DBG1     BIT(26)
#define CTRL_PAUSE_DBG0     BIT(25)
#define CTRL_PAUSE_JTAG     BIT(24)
#define CTRL_TIME_MASK      0x00ffffff

#define REASON_FORCE        BIT(1)
#define REASON_TIMER        BIT(0)

#define TICK_RUNNING        BIT(10)
#define TICK_ENABLE         BIT(9)

#define NS_PER_US           1000

static QTestState *rp2040_watchdog_start(void)
{
    return qtest_init("-machine raspi-pico -watchdog-action none");
}

static void check_watchdog_event(QTestState *qts)
{
    QDict *event = qtest_qmp_eventwait_ref(qts, "WATCHDOG");
    QDict *data = qdict_get_qdict(event, "data");

    g_assert_cmpstr(qdict_get_str(data, "action"), ==, "none");
    qobject_unref(event);
}

static void test_reset_values(void)
{
    QTestState *qts = rp2040_watchdog_start();

    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_CTRL), ==,
                    CTRL_PAUSE_DBG1 | CTRL_PAUSE_DBG0 | CTRL_PAUSE_JTAG);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_LOAD), ==, 0);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_REASON), ==, 0);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_TICK), ==,
                    TICK_ENABLE);

    qtest_quit(qts);
}

static void test_scratch_registers(void)
{
    QTestState *qts = rp2040_watchdog_start();

    qtest_writel(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0), ==,
                    0x12345678);

    g_assert_cmphex(qtest_readb(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0), ==,
                    0x78);
    g_assert_cmphex(qtest_readb(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0 + 1),
                    ==, 0x56);
    g_assert_cmphex(qtest_readb(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0 + 2),
                    ==, 0x34);
    g_assert_cmphex(qtest_readb(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0 + 3),
                    ==, 0x12);

    qtest_writeb(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0, 0xa5);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0), ==,
                    0xa5a5a5a5);

    qtest_writeb(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0 + 1, 0x3c);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0), ==,
                    0x3c3c3c3c);

    qtest_writew(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0, 0xf00d);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_SCRATCH0), ==,
                    0xf00df00d);

    qtest_quit(qts);
}

static void test_timer_expiry(void)
{
    QTestState *qts = rp2040_watchdog_start();
    uint32_t ctrl;

    /*
     * The default clk_ref is 6 MHz. CYCLES=6 generates a 1 MHz watchdog
     * tick; RP2040-E1 makes the counter decrement twice per tick.
     */
    qtest_writel(qts, WATCHDOG_BASE + WATCHDOG_TICK, TICK_ENABLE | 6);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_TICK) &
                    (TICK_RUNNING | TICK_ENABLE), ==,
                    TICK_RUNNING | TICK_ENABLE);

    qtest_writel(qts, WATCHDOG_BASE + WATCHDOG_LOAD, 20);
    qtest_writel(qts, WATCHDOG_BASE + WATCHDOG_CTRL, CTRL_ENABLE);

    qtest_clock_step(qts, 5 * NS_PER_US);
    ctrl = qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_CTRL);
    g_assert_cmpuint(ctrl & CTRL_TIME_MASK, <, 20);
    g_assert_cmpuint(ctrl & CTRL_TIME_MASK, >, 0);

    qtest_clock_step(qts, 10 * NS_PER_US);
    check_watchdog_event(qts);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_REASON), ==,
                    REASON_TIMER);

    qtest_quit(qts);
}

static void test_force_trigger(void)
{
    QTestState *qts = rp2040_watchdog_start();

    qtest_writel(qts, WATCHDOG_BASE + WATCHDOG_CTRL, CTRL_TRIGGER);
    check_watchdog_event(qts);
    g_assert_cmphex(qtest_readl(qts, WATCHDOG_BASE + WATCHDOG_REASON), ==,
                    REASON_FORCE);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-watchdog/reset-values", test_reset_values);
    qtest_add_func("/rp2040-watchdog/scratch-registers",
                   test_scratch_registers);
    qtest_add_func("/rp2040-watchdog/timer-expiry", test_timer_expiry);
    qtest_add_func("/rp2040-watchdog/force-trigger", test_force_trigger);

    return g_test_run();
}
