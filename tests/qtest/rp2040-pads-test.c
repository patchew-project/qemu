/*
 * QTest testcase for the RP2040 pad controls.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define PADS_BANK0_BASE       0x4001c000
#define PADS_BANK0_VOLTAGE    0x00
#define PADS_BANK0_GPIO0      0x04
#define PADS_BANK0_GPIO29     0x78
#define PADS_BANK0_SWCLK      0x7c
#define PADS_BANK0_SWD        0x80

#define PADS_QSPI_BASE        0x40020000
#define PADS_QSPI_VOLTAGE     0x00
#define PADS_QSPI_SCLK        0x04
#define PADS_QSPI_SD0         0x08
#define PADS_QSPI_SD3         0x14
#define PADS_QSPI_SS          0x18

#define ATOMIC_XOR            0x1000
#define ATOMIC_SET            0x2000
#define ATOMIC_CLR            0x3000

static QTestState *rp2040_pads_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_bank0(void)
{
    QTestState *qts = rp2040_pads_start();

    g_assert_cmphex(qtest_readl(qts, PADS_BANK0_BASE +
                                PADS_BANK0_VOLTAGE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, PADS_BANK0_BASE + PADS_BANK0_GPIO0),
                    ==, 0x56);
    g_assert_cmphex(qtest_readl(qts, PADS_BANK0_BASE + PADS_BANK0_GPIO29),
                    ==, 0x56);
    g_assert_cmphex(qtest_readl(qts, PADS_BANK0_BASE + PADS_BANK0_SWCLK),
                    ==, 0x5a);
    g_assert_cmphex(qtest_readl(qts, PADS_BANK0_BASE + PADS_BANK0_SWD),
                    ==, 0x5a);

    qtest_writel(qts, PADS_BANK0_BASE + PADS_BANK0_VOLTAGE, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, PADS_BANK0_BASE +
                                PADS_BANK0_VOLTAGE), ==, 1);

    qtest_writel(qts, PADS_BANK0_BASE + PADS_BANK0_GPIO0, 0x12);
    qtest_writel(qts, PADS_BANK0_BASE + ATOMIC_SET + PADS_BANK0_GPIO0,
                 0x81);
    g_assert_cmphex(qtest_readl(qts, PADS_BANK0_BASE + PADS_BANK0_GPIO0),
                    ==, 0x93);
    qtest_writel(qts, PADS_BANK0_BASE + ATOMIC_CLR + PADS_BANK0_GPIO0,
                 0x11);
    g_assert_cmphex(qtest_readl(qts, PADS_BANK0_BASE + PADS_BANK0_GPIO0),
                    ==, 0x82);
    qtest_writel(qts, PADS_BANK0_BASE + ATOMIC_XOR + PADS_BANK0_GPIO0,
                 0x03);
    g_assert_cmphex(qtest_readl(qts, PADS_BANK0_BASE + PADS_BANK0_GPIO0),
                    ==, 0x81);

    qtest_quit(qts);
}

static void test_qspi(void)
{
    QTestState *qts = rp2040_pads_start();

    g_assert_cmphex(qtest_readl(qts, PADS_QSPI_BASE + PADS_QSPI_VOLTAGE),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, PADS_QSPI_BASE + PADS_QSPI_SCLK),
                    ==, 0x56);
    g_assert_cmphex(qtest_readl(qts, PADS_QSPI_BASE + PADS_QSPI_SD0),
                    ==, 0x52);
    g_assert_cmphex(qtest_readl(qts, PADS_QSPI_BASE + PADS_QSPI_SD3),
                    ==, 0x52);
    g_assert_cmphex(qtest_readl(qts, PADS_QSPI_BASE + PADS_QSPI_SS),
                    ==, 0x5a);

    qtest_writel(qts, PADS_QSPI_BASE + PADS_QSPI_SS, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, PADS_QSPI_BASE + PADS_QSPI_SS),
                    ==, 0xff);
    qtest_writel(qts, PADS_QSPI_BASE + ATOMIC_CLR + PADS_QSPI_SS, 0x0f);
    g_assert_cmphex(qtest_readl(qts, PADS_QSPI_BASE + PADS_QSPI_SS),
                    ==, 0xf0);
    qtest_writel(qts, PADS_QSPI_BASE + ATOMIC_XOR + PADS_QSPI_SS, 0x33);
    g_assert_cmphex(qtest_readl(qts, PADS_QSPI_BASE + PADS_QSPI_SS),
                    ==, 0xc3);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-pads/bank0", test_bank0);
    qtest_add_func("/rp2040-pads/qspi", test_qspi);

    return g_test_run();
}
