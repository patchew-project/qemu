/*
 * QTest testcase for the RP2040 IO_BANK0.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define IOBANK0_BASE         0x40014000
#define GPIO0_STATUS         0x000
#define GPIO0_CTRL           0x004
#define GPIO29_CTRL          0x0ec
#define INTR0                0x0f0
#define PROC0_INTE0          0x100
#define PROC0_INTF0          0x110
#define PROC0_INTS0          0x120
#define PROC1_INTE3          0x13c
#define PROC1_INTF3          0x14c
#define PROC1_INTS3          0x15c
#define DORMANT_INTE3        0x16c
#define DORMANT_INTF3        0x17c
#define DORMANT_INTS3        0x18c

#define CTRL_RESET           0x1f
#define CTRL_RW_MASK         0x33333f
#define IRQ_LAST_MASK        0x00ffffff
#define IO_IRQ_BANK0         13

#define ATOMIC_XOR           0x1000
#define ATOMIC_SET           0x2000
#define ATOMIC_CLR           0x3000

#define NVIC_ISPR            0xe000e200
#define NVIC_ICPR            0xe000e280

static QTestState *rp2040_iobank0_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_gpio_control(void)
{
    QTestState *qts = rp2040_iobank0_start();

    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + GPIO0_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + GPIO0_CTRL), ==,
                    CTRL_RESET);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + GPIO29_CTRL), ==,
                    CTRL_RESET);

    qtest_writel(qts, IOBANK0_BASE + GPIO0_CTRL, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + GPIO0_CTRL), ==,
                    CTRL_RW_MASK);
    qtest_writel(qts, IOBANK0_BASE + ATOMIC_CLR + GPIO0_CTRL, 0x30);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + GPIO0_CTRL), ==,
                    CTRL_RW_MASK & ~0x30);
    qtest_writel(qts, IOBANK0_BASE + ATOMIC_XOR + GPIO0_CTRL, 0x03);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + GPIO0_CTRL), ==,
                    (CTRL_RW_MASK & ~0x30) ^ 0x03);

    qtest_quit(qts);
}

static void test_proc0_irq(void)
{
    QTestState *qts = rp2040_iobank0_start();
    uint32_t irq = BIT(IO_IRQ_BANK0);

    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + INTR0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + PROC0_INTS0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, NVIC_ISPR) & irq, ==, 0);

    qtest_writel(qts, IOBANK0_BASE + PROC0_INTF0, BIT(7));
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + PROC0_INTS0), ==,
                    BIT(7));
    g_assert_cmphex(qtest_readl(qts, NVIC_ISPR) & irq, ==, irq);

    qtest_writel(qts, IOBANK0_BASE + ATOMIC_CLR + PROC0_INTF0, BIT(7));
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + PROC0_INTS0), ==, 0);
    qtest_writel(qts, NVIC_ICPR, irq);
    g_assert_cmphex(qtest_readl(qts, NVIC_ISPR) & irq, ==, 0);

    qtest_writel(qts, IOBANK0_BASE + ATOMIC_SET + PROC0_INTE0, BIT(3));
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + PROC0_INTE0), ==,
                    BIT(3));

    qtest_quit(qts);
}

static void test_last_bank_masks(void)
{
    QTestState *qts = rp2040_iobank0_start();

    qtest_writel(qts, IOBANK0_BASE + PROC1_INTE3, 0xffffffff);
    qtest_writel(qts, IOBANK0_BASE + PROC1_INTF3, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + PROC1_INTE3), ==,
                    IRQ_LAST_MASK);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + PROC1_INTS3), ==,
                    IRQ_LAST_MASK);

    qtest_writel(qts, IOBANK0_BASE + DORMANT_INTE3, 0xffffffff);
    qtest_writel(qts, IOBANK0_BASE + DORMANT_INTF3, 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + DORMANT_INTE3), ==,
                    IRQ_LAST_MASK);
    g_assert_cmphex(qtest_readl(qts, IOBANK0_BASE + DORMANT_INTS3), ==,
                    IRQ_LAST_MASK);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-iobank0/gpio-control", test_gpio_control);
    qtest_add_func("/rp2040-iobank0/proc0-irq", test_proc0_irq);
    qtest_add_func("/rp2040-iobank0/last-bank-masks", test_last_bank_masks);

    return g_test_run();
}
