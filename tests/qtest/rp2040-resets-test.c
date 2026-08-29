/*
 * QTest testcase for the RP2040 reset controller block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define RESETS_BASE        0x4000c000
#define RESETS_RESET       0x00
#define RESETS_WDSEL       0x04
#define RESETS_RESET_DONE  0x08

#define RESETS_VALID_MASK  0x01ffffff
#define RESET_ADC          (1u << 0)
#define RESET_PWM          (1u << 14)

#define ATOMIC_SET         0x2000
#define ATOMIC_CLR         0x3000

static QTestState *rp2040_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_resets_reset_values(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, RESETS_BASE + RESETS_RESET), ==,
                    RESETS_VALID_MASK);
    g_assert_cmphex(qtest_readl(qts, RESETS_BASE + RESETS_RESET_DONE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, RESETS_BASE + RESETS_WDSEL), ==, 0);

    qtest_quit(qts);
}

static void test_resets_set_clear_done(void)
{
    QTestState *qts = rp2040_start();
    uint32_t mask = RESET_PWM | RESET_ADC;

    qtest_writel(qts, RESETS_BASE + RESETS_RESET + ATOMIC_CLR, mask);
    g_assert_cmphex(qtest_readl(qts, RESETS_BASE + RESETS_RESET) & mask,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, RESETS_BASE + RESETS_RESET_DONE) & mask,
                    ==, mask);

    qtest_writel(qts, RESETS_BASE + RESETS_RESET + ATOMIC_SET, RESET_PWM);
    g_assert_cmphex(qtest_readl(qts, RESETS_BASE + RESETS_RESET) & RESET_PWM,
                    ==, RESET_PWM);
    g_assert_cmphex(qtest_readl(qts, RESETS_BASE + RESETS_RESET_DONE) &
                    RESET_PWM, ==, 0);

    qtest_writel(qts, RESETS_BASE + RESETS_RESET + ATOMIC_CLR, RESET_PWM);
    g_assert_cmphex(qtest_readl(qts, RESETS_BASE + RESETS_RESET_DONE) & mask,
                    ==, mask);

    qtest_quit(qts);
}

static void test_resets_wdsel(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, RESETS_BASE + RESETS_WDSEL, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, RESETS_BASE + RESETS_WDSEL), ==,
                    RESETS_VALID_MASK);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-resets/reset-values", test_resets_reset_values);
    qtest_add_func("/rp2040-resets/set-clear-done",
                   test_resets_set_clear_done);
    qtest_add_func("/rp2040-resets/wdsel", test_resets_wdsel);

    return g_test_run();
}
