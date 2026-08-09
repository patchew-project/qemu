/*
 * QTest testcase for K230 GPIO
 *
 * Copyright (c) 2025 Wang Guochun <wdasn99@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18)
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"
#include "hw/gpio/k230_gpio.h"

#define K230_GPIO0_BASE  0x9140B000
#define K230_GPIO1_BASE  0x9140C000

#define GPIO_BASE K230_GPIO0_BASE

#define PLIC_BASE           0xF00000000ULL
#define PLIC_PRIORITY       (PLIC_BASE + 0x000000)
#define PLIC_PENDING        (PLIC_BASE + 0x001000)
#define PLIC_ENABLE         (PLIC_BASE + 0x002000)
#define PLIC_CONTEXT        (PLIC_BASE + 0x200000)
#define PLIC_THRESHOLD      (PLIC_CONTEXT + 0x0)
#define PLIC_CLAIM          (PLIC_CONTEXT + 0x4)

#define GPIO0_IRQ_BASE  32
#define GPIO1_IRQ_BASE  64

static inline bool plic_irq_pending(QTestState *qts, int irq)
{
    uint32_t word = qtest_readl(qts, PLIC_PENDING + (irq / 32) * 4);
    return extract32(word, irq % 32, 1);
}

static void gpio0_set_input(QTestState *qts, int line, int level)
{
    qtest_set_irq_in(qts, "/machine/soc/k230-gpio0",
                     "unnamed-gpio-in", line, level);
}

static void gpio1_set_input(QTestState *qts, int line, int level)
{
    qtest_set_irq_in(qts, "/machine/soc/k230-gpio1",
                     "unnamed-gpio-in", line, level);
}

static void plic_enable_irq(QTestState *qts, int irq)
{
    qtest_writel(qts, PLIC_PRIORITY + irq * 4, 1);
    qtest_writel(qts, PLIC_THRESHOLD, 0);
    qtest_writel(qts, PLIC_ENABLE + (irq / 32) * 4, BIT(irq % 32));
}

static void plic_claim_complete(QTestState *qts, int irq)
{
    uint32_t claimed = qtest_readl(qts, PLIC_CLAIM);
    g_assert_cmpuint(claimed, ==, irq);
    qtest_writel(qts, PLIC_CLAIM, irq);
}

static uint32_t gpio0_read(QTestState *qts, hwaddr offset)
{
    return qtest_readl(qts, GPIO_BASE + offset);
}

static void gpio0_write(QTestState *qts, hwaddr offset, uint32_t value)
{
    qtest_writel(qts, GPIO_BASE + offset, value);
}

static uint32_t gpio0_raw_intstatus(QTestState *qts)
{
    return gpio0_read(qts, K230_GPIO_RAW_INTSTATUS);
}

static void test_reset_values(void)
{
    QTestState *qts = qtest_init("-machine k230");

    g_assert_cmphex(gpio0_read(qts, K230_GPIO_SWPORTA_DR), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_SWPORTA_DDR), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_SWPORTA_CTL), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_INTEN), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_INTMASK), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_INTTYPE_LEVEL), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_INT_POLARITY), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_INTSTATUS), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_RAW_INTSTATUS), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_CONFIG_REG2), ==, 0x0);
    g_assert_cmphex(gpio0_read(qts, K230_GPIO_CONFIG_REG1), ==, 0x0);

    qtest_quit(qts);
}

static void test_edge_rising(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 5;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(5));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(5));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(5));

    gpio0_set_input(qts, 5, 0);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    gpio0_set_input(qts, 5, 1);
    g_assert_true(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_edge_falling(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 3;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(3));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, 0);
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(3));

    gpio0_set_input(qts, 3, 1);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    gpio0_set_input(qts, 3, 0);
    g_assert_true(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_both_edge(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 7;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(7));
    gpio0_write(qts, K230_GPIO_INT_BOTHEDGE, BIT(7));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(7));

    gpio0_set_input(qts, 7, 0);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    gpio0_set_input(qts, 7, 1);
    g_assert_true(plic_irq_pending(qts, gpio_irq));

    gpio0_write(qts, K230_GPIO_PORTA_EOI, BIT(7));
    g_assert_cmphex(gpio0_raw_intstatus(qts) & BIT(7), ==, 0);
    plic_claim_complete(qts, gpio_irq);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    gpio0_set_input(qts, 7, 0);
    g_assert_true(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_level_high(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 2;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, 0);
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(2));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(2));

    gpio0_set_input(qts, 2, 0);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    gpio0_set_input(qts, 2, 1);
    g_assert_true(plic_irq_pending(qts, gpio_irq));

    gpio0_set_input(qts, 2, 0);
    g_assert_cmphex(gpio0_raw_intstatus(qts) & BIT(2), ==, 0);
    plic_claim_complete(qts, gpio_irq);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_level_low(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 4;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, 0);
    gpio0_write(qts, K230_GPIO_INT_POLARITY, 0);
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(4));

    gpio0_set_input(qts, 4, 1);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    gpio0_set_input(qts, 4, 0);
    g_assert_true(plic_irq_pending(qts, gpio_irq));

    /*
     * Level goes away: GPIO deasserts, but the PLIC pending bit is
     * sticky. Verify the GPIO-side status is cleared, then
     * claim/complete to clear the PLIC pending bit.
     */
    gpio0_set_input(qts, 4, 1);
    g_assert_cmphex(gpio0_raw_intstatus(qts) & BIT(4), ==, 0);
    plic_claim_complete(qts, gpio_irq);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_eoi_clear(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 10;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(10));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(10));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(10));

    gpio0_set_input(qts, 10, 0);
    gpio0_set_input(qts, 10, 1);
    g_assert_true(plic_irq_pending(qts, gpio_irq));

    /*
     * EOI clears the GPIO-side raw status, then claim/complete
     * clears the sticky PLIC pending bit.
     */
    gpio0_write(qts, K230_GPIO_PORTA_EOI, BIT(10));
    g_assert_cmphex(gpio0_raw_intstatus(qts) & BIT(10), ==, 0);
    plic_claim_complete(qts, gpio_irq);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_inten_disabled(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 15;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(15));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(15));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, 0);

    gpio0_set_input(qts, 15, 0);
    gpio0_set_input(qts, 15, 1);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_intmask(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 20;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(20));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(20));
    gpio0_write(qts, K230_GPIO_INTMASK, BIT(20));
    gpio0_write(qts, K230_GPIO_INTEN, BIT(20));

    gpio0_set_input(qts, 20, 0);
    gpio0_set_input(qts, 20, 1);

    g_assert_cmphex(gpio0_raw_intstatus(qts) & BIT(20), ==, BIT(20));
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_ddr_output_no_int(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 25;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, BIT(25));
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(25));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(25));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(25));

    gpio0_set_input(qts, 25, 0);
    gpio0_set_input(qts, 25, 1);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_ctl_hw_no_int(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 27;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_SWPORTA_CTL, BIT(27));
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(27));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(27));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(27));

    gpio0_set_input(qts, 27, 0);
    gpio0_set_input(qts, 27, 1);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_gpio_plic(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO0_IRQ_BASE + 6;

    plic_enable_irq(qts, gpio_irq);

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(6));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(6));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(6));

    gpio0_set_input(qts, 6, 0);
    gpio0_set_input(qts, 6, 1);

    g_assert_true(plic_irq_pending(qts, gpio_irq));

    /*
     * EOI clears the GPIO-side raw status, then claim/complete
     * clears the sticky PLIC pending bit and returns the claimed
     * IRQ id.
     */
    gpio0_write(qts, K230_GPIO_PORTA_EOI, BIT(6));
    plic_claim_complete(qts, gpio_irq);
    g_assert_false(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_multiple_irqs(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int irq_a = GPIO0_IRQ_BASE + 1;
    int irq_b = GPIO0_IRQ_BASE + 9;

    qtest_writel(qts, PLIC_PRIORITY + irq_a * 4, 1);
    qtest_writel(qts, PLIC_PRIORITY + irq_b * 4, 1);
    qtest_writel(qts, PLIC_THRESHOLD, 0);
    qtest_writel(qts, PLIC_ENABLE + (irq_a / 32) * 4,
                 BIT(irq_a % 32) | BIT(irq_b % 32));

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(1) | BIT(9));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(1) | BIT(9));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(1) | BIT(9));

    gpio0_set_input(qts, 1, 0);
    gpio0_set_input(qts, 9, 0);

    gpio0_set_input(qts, 1, 1);
    g_assert_true(plic_irq_pending(qts, irq_a));
    g_assert_false(plic_irq_pending(qts, irq_b));

    gpio0_set_input(qts, 9, 1);
    g_assert_true(plic_irq_pending(qts, irq_a));
    g_assert_true(plic_irq_pending(qts, irq_b));

    qtest_quit(qts);
}

static void test_gpio1_plic(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int gpio_irq = GPIO1_IRQ_BASE + 5;

    plic_enable_irq(qts, gpio_irq);

    qtest_writel(qts, K230_GPIO1_BASE + K230_GPIO_SWPORTA_DDR, 0);
    qtest_writel(qts, K230_GPIO1_BASE + K230_GPIO_INTTYPE_LEVEL, BIT(5));
    qtest_writel(qts, K230_GPIO1_BASE + K230_GPIO_INT_POLARITY, BIT(5));
    qtest_writel(qts, K230_GPIO1_BASE + K230_GPIO_INTMASK, 0);
    qtest_writel(qts, K230_GPIO1_BASE + K230_GPIO_INTEN, BIT(5));

    gpio1_set_input(qts, 5, 0);
    gpio1_set_input(qts, 5, 1);

    g_assert_true(plic_irq_pending(qts, gpio_irq));

    qtest_quit(qts);
}

static void test_ddr_switch_output_to_input(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int level_irq = GPIO0_IRQ_BASE + 12;
    int edge_irq = GPIO0_IRQ_BASE + 13;

    qtest_writel(qts, PLIC_PRIORITY + level_irq * 4, 1);
    qtest_writel(qts, PLIC_PRIORITY + edge_irq * 4, 1);
    qtest_writel(qts, PLIC_THRESHOLD, 0);
    qtest_writel(qts, PLIC_ENABLE + (level_irq / 32) * 4,
                 BIT(level_irq % 32) | BIT(edge_irq % 32));

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, BIT(12) | BIT(13));
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(13));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(12) | BIT(13));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(12) | BIT(13));

    gpio0_set_input(qts, 12, 0);
    gpio0_set_input(qts, 13, 0);

    gpio0_set_input(qts, 12, 1);
    gpio0_set_input(qts, 13, 1);

    g_assert_false(plic_irq_pending(qts, level_irq));
    g_assert_false(plic_irq_pending(qts, edge_irq));

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);

    g_assert_true(plic_irq_pending(qts, level_irq));
    g_assert_false(plic_irq_pending(qts, edge_irq));

    qtest_quit(qts);
}

static void test_ctl_switch_hw_to_sw(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int level_irq = GPIO0_IRQ_BASE + 14;
    int edge_irq = GPIO0_IRQ_BASE + 16;

    qtest_writel(qts, PLIC_PRIORITY + level_irq * 4, 1);
    qtest_writel(qts, PLIC_PRIORITY + edge_irq * 4, 1);
    qtest_writel(qts, PLIC_THRESHOLD, 0);
    qtest_writel(qts, PLIC_ENABLE + (level_irq / 32) * 4,
                 BIT(level_irq % 32) | BIT(edge_irq % 32));

    gpio0_write(qts, K230_GPIO_SWPORTA_DDR, 0);
    gpio0_write(qts, K230_GPIO_SWPORTA_CTL, BIT(14) | BIT(16));
    gpio0_write(qts, K230_GPIO_INTTYPE_LEVEL, BIT(16));
    gpio0_write(qts, K230_GPIO_INT_POLARITY, BIT(14) | BIT(16));
    gpio0_write(qts, K230_GPIO_INTMASK, 0);
    gpio0_write(qts, K230_GPIO_INTEN, BIT(14) | BIT(16));

    gpio0_set_input(qts, 14, 0);
    gpio0_set_input(qts, 16, 0);

    gpio0_set_input(qts, 14, 1);
    gpio0_set_input(qts, 16, 1);

    g_assert_false(plic_irq_pending(qts, level_irq));
    g_assert_false(plic_irq_pending(qts, edge_irq));

    gpio0_write(qts, K230_GPIO_SWPORTA_CTL, 0);

    g_assert_true(plic_irq_pending(qts, level_irq));
    g_assert_false(plic_irq_pending(qts, edge_irq));

    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-gpio/reset_values", test_reset_values);
    qtest_add_func("/k230-gpio/edge_rising", test_edge_rising);
    qtest_add_func("/k230-gpio/edge_falling", test_edge_falling);
    qtest_add_func("/k230-gpio/both_edge", test_both_edge);
    qtest_add_func("/k230-gpio/level_high", test_level_high);
    qtest_add_func("/k230-gpio/level_low", test_level_low);
    qtest_add_func("/k230-gpio/eoi_clear", test_eoi_clear);
    qtest_add_func("/k230-gpio/inten_disabled", test_inten_disabled);
    qtest_add_func("/k230-gpio/intmask", test_intmask);
    qtest_add_func("/k230-gpio/ddr_output_no_int", test_ddr_output_no_int);
    qtest_add_func("/k230-gpio/ctl_hw_no_int", test_ctl_hw_no_int);
    qtest_add_func("/k230-gpio/gpio_plic", test_gpio_plic);
    qtest_add_func("/k230-gpio/multiple_irqs", test_multiple_irqs);
    qtest_add_func("/k230-gpio/gpio1_plic", test_gpio1_plic);
    qtest_add_func("/k230-gpio/ddr_switch_out_to_in",
                   test_ddr_switch_output_to_input);
    qtest_add_func("/k230-gpio/ctl_switch_hw_to_sw",
                   test_ctl_switch_hw_to_sw);

    return g_test_run();
}
