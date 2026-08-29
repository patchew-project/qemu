/*
 * QTest testcase for the RP2040 clocks block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define CLOCKS_BASE             0x40008000
#define CLK_SYS_CTRL            0x3c
#define CLK_SYS_DIV             0x40
#define CLK_PERI_CTRL           0x48
#define CLK_PERI_DIV            0x4c
#define CLK_USB_CTRL            0x54
#define CLK_USB_DIV             0x58
#define CLK_ADC_CTRL            0x60
#define CLK_ADC_DIV             0x64
#define CLK_RTC_CTRL            0x6c
#define CLK_RTC_DIV             0x70
#define FC0_SRC                 0x94
#define FC0_STATUS              0x98
#define FC0_RESULT              0x9c

#define PLL_SYS_BASE            0x40028000
#define PLL_USB_BASE            0x4002c000
#define PLL_CS                  0x00
#define PLL_PWR                 0x04
#define PLL_FBDIV_INT           0x08
#define PLL_PRIM                0x0c

#define PLL_PWR_BITS            (BIT(5) | BIT(3) | BIT(2) | BIT(0))
#define CLK_CTRL_ENABLE         BIT(11)
#define FC0_STATUS_DONE         BIT(4)

static QTestState *rp2040_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static void pll_configure(QTestState *qts, uint32_t base,
                          uint32_t fbdiv, uint32_t postdiv1,
                          uint32_t postdiv2)
{
    qtest_writel(qts, base + PLL_CS, 1);
    qtest_writel(qts, base + PLL_FBDIV_INT, fbdiv);
    qtest_writel(qts, base + PLL_PWR, 0);
    qtest_writel(qts, base + PLL_PRIM, (postdiv1 << 16) | (postdiv2 << 12));
}

static uint32_t frequency_count_khz(QTestState *qts, uint32_t src)
{
    qtest_writel(qts, CLOCKS_BASE + FC0_SRC, src);
    g_assert_cmphex(qtest_readl(qts, CLOCKS_BASE + FC0_STATUS) &
                    FC0_STATUS_DONE, ==, FC0_STATUS_DONE);
    return qtest_readl(qts, CLOCKS_BASE + FC0_RESULT) >> 5;
}

static void test_clock_mux_and_frequency_counter(void)
{
    QTestState *qts = rp2040_start();

    pll_configure(qts, PLL_SYS_BASE, 125, 6, 2);
    pll_configure(qts, PLL_USB_BASE, 100, 5, 5);

    qtest_writel(qts, CLOCKS_BASE + CLK_SYS_DIV, 0x100);
    qtest_writel(qts, CLOCKS_BASE + CLK_PERI_DIV, 0x100);
    qtest_writel(qts, CLOCKS_BASE + CLK_USB_DIV, 0x100);
    qtest_writel(qts, CLOCKS_BASE + CLK_ADC_DIV, 0x100);
    qtest_writel(qts, CLOCKS_BASE + CLK_RTC_DIV, 0x400);

    qtest_writel(qts, CLOCKS_BASE + CLK_SYS_CTRL, BIT(0));
    qtest_writel(qts, CLOCKS_BASE + CLK_PERI_CTRL, CLK_CTRL_ENABLE);
    qtest_writel(qts, CLOCKS_BASE + CLK_USB_CTRL, CLK_CTRL_ENABLE);
    qtest_writel(qts, CLOCKS_BASE + CLK_ADC_CTRL, CLK_CTRL_ENABLE);
    qtest_writel(qts, CLOCKS_BASE + CLK_RTC_CTRL,
                 CLK_CTRL_ENABLE | (0x3 << 5));

    g_assert_cmpuint(frequency_count_khz(qts, 0x01), ==, 125000);
    g_assert_cmpuint(frequency_count_khz(qts, 0x02), ==, 48000);
    g_assert_cmpuint(frequency_count_khz(qts, 0x03), ==, 6000);
    g_assert_cmpuint(frequency_count_khz(qts, 0x09), ==, 125000);
    g_assert_cmpuint(frequency_count_khz(qts, 0x0a), ==, 125000);
    g_assert_cmpuint(frequency_count_khz(qts, 0x0b), ==, 48000);
    g_assert_cmpuint(frequency_count_khz(qts, 0x0c), ==, 48000);
    g_assert_cmpuint(frequency_count_khz(qts, 0x0d), ==, 3000);

    qtest_writel(qts, CLOCKS_BASE + CLK_SYS_CTRL, BIT(0) | (0x1 << 5));
    qtest_writel(qts, PLL_SYS_BASE + PLL_PWR, PLL_PWR_BITS);

    g_assert_cmpuint(frequency_count_khz(qts, 0x01), ==, 0);
    g_assert_cmpuint(frequency_count_khz(qts, 0x09), ==, 48000);
    g_assert_cmpuint(frequency_count_khz(qts, 0x0a), ==, 48000);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-clocks/mux-and-frequency-counter",
                   test_clock_mux_and_frequency_counter);

    return g_test_run();
}
