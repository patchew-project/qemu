/*
 * QTest testcase for the RP2040 power-on state machine.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define PSM_BASE       0x40010000
#define PSM_FRCE_ON    0x00
#define PSM_FRCE_OFF   0x04
#define PSM_WDSEL      0x08
#define PSM_DONE       0x0c
#define PSM_VALID_MASK 0x0001ffff
#define PSM_PROC1      BIT(16)

#define ATOMIC_SET     0x2000
#define ATOMIC_CLR     0x3000

static QTestState *rp2040_psm_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_reset_values(void)
{
    QTestState *qts = rp2040_psm_start();

    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_FRCE_ON), ==, 0);
    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_FRCE_OFF), ==, 0);
    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_WDSEL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_DONE), ==,
                    PSM_VALID_MASK);

    qtest_quit(qts);
}

static void test_proc1_force_off(void)
{
    QTestState *qts = rp2040_psm_start();

    qtest_writel(qts, PSM_BASE + ATOMIC_SET + PSM_FRCE_OFF, PSM_PROC1);
    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_FRCE_OFF), ==,
                    PSM_PROC1);
    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_DONE) & PSM_PROC1, ==,
                    0);

    qtest_writel(qts, PSM_BASE + ATOMIC_CLR + PSM_FRCE_OFF, PSM_PROC1);
    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_FRCE_OFF), ==, 0);
    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_DONE) & PSM_PROC1, ==,
                    PSM_PROC1);

    qtest_quit(qts);
}

static void test_register_masks(void)
{
    QTestState *qts = rp2040_psm_start();

    qtest_writel(qts, PSM_BASE + PSM_FRCE_ON, UINT32_MAX);
    qtest_writel(qts, PSM_BASE + PSM_WDSEL, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_FRCE_ON), ==,
                    PSM_VALID_MASK);
    g_assert_cmphex(qtest_readl(qts, PSM_BASE + PSM_WDSEL), ==,
                    PSM_VALID_MASK);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-psm/reset-values", test_reset_values);
    qtest_add_func("/rp2040-psm/proc1-force-off", test_proc1_force_off);
    qtest_add_func("/rp2040-psm/register-masks", test_register_masks);

    return g_test_run();
}
