/*
 * QTest testcase for STM32F100 GPIO (Rust device "stm32f1xx-gpio-rust")
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 QEMU contributors
 *
 * The STM32F1 GPIO differs from the L4 family: each pin is configured by a
 * 4-bit field MODE[1:0]+CNF[1:0] packed into CRL (pins 0-7) and CRH (pins
 * 8-15), instead of the separate MODER/OTYPER/PUPDR registers of the L4.
 * See ST RM0041 for the register map and reset values.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define GPIO_BASE_ADDR 0x40010800
#define GPIO_SIZE      0x400
#define NUM_GPIOS      5
#define NUM_GPIO_PINS  16

#define GPIO_A 0x40010800
#define GPIO_B 0x40010C00
#define GPIO_C 0x40011000
#define GPIO_D 0x40011400
#define GPIO_E 0x40011800

#define CRL  0x00
#define CRH  0x04
#define IDR  0x08
#define ODR  0x0C
#define BSRR 0x10
#define BRR  0x14
#define LCKR 0x18

#define CFG_INPUT_FLOATING   0x4
#define CFG_INPUT_ANALOG     0x0
#define CFG_INPUT_PULL       0x8
#define CFG_OUTPUT_PP        0x1
#define CFG_OUTPUT_OD        0x5
#define CFG_AF_PP            0x9

#define CR_RESET 0x44444444

static uint32_t gpio_readl(unsigned int gpio, unsigned int offset)
{
    return readl(gpio + offset);
}

static void gpio_writel(unsigned int gpio, unsigned int offset, uint32_t value)
{
    writel(gpio + offset, value);
}

static void gpio_set_config(unsigned int gpio, unsigned int pin, uint8_t cfg)
{
    unsigned int reg = (pin < 8) ? CRL : CRH;
    unsigned int shift = (pin % 8) * 4;
    uint32_t mask = ~(0xFu << shift);
    uint32_t val = (gpio_readl(gpio, reg) & mask) | ((uint32_t)cfg << shift);
    gpio_writel(gpio, reg, val);
}

static void gpio_set_odr_bit(unsigned int gpio, unsigned int pin,
                             uint32_t value)
{
    uint32_t mask = ~(0x1u << pin);
    gpio_writel(gpio, ODR, (gpio_readl(gpio, ODR) & mask) | (value << pin));
}

static unsigned int get_gpio_id(uint32_t gpio_addr)
{
    return (gpio_addr - GPIO_BASE_ADDR) / GPIO_SIZE;
}

static const char *gpio_path(uint32_t gpio)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "/machine/soc/gpio%c", 'a' + get_gpio_id(gpio));
    return buf;
}

static void gpio_set_irq(unsigned int gpio, int num, int level)
{
    qtest_set_irq_in(global_qtest, gpio_path(gpio), NULL, num, level);
}

/* qtest can intercept one device's outputs per session; use GPIOA. */
static void intercept_out(unsigned int gpio)
{
    qtest_irq_intercept_out(global_qtest, gpio_path(gpio));
}

static void reset_gpio(unsigned int gpio)
{
    gpio_writel(gpio, CRL, CR_RESET);
    gpio_writel(gpio, CRH, CR_RESET);
    gpio_writel(gpio, ODR, 0);
}

static void test_reset_values(void)
{
    gpio_writel(GPIO_A, CRL, 0xDEADBEEF);
    gpio_writel(GPIO_A, CRH, 0xDEADBEEF);
    gpio_writel(GPIO_A, ODR, 0xDEADBEEF);

    gpio_writel(GPIO_C, CRL, 0x12345678);
    gpio_writel(GPIO_C, ODR, 0x0000FFFF);

    qtest_system_reset(global_qtest);

    g_assert_cmphex(gpio_readl(GPIO_A, CRL), ==, CR_RESET);
    g_assert_cmphex(gpio_readl(GPIO_A, CRH), ==, CR_RESET);
    g_assert_cmphex(gpio_readl(GPIO_A, IDR), ==, 0);
    g_assert_cmphex(gpio_readl(GPIO_A, ODR), ==, 0);

    g_assert_cmphex(gpio_readl(GPIO_C, CRL), ==, CR_RESET);
    g_assert_cmphex(gpio_readl(GPIO_C, IDR), ==, 0);
    g_assert_cmphex(gpio_readl(GPIO_C, ODR), ==, 0);
}

static void test_output_mode(const void *data)
{
    unsigned int pin = (uintptr_t)data & 0xF;
    uint32_t gpio = (uintptr_t)data & ~(GPIO_SIZE - 1);
    bool observe = (gpio == GPIO_A);

    reset_gpio(gpio);
    if (observe) {
        intercept_out(gpio);
    }

    gpio_set_odr_bit(gpio, pin, 1);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, 0);
    if (observe) {
        g_assert_false(get_irq(pin));
    }

    gpio_set_config(gpio, pin, CFG_OUTPUT_PP);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, (1u << pin));
    if (observe) {
        g_assert_true(get_irq(pin));
    }

    gpio_set_odr_bit(gpio, pin, 0);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, 0);
    if (observe) {
        g_assert_false(get_irq(pin));
    }

    reset_gpio(gpio);
}

static void test_input_mode(const void *data)
{
    unsigned int pin = (uintptr_t)data & 0xF;
    uint32_t gpio = (uintptr_t)data & ~(GPIO_SIZE - 1);
    bool observe = (gpio == GPIO_A);

    reset_gpio(gpio);
    if (observe) {
        intercept_out(gpio);
    }

    gpio_set_config(gpio, pin, CFG_INPUT_FLOATING);

    gpio_set_irq(gpio, pin, 1);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, (1u << pin));
    if (observe) {
        g_assert_true(get_irq(pin));
    }

    gpio_set_irq(gpio, pin, 0);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, 0);
    if (observe) {
        g_assert_false(get_irq(pin));
    }

    reset_gpio(gpio);
}

static void test_pull_up_down(const void *data)
{
    unsigned int pin = (uintptr_t)data & 0xF;
    uint32_t gpio = (uintptr_t)data & ~(GPIO_SIZE - 1);

    reset_gpio(gpio);
    intercept_out(gpio);

    gpio_set_odr_bit(gpio, pin, 1);
    gpio_set_config(gpio, pin, CFG_INPUT_PULL);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, (1u << pin));
    g_assert_true(get_irq(pin));

    gpio_set_odr_bit(gpio, pin, 0);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, 0);
    g_assert_false(get_irq(pin));

    reset_gpio(gpio);
}

static void test_bsrr_brr(const void *data)
{
    unsigned int pin = (uintptr_t)data & 0xF;
    uint32_t gpio = (uintptr_t)data & ~(GPIO_SIZE - 1);

    reset_gpio(gpio);

    gpio_writel(gpio, BSRR, (1u << pin));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, (1u << pin));

    gpio_writel(gpio, BSRR, (1u << (pin + 16)));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, 0);

    gpio_writel(gpio, BSRR, (1u << pin));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, (1u << pin));

    gpio_writel(gpio, BRR, (1u << pin));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, 0);

    /* Set has priority over reset in BSRR. */
    gpio_writel(gpio, BSRR, (1u << pin) | (1u << (pin + 16)));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, (1u << pin));

    reset_gpio(gpio);
}

static void test_push_pull_disconnect(const void *data)
{
    unsigned int pin = (uintptr_t)data & 0xF;
    uint32_t gpio = (uintptr_t)data & ~(GPIO_SIZE - 1);

    reset_gpio(gpio);

    gpio_set_config(gpio, pin, CFG_INPUT_FLOATING);
    gpio_set_irq(gpio, pin, 1);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, (1u << pin));

    gpio_set_odr_bit(gpio, pin, 0);
    gpio_set_config(gpio, pin, CFG_OUTPUT_PP);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, 0);

    gpio_set_irq(gpio, pin, 1);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, 0);

    reset_gpio(gpio);
}

static void test_analog_input(void)
{
    reset_gpio(GPIO_A);
    gpio_set_config(GPIO_A, 0, CFG_INPUT_ANALOG);
    gpio_set_irq(GPIO_A, 0, 1);

    g_assert_cmphex(gpio_readl(GPIO_A, IDR) & 1, ==, 0);

    reset_gpio(GPIO_A);
}

static void test_open_drain_released(void)
{
    reset_gpio(GPIO_A);
    gpio_set_config(GPIO_A, 1, CFG_OUTPUT_OD);
    gpio_set_odr_bit(GPIO_A, 1, 1);

    gpio_set_irq(GPIO_A, 1, 1);
    g_assert_cmphex(gpio_readl(GPIO_A, IDR) & (1u << 1), ==, 1u << 1);

    gpio_set_irq(GPIO_A, 1, 0);
    g_assert_cmphex(gpio_readl(GPIO_A, IDR) & (1u << 1), ==, 0);

    reset_gpio(GPIO_A);
}

static void test_af_output_not_driven_by_odr(void)
{
    reset_gpio(GPIO_A);
    gpio_set_config(GPIO_A, 2, CFG_AF_PP);
    gpio_set_odr_bit(GPIO_A, 2, 1);

    g_assert_cmphex(gpio_readl(GPIO_A, IDR) & (1u << 2), ==, 0);

    reset_gpio(GPIO_A);
}

static void test_reset_deasserts_output(void)
{
    reset_gpio(GPIO_A);
    intercept_out(GPIO_A);
    gpio_set_config(GPIO_A, 3, CFG_OUTPUT_PP);
    gpio_set_odr_bit(GPIO_A, 3, 1);
    g_assert_true(get_irq(3));

    qtest_system_reset(global_qtest);

    g_assert_false(get_irq(3));
    g_assert_cmphex(gpio_readl(GPIO_A, ODR), ==, 0);
}

static void test_lckr_unimplemented(void)
{
    reset_gpio(GPIO_A);
    gpio_writel(GPIO_A, LCKR, 0x00010001);
    g_assert_cmphex(gpio_readl(GPIO_A, LCKR), ==, 0);
}

static void test_word_access_only(void)
{
    gpio_writel(GPIO_A, ODR, 0x55aa);

    writeb(GPIO_A + ODR, 0xff);
    g_assert_cmphex(gpio_readl(GPIO_A, ODR), ==, 0x55aa);

    writew(GPIO_A + ODR, 0xffff);
    g_assert_cmphex(gpio_readl(GPIO_A, ODR), ==, 0x55aa);

    writel(GPIO_A + ODR + 1, 0xffffffff);
    g_assert_cmphex(gpio_readl(GPIO_A, ODR), ==, 0x55aa);

    reset_gpio(GPIO_A);
}

static void test_machine_started(void)
{
    g_assert_nonnull(global_qtest);
}

static void register_gpio_tests(void)
{
    qtest_add_func("stm32f100/gpio/reset_values", test_reset_values);
    qtest_add_data_func("stm32f100/gpio/output_mode_a5",
                        (void *)(uintptr_t)(GPIO_A | 5), test_output_mode);
    qtest_add_data_func("stm32f100/gpio/output_mode_c13",
                        (void *)(uintptr_t)(GPIO_C | 13), test_output_mode);
    qtest_add_data_func("stm32f100/gpio/input_mode_b6",
                        (void *)(uintptr_t)(GPIO_B | 6), test_input_mode);
    qtest_add_data_func("stm32f100/gpio/input_mode_d10",
                        (void *)(uintptr_t)(GPIO_D | 10), test_input_mode);
    qtest_add_data_func("stm32f100/gpio/pull_up_down_a0",
                        (void *)(uintptr_t)(GPIO_A | 0), test_pull_up_down);
    qtest_add_data_func("stm32f100/gpio/bsrr_brr_a1",
                        (void *)(uintptr_t)(GPIO_A | 1), test_bsrr_brr);
    qtest_add_data_func("stm32f100/gpio/bsrr_brr_e12",
                        (void *)(uintptr_t)(GPIO_E | 12), test_bsrr_brr);
    qtest_add_data_func("stm32f100/gpio/push_pull_disconnect_e7",
                        (void *)(uintptr_t)(GPIO_E | 7),
                        test_push_pull_disconnect);
    qtest_add_func("stm32f100/gpio/analog_input", test_analog_input);
    qtest_add_func("stm32f100/gpio/open_drain_released",
                   test_open_drain_released);
    qtest_add_func("stm32f100/gpio/af_output_not_driven_by_odr",
                   test_af_output_not_driven_by_odr);
    qtest_add_func("stm32f100/gpio/reset_deasserts_output",
                   test_reset_deasserts_output);
    qtest_add_func("stm32f100/gpio/lckr_unimplemented",
                   test_lckr_unimplemented);
    qtest_add_func("stm32f100/gpio/word_access_only",
                   test_word_access_only);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    if (qtest_has_device("stm32f1xx-gpio-rust")) {
        register_gpio_tests();
    } else {
        qtest_add_func("stm32f100/gpio/no_rust_machine_start",
                       test_machine_started);
    }

    qtest_start("-machine stm32vldiscovery");
    ret = g_test_run();
    qtest_end();

    return ret;
}
