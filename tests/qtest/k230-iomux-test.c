/*
 * QTest testcase for Kendryte K230 IOMUX
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides test coverage for the Function IO configuration registers.
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * For more information, see <https://www.kendryte.com/en/proDetail/230>
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/misc/k230_iomux.h"

#define K230_IOMUX_BASE 0x91105000
#define K230_IOMUX_IO0  (K230_IOMUX_BASE + 0x00)
#define K230_IOMUX_IO1  (K230_IOMUX_BASE + 0x04)
#define K230_IOMUX_IO63 (K230_IOMUX_BASE + 0xfc)
#define K230_IOMUX_RESERVED_FIRST (K230_IOMUX_BASE + 0x100)
#define K230_IOMUX_RESERVED_LAST \
    (K230_IOMUX_BASE + K230_IOMUX_MMIO_SIZE - 4)
#define K230_IOMUX_IO_SEL_MASK (0x7 << 11)
#define K230_IOMUX_IO_SEL(value) ((value) << 11)

static const uint32_t k230_iomux_reset_values[K230_IOMUX_NUM_REGS] = {
    0x944, 0x944, 0x929, 0x908, 0x888, 0x908, 0x948, 0x890,
    0x890, 0x890, 0x910, 0x890, 0x890, 0x8a9, 0xa9e, 0xabf,
    0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e,
    0xb1e, 0xa90, 0xabf, 0xb9e, 0xb9e, 0xb9e, 0xb9e, 0xb9e,
    0xbd0, 0xbd0, 0xbd0, 0xbd0, 0xbd0, 0xbd0, 0x890, 0x910,
    0x890, 0x910, 0x890, 0x910, 0x890, 0x910, 0x890, 0x910,
    0x890, 0x910, 0x890, 0x910, 0x890, 0x910, 0x89e, 0x89f,
    0x99e, 0x99e, 0x99e, 0x99e, 0x890, 0x890, 0x8a9, 0x8a9,
};

static void test_reset_values(void)
{
    QTestState *qts = qtest_init("-machine k230");

    for (size_t i = 0; i < G_N_ELEMENTS(k230_iomux_reset_values); i++) {
        g_assert_cmphex(qtest_readl(qts, K230_IOMUX_BASE +
                                    i * sizeof(uint32_t)), ==,
                        k230_iomux_reset_values[i]);
    }

    qtest_quit(qts);
}

static void test_rw(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_IOMUX_IO0, 0x00001234);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO0), ==, 0x00001234);

    qtest_writel(qts, K230_IOMUX_IO1, 0x00002abc);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO1), ==, 0x00002abc);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO0), ==, 0x00001234);

    qtest_quit(qts);
}

static void test_write_mask(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_IOMUX_IO0, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO0), ==,
                    K230_IOMUX_WRITABLE_MASK);

    qtest_quit(qts);
}

static void test_rmw(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t value;

    value = qtest_readl(qts, K230_IOMUX_IO0);
    value = (value & ~K230_IOMUX_IO_SEL_MASK) | K230_IOMUX_IO_SEL(3);
    qtest_writel(qts, K230_IOMUX_IO0, value);

    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO0), ==,
                    (k230_iomux_reset_values[0] &
                     ~K230_IOMUX_IO_SEL_MASK) | K230_IOMUX_IO_SEL(3));

    qtest_quit(qts);
}

static void test_io63_rw(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_IOMUX_IO63, 0x00000abc);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO63), ==, 0x00000abc);

    qtest_quit(qts);
}

static void test_reserved_offsets(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_IOMUX_RESERVED_FIRST, 0x00001234);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_RESERVED_FIRST), ==, 0);

    qtest_writel(qts, K230_IOMUX_RESERVED_LAST, 0x00002abc);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_RESERVED_LAST), ==, 0);

    qtest_quit(qts);
}

static void test_reset_after_write(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_IOMUX_IO0, 0x00001234);
    qtest_writel(qts, K230_IOMUX_IO1, 0x00002abc);
    qtest_writel(qts, K230_IOMUX_IO63, 0x00000abc);

    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO0), ==, 0x00001234);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO1), ==, 0x00002abc);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO63), ==, 0x00000abc);

    qtest_system_reset(qts);

    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO0), ==,
                    k230_iomux_reset_values[0]);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO1), ==,
                    k230_iomux_reset_values[1]);
    g_assert_cmphex(qtest_readl(qts, K230_IOMUX_IO63), ==,
                    k230_iomux_reset_values[63]);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-iomux/reset", test_reset_values);
    qtest_add_func("/k230-iomux/rw", test_rw);
    qtest_add_func("/k230-iomux/write-mask", test_write_mask);
    qtest_add_func("/k230-iomux/rmw", test_rmw);
    qtest_add_func("/k230-iomux/io63-rw", test_io63_rw);
    qtest_add_func("/k230-iomux/reserved-offsets", test_reserved_offsets);
    qtest_add_func("/k230-iomux/reset-after-write", test_reset_after_write);

    return g_test_run();
}
