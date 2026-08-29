/*
 * QTest testcase for the RP2040 QSPI IO bank block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define IOQSPI_BASE             0x40018000
#define IOQSPI_SCLK_STATUS      0x00
#define IOQSPI_SCLK_CTRL        0x04
#define IOQSPI_SD1_CTRL         0x1c
#define IOQSPI_PROC0_INTE       0x34
#define IOQSPI_PROC0_INTF       0x38
#define IOQSPI_PROC0_INTS       0x3c

#define IOQSPI_CTRL_RESET       0x1f
#define IOQSPI_SD1_CTRL_SET     0x201c
#define IOQSPI_SD1_CTRL_CLR     0x301c

static QTestState *rp2040_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_ioqspi_reset_values(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, IOQSPI_BASE + IOQSPI_SCLK_STATUS), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, IOQSPI_BASE + IOQSPI_SCLK_CTRL), ==,
                    IOQSPI_CTRL_RESET);
    g_assert_cmphex(qtest_readl(qts, IOQSPI_BASE + IOQSPI_SD1_CTRL), ==,
                    IOQSPI_CTRL_RESET);

    qtest_quit(qts);
}

static void test_ioqspi_atomic_ctrl_aliases(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, IOQSPI_BASE + IOQSPI_SD1_CTRL, 0);
    qtest_writel(qts, IOQSPI_BASE + IOQSPI_SD1_CTRL_SET, BIT(17));
    qtest_writel(qts, IOQSPI_BASE + IOQSPI_SD1_CTRL_CLR, BIT(17));
    qtest_writel(qts, IOQSPI_BASE + IOQSPI_SD1_CTRL_SET, BIT(9));

    g_assert_cmphex(qtest_readl(qts, IOQSPI_BASE + IOQSPI_SD1_CTRL), ==,
                    BIT(9));

    qtest_quit(qts);
}

static void test_ioqspi_interrupt_force_status(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, IOQSPI_BASE + IOQSPI_PROC0_INTE, BIT(1));
    qtest_writel(qts, IOQSPI_BASE + IOQSPI_PROC0_INTF, BIT(2));

    g_assert_cmphex(qtest_readl(qts, IOQSPI_BASE + IOQSPI_PROC0_INTS), ==,
                    BIT(2));

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-ioqspi/reset-values", test_ioqspi_reset_values);
    qtest_add_func("/rp2040-ioqspi/atomic-ctrl-aliases",
                   test_ioqspi_atomic_ctrl_aliases);
    qtest_add_func("/rp2040-ioqspi/interrupt-force-status",
                   test_ioqspi_interrupt_force_status);

    return g_test_run();
}
