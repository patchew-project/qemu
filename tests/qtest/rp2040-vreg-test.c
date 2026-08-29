/*
 * QTest testcase for the RP2040 vreg_and_chip_reset block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define VREG_BASE           0x40064000
#define VREG_VREG           0x00
#define VREG_BOD            0x04
#define VREG_CHIP_RESET     0x08

#define VREG_ROK            BIT(12)
#define VREG_RESET          0x000000b1
#define BOD_RESET           0x00000091
#define CHIP_RESET_RESCUE   BIT(24)

#define ATOMIC_SET_ALIAS    0x2000
#define ATOMIC_CLR_ALIAS    0x3000

static QTestState *rp2040_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_vreg_reset_values(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, VREG_BASE + VREG_VREG), ==,
                    VREG_RESET | VREG_ROK);
    g_assert_cmphex(qtest_readl(qts, VREG_BASE + VREG_BOD), ==, BOD_RESET);
    g_assert_cmphex(qtest_readl(qts, VREG_BASE + VREG_CHIP_RESET), ==, 0);

    qtest_quit(qts);
}

static void test_vreg_rw_masks(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, VREG_BASE + VREG_VREG, 0xffffffff);
    qtest_writel(qts, VREG_BASE + VREG_BOD, 0xffffffff);
    qtest_writel(qts, VREG_BASE + VREG_CHIP_RESET, 0xffffffff);

    g_assert_cmphex(qtest_readl(qts, VREG_BASE + VREG_VREG), ==, 0x000000f3);
    g_assert_cmphex(qtest_readl(qts, VREG_BASE + VREG_BOD), ==, 0x000000f1);
    g_assert_cmphex(qtest_readl(qts, VREG_BASE + VREG_CHIP_RESET), ==,
                    CHIP_RESET_RESCUE);

    qtest_quit(qts);
}

static void test_vreg_atomic_aliases(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, VREG_BASE + VREG_BOD, 0);
    qtest_writel(qts, VREG_BASE + ATOMIC_SET_ALIAS + VREG_BOD, 0x00000011);
    qtest_writel(qts, VREG_BASE + ATOMIC_CLR_ALIAS + VREG_BOD, 0x00000010);

    g_assert_cmphex(qtest_readl(qts, VREG_BASE + VREG_BOD), ==, 0x00000001);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-vreg/reset-values", test_vreg_reset_values);
    qtest_add_func("/rp2040-vreg/rw-masks", test_vreg_rw_masks);
    qtest_add_func("/rp2040-vreg/atomic-aliases", test_vreg_atomic_aliases);

    return g_test_run();
}
