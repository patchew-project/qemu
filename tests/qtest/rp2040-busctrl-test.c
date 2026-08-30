/*
 * QTest testcase for the RP2040 bus fabric control block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define BUSCTRL_BASE           0x40030000
#define BUSCTRL_PRIORITY       0x00
#define BUSCTRL_PRIORITY_ACK   0x04
#define BUSCTRL_PERFCTR0       0x08
#define BUSCTRL_PERFSEL0       0x0c
#define BUSCTRL_PERFCTR1       0x10
#define BUSCTRL_PERFSEL1       0x14
#define BUSCTRL_PRIORITY_MASK  0x00001111
#define BUSCTRL_PERFCTR_MASK   0x00ffffff
#define BUSCTRL_PERFSEL_RESET  0x1f
#define BUSCTRL_PERFSEL_SRAM5  0x05

static QTestState *rp2040_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_busctrl_reset_values(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PRIORITY), ==, 0);
    g_assert_cmphex(qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PRIORITY_ACK),
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PERFSEL0),
                    ==, BUSCTRL_PERFSEL_RESET);
    g_assert_cmphex(qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PERFSEL1),
                    ==, BUSCTRL_PERFSEL_RESET);

    qtest_quit(qts);
}

static void test_busctrl_priority(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, BUSCTRL_BASE + BUSCTRL_PRIORITY, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PRIORITY),
                    ==, BUSCTRL_PRIORITY_MASK);
    g_assert_cmphex(qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PRIORITY_ACK),
                    ==, 1);

    qtest_quit(qts);
}

static void test_busctrl_perf_counter(void)
{
    QTestState *qts = rp2040_start();
    uint32_t first;
    uint32_t second;

    g_assert_cmphex(qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PERFCTR0),
                    ==, 0);
    qtest_writel(qts, BUSCTRL_BASE + BUSCTRL_PERFSEL0,
                 BUSCTRL_PERFSEL_SRAM5);
    g_assert_cmphex(qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PERFSEL0),
                    ==, BUSCTRL_PERFSEL_SRAM5);

    first = qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PERFCTR0);
    second = qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PERFCTR0);
    g_assert_cmphex(first, ==, 1);
    g_assert_cmphex(second, ==, 2);

    qtest_writel(qts, BUSCTRL_BASE + BUSCTRL_PERFCTR0,
                 BUSCTRL_PERFCTR_MASK);
    g_assert_cmphex(qtest_readl(qts, BUSCTRL_BASE + BUSCTRL_PERFCTR0),
                    ==, BUSCTRL_PERFCTR_MASK);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-busctrl/reset-values",
                   test_busctrl_reset_values);
    qtest_add_func("/rp2040-busctrl/priority", test_busctrl_priority);
    qtest_add_func("/rp2040-busctrl/perf-counter",
                   test_busctrl_perf_counter);

    return g_test_run();
}
