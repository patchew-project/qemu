/*
 * QTest testcase for the RP2040 SIO block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define SIO_BASE                0xd0000000
#define SIO_CPUID               0x000
#define SIO_GPIO_IN             0x004
#define SIO_GPIO_HI_IN          0x008
#define SIO_GPIO_OUT            0x010
#define SIO_GPIO_OUT_SET        0x014
#define SIO_GPIO_OUT_CLR        0x018
#define SIO_GPIO_OUT_XOR        0x01c
#define SIO_GPIO_HI_OUT         0x030
#define SIO_GPIO_HI_OUT_SET     0x034
#define SIO_GPIO_HI_OUT_CLR     0x038
#define SIO_GPIO_HI_OUT_XOR     0x03c
#define SIO_FIFO_ST             0x050
#define SIO_FIFO_RD             0x058
#define SIO_SPINLOCK_ST         0x05c
#define SIO_SPINLOCK0           0x100
#define SIO_INTERP0_ACCUM0      0x080
#define SIO_INTERP0_ACCUM1      0x084
#define SIO_INTERP0_BASE0       0x088
#define SIO_INTERP0_BASE1       0x08c
#define SIO_INTERP0_POP_LANE1   0x098
#define SIO_INTERP0_PEEK_LANE0  0x0a0
#define SIO_INTERP0_PEEK_LANE1  0x0a4
#define SIO_INTERP0_CTRL_LANE0  0x0ac
#define SIO_INTERP0_CTRL_LANE1  0x0b0
#define SIO_INTERP0_ACCUM0_ADD  0x0b4
#define SIO_INTERP0_BASE_1AND0  0x0bc
#define SIO_INTERP1_ACCUM0      0x0c0
#define SIO_INTERP1_BASE0       0x0c8
#define SIO_INTERP1_BASE1       0x0cc
#define SIO_INTERP1_PEEK_LANE0  0x0e0
#define SIO_INTERP1_CTRL_LANE0  0x0ec

#define INTERP_CTRL_SHIFT(n)    (n)
#define INTERP_CTRL_MASK(lsb, msb) (((lsb) << 5) | ((msb) << 10))
#define INTERP_CTRL_SIGNED      BIT(15)
#define INTERP_CTRL_CROSS_RESULT BIT(17)
#define INTERP0_CTRL_BLEND      BIT(21)
#define INTERP1_CTRL_CLAMP      BIT(22)

static QTestState *rp2040_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void test_sio_reset_values(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_CPUID), ==, 0);
    g_assert_cmphex(qtest_readb(qts, SIO_BASE + SIO_CPUID), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_GPIO_IN), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_GPIO_HI_IN), ==, BIT(1));
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_GPIO_OUT), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_GPIO_HI_OUT), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_FIFO_ST), ==, BIT(1));
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_SPINLOCK_ST), ==, 0);

    qtest_quit(qts);
}

static void test_sio_gpio_alias_registers(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, SIO_BASE + SIO_GPIO_OUT, BIT(3));
    qtest_writel(qts, SIO_BASE + SIO_GPIO_OUT_SET, BIT(5));
    qtest_writel(qts, SIO_BASE + SIO_GPIO_OUT_CLR, BIT(3));
    qtest_writel(qts, SIO_BASE + SIO_GPIO_OUT_XOR, BIT(7));
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_GPIO_OUT), ==,
                    BIT(5) | BIT(7));

    qtest_writel(qts, SIO_BASE + SIO_GPIO_HI_OUT, BIT(0));
    qtest_writel(qts, SIO_BASE + SIO_GPIO_HI_OUT_SET, BIT(2));
    qtest_writel(qts, SIO_BASE + SIO_GPIO_HI_OUT_CLR, BIT(0));
    qtest_writel(qts, SIO_BASE + SIO_GPIO_HI_OUT_XOR, BIT(5));
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_GPIO_HI_OUT), ==,
                    BIT(2) | BIT(5));

    qtest_quit(qts);
}

static void test_sio_fifo_empty_read_sets_roe(void)
{
    QTestState *qts = rp2040_start();

    qtest_readl(qts, SIO_BASE + SIO_FIFO_RD);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_FIFO_ST), ==,
                    BIT(3) | BIT(1));

    qtest_writel(qts, SIO_BASE + SIO_FIFO_ST, BIT(3));
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_FIFO_ST), ==, BIT(1));

    qtest_quit(qts);
}

static void test_sio_spinlock_claim_release(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_SPINLOCK0), ==, BIT(0));
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_SPINLOCK0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_SPINLOCK_ST), ==, BIT(0));

    qtest_writel(qts, SIO_BASE + SIO_SPINLOCK0, 0);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_SPINLOCK_ST), ==, 0);

    qtest_quit(qts);
}

static void test_sio_interp_mask_and_sign(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, SIO_BASE + SIO_INTERP0_ACCUM0, 0x1234abcd);
    qtest_writel(qts, SIO_BASE + SIO_INTERP0_CTRL_LANE0,
                 INTERP_CTRL_MASK(4, 7));
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP0_ACCUM0_ADD),
                    ==, 0x000000c0);

    qtest_writel(qts, SIO_BASE + SIO_INTERP0_CTRL_LANE0,
                 INTERP_CTRL_MASK(4, 7) | INTERP_CTRL_SIGNED);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP0_ACCUM0_ADD),
                    ==, 0xffffffc0);

    qtest_quit(qts);
}

static void test_sio_interp_pop_cross_result(void)
{
    QTestState *qts = rp2040_start();
    uint32_t full_mask = INTERP_CTRL_MASK(0, 31);

    qtest_writel(qts, SIO_BASE + SIO_INTERP0_ACCUM0, 123);
    qtest_writel(qts, SIO_BASE + SIO_INTERP0_ACCUM1, 456);
    qtest_writel(qts, SIO_BASE + SIO_INTERP0_BASE0, 1);
    qtest_writel(qts, SIO_BASE + SIO_INTERP0_BASE1, 0);
    qtest_writel(qts, SIO_BASE + SIO_INTERP0_CTRL_LANE0,
                 full_mask | INTERP_CTRL_CROSS_RESULT);
    qtest_writel(qts, SIO_BASE + SIO_INTERP0_CTRL_LANE1,
                 full_mask | INTERP_CTRL_CROSS_RESULT);

    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP0_PEEK_LANE0),
                    ==, 124);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP0_POP_LANE1),
                    ==, 456);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP0_PEEK_LANE0),
                    ==, 457);

    qtest_quit(qts);
}

static void test_sio_interp_blend(void)
{
    QTestState *qts = rp2040_start();
    uint32_t full_mask = INTERP_CTRL_MASK(0, 31);

    qtest_writel(qts, SIO_BASE + SIO_INTERP0_CTRL_LANE0,
                 full_mask | INTERP0_CTRL_BLEND);
    qtest_writel(qts, SIO_BASE + SIO_INTERP0_CTRL_LANE1, full_mask);
    qtest_writel(qts, SIO_BASE + SIO_INTERP0_ACCUM1, 128);

    qtest_writel(qts, SIO_BASE + SIO_INTERP0_BASE_1AND0, 0x30005000);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP0_PEEK_LANE1),
                    ==, 0x00004000);

    qtest_writel(qts, SIO_BASE + SIO_INTERP0_BASE_1AND0, 0xe000f000);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP0_PEEK_LANE1),
                    ==, 0x0000e800);

    qtest_writel(qts, SIO_BASE + SIO_INTERP0_CTRL_LANE1,
                 full_mask | INTERP_CTRL_SIGNED);
    qtest_writel(qts, SIO_BASE + SIO_INTERP0_BASE_1AND0, 0xe000f000);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP0_PEEK_LANE1),
                    ==, 0xffffe800);

    qtest_quit(qts);
}

static void test_sio_interp_clamp(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, SIO_BASE + SIO_INTERP1_CTRL_LANE0,
                 INTERP1_CTRL_CLAMP | INTERP_CTRL_SIGNED |
                 INTERP_CTRL_SHIFT(2) | INTERP_CTRL_MASK(0, 29));
    qtest_writel(qts, SIO_BASE + SIO_INTERP1_BASE0, 0);
    qtest_writel(qts, SIO_BASE + SIO_INTERP1_BASE1, 255);

    qtest_writel(qts, SIO_BASE + SIO_INTERP1_ACCUM0, -1024);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP1_PEEK_LANE0),
                    ==, 0);

    qtest_writel(qts, SIO_BASE + SIO_INTERP1_ACCUM0, 512);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP1_PEEK_LANE0),
                    ==, 128);

    qtest_writel(qts, SIO_BASE + SIO_INTERP1_ACCUM0, 1024);
    g_assert_cmphex(qtest_readl(qts, SIO_BASE + SIO_INTERP1_PEEK_LANE0),
                    ==, 255);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-sio/reset-values", test_sio_reset_values);
    qtest_add_func("/rp2040-sio/gpio-alias-registers",
                   test_sio_gpio_alias_registers);
    qtest_add_func("/rp2040-sio/fifo-empty-read-sets-roe",
                   test_sio_fifo_empty_read_sets_roe);
    qtest_add_func("/rp2040-sio/spinlock-claim-release",
                   test_sio_spinlock_claim_release);
    qtest_add_func("/rp2040-sio/interp-mask-and-sign",
                   test_sio_interp_mask_and_sign);
    qtest_add_func("/rp2040-sio/interp-pop-cross-result",
                   test_sio_interp_pop_cross_result);
    qtest_add_func("/rp2040-sio/interp-blend", test_sio_interp_blend);
    qtest_add_func("/rp2040-sio/interp-clamp", test_sio_interp_clamp);

    return g_test_run();
}
