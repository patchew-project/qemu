/*
 * QTest testcase for STM32L4x5_EXTI
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define GPIO_A_BASE_ADDR 0x48000000
#define GPIO_B_BASE_ADDR 0x48000400
#define GPIO_C_BASE_ADDR 0x48000800
#define GPIO_D_BASE_ADDR 0x48000C00
#define GPIO_E_BASE_ADDR 0x48001000
#define GPIO_F_BASE_ADDR 0x48001400
#define GPIO_G_BASE_ADDR 0x48001800
#define GPIO_H_BASE_ADDR 0x48001C00
#define GPIO_MODER 0x00
#define GPIO_OTYPER 0x04
#define GPIO_OSPEEDR 0x08
#define GPIO_PUPDR 0x0C
#define GPIO_IDR 0x10
#define GPIO_ODR 0x14
#define GPIO_BSRR 0x18
#define GPIO_LCKR 0x1C
#define GPIO_AFRL 0x20
#define GPIO_AFRH 0x24
#define GPIO_BRR 0x28
#define GPIO_ASCR 0x2C
#define GPIO_DISCONNECTED_PINS 0x30

static uint32_t gpio_a_readl(unsigned int offset)
{
    return readl(GPIO_A_BASE_ADDR + offset);
}

static uint32_t gpio_b_readl(unsigned int offset)
{
    return readl(GPIO_B_BASE_ADDR + offset);
}

static uint32_t gpio_c_readl(unsigned int offset)
{
    return readl(GPIO_C_BASE_ADDR + offset);
}

static uint32_t gpio_h_readl(unsigned int offset)
{
    return readl(GPIO_H_BASE_ADDR + offset);
}

static void gpio_a_writel(unsigned int offset, uint32_t value)
{
    writel(GPIO_A_BASE_ADDR + offset, value);
}

static void gpio_a_set_irq(int num, int level)
{
    qtest_set_irq_in(global_qtest, "/machine/soc/gpioa",
                     NULL, num, level);
}

static void test_idr_reset_value(void)
{
    /*
     * Check that IDR register as well as other registers
     * determining its value have the exepcted value
     * GPIOA->IDR value after reset is not identical
     * to the real one ad Alternate Functions aren't
     * implemented
     */
    uint32_t moder = gpio_a_readl(GPIO_MODER);
    uint32_t odr = gpio_a_readl(GPIO_ODR);
    uint32_t otyper = gpio_a_readl(GPIO_OTYPER);
    uint32_t pupdr = gpio_a_readl(GPIO_PUPDR);
    uint32_t idr = gpio_a_readl(GPIO_IDR);
    /* 15: AF, 14: AF, 13: AF, 12: Analog ... */
    /* here AF is the same as Analog */
    g_assert_cmpint(moder, ==, 0xABFFFFFF);
    g_assert_cmpint(odr, ==, 0x00000000);
    g_assert_cmpint(otyper, ==, 0x00000000);
    /* 15: pull-up, 14: pull-down, 13: pull-up, 12: neither ... */
    g_assert_cmpint(pupdr, ==, 0x64000000);
    /* 15 : 1, 14: 0, 13: 1, 12 : reset value ... */
    g_assert_cmpint(idr, ==, 0x0000A000);

    moder = gpio_b_readl(GPIO_MODER);
    odr = gpio_b_readl(GPIO_ODR);
    otyper = gpio_b_readl(GPIO_OTYPER);
    pupdr = gpio_b_readl(GPIO_PUPDR);
    idr = gpio_b_readl(GPIO_IDR);
    /* ... 5: Analog, 4: AF, 3: AF, 2: Analog ... */
    /* here AF is the same as Analog */
    g_assert_cmpint(moder, ==, 0xFFFFFEBF);
    g_assert_cmpint(odr, ==, 0x00000000);
    g_assert_cmpint(otyper, ==, 0x00000000);
    /* ... 5: neither, 4: pull-up, 3: neither ... */
    g_assert_cmpint(pupdr, ==, 0x00000100);
    /* ... 5 : reset value, 4 : 1, 3 : reset value ... */
    g_assert_cmpint(idr, ==, 0x00000010);

    moder = gpio_c_readl(GPIO_MODER);
    odr = gpio_c_readl(GPIO_ODR);
    otyper = gpio_c_readl(GPIO_OTYPER);
    pupdr = gpio_c_readl(GPIO_PUPDR);
    idr = gpio_c_readl(GPIO_IDR);
    /* Analog */
    g_assert_cmpint(moder, ==, 0xFFFFFFFF);
    g_assert_cmpint(odr, ==, 0x00000000);
    g_assert_cmpint(otyper, ==, 0x00000000);
    /* no pull-up or pull-down */
    g_assert_cmpint(pupdr, ==, 0x00000000);
    /* reset value */
    g_assert_cmpint(idr, ==, 0x00000000);

    moder = gpio_h_readl(GPIO_MODER);
    odr = gpio_h_readl(GPIO_ODR);
    otyper = gpio_h_readl(GPIO_OTYPER);
    pupdr = gpio_h_readl(GPIO_PUPDR);
    idr = gpio_h_readl(GPIO_IDR);
    /* Analog */
    g_assert_cmpint(moder, ==, 0x0000000F);
    g_assert_cmpint(odr, ==, 0x00000000);
    g_assert_cmpint(otyper, ==, 0x00000000);
    /* no pull-up or pull-down */
    g_assert_cmpint(pupdr, ==, 0x00000000);
    /* reset value */
    g_assert_cmpint(idr, ==, 0x00000000);

}

static void test_gpio_output_mode(void)
{
    /*
     * Test that setting and resetting a bit in ODR sends signal
     * to SYSCFG when this bit is configured in output mode
     * (even if output mode if configured after the bit in ODR is set)
     */
    qtest_irq_intercept_in(global_qtest, "/machine/soc/syscfg");

    /* Set bit 0 in ODR */
    gpio_a_writel(GPIO_ODR, 0x00000001);

    /* Check that IDR wasn't updated */
    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A000);

    /* Check no signal was transmitted to syscfg */
    g_assert_false(get_irq(0));

    /* Configure GPIOA line 0 as output */
    gpio_a_writel(GPIO_MODER, 0xABFFFFFD);

    /* Check that IDR was updated */
    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A001);

    /* Check that the signal was transmitted to syscfg */
    g_assert_true(get_irq(0));

    /* Clean the test */
    gpio_a_writel(GPIO_ODR, 0x00000000);
    gpio_a_writel(GPIO_MODER, 0xABFFFFFF);
}

static void test_gpio_input_mode(void)
{
    /*
     * Test that configuring a line in input mode allows to send
     * a signal to SYSCFG when raising and lowering the line
     */
    qtest_irq_intercept_in(global_qtest,
                           "/machine/soc/syscfg");

    /* Configure GPIOA line 0 as input */
    gpio_a_writel(GPIO_MODER, 0x00000000);

    /* Raise line 0 */
    gpio_a_set_irq(0, 1);

    /* Check that IDR was updated */
    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A001);

    /* Check that the signal was transmitted to syscfg */
    g_assert_true(get_irq(0));

    /* Lower line 0 */
    gpio_a_set_irq(0, 0);

    /* Check that IDR was updated */
    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A000);

    /* Check that the signal was transmitted to syscfg */
    g_assert_false(get_irq(0));

    /* Clean the test */
    gpio_a_writel(GPIO_ODR, 0x00000000);
    gpio_a_writel(GPIO_MODER, 0xABFFFFFF);
    gpio_a_writel(GPIO_DISCONNECTED_PINS, 0xFFFF);
}

static void test_pull_up_pull_down(void)
{
    /*
     * Test that configuring a line in input mode allows to send
     * a signal to SYSCFG just by changing pull-up and pull-down
     */
    qtest_irq_intercept_in(global_qtest,
                           "/machine/soc/syscfg");

    /* Configure GPIOA line 0 as input */
    gpio_a_writel(GPIO_MODER, 0x00000000);

    /* Configure pull-up for GPIOA line 0 */
    gpio_a_writel(GPIO_PUPDR, 0x00000001);

    /* Check that IDR was updated */
    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A001);

    /* Check that the signal was transmitted to syscfg */
    g_assert_true(get_irq(0));

    /* Configure pull-down for GPIOA line 0 */
    gpio_a_writel(GPIO_PUPDR, 0x00000002);

    /* Check that IDR was updated */
    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A000);

    /* Check that the signal was transmitted to syscfg */
    g_assert_false(get_irq(0));

    /* Clean the test */
    gpio_a_writel(GPIO_ODR, 0x00000000);
    gpio_a_writel(GPIO_MODER, 0xABFFFFFF);
}

static void test_no_short_circuit(void)
{
    /*
     * Test that configuring a line in output mode
     * disconnects the pin, that the pin can't be set or reset
     * in push-pull mode, and that it can only be reset
     * in open-drain mode
     */
    qtest_irq_intercept_in(global_qtest,
                           "/machine/soc/syscfg");

    gpio_a_set_irq(0, 1);

    /* Configuring pin 0 in push-pull output mode */
    gpio_a_writel(GPIO_MODER, 0x00000001);

    /* Checking that the pin is disconnected */
    g_assert_cmpuint(gpio_a_readl(GPIO_DISCONNECTED_PINS), ==, 0xFFFF);

    /* Checking that IDR was updated accordingly */
    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A000);

    /* Trying to set and reset the pin and checking it doesn't work */
    gpio_a_set_irq(0, 1);

    g_assert_cmpuint(gpio_a_readl(GPIO_DISCONNECTED_PINS), ==, 0xFFFF);

    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A000);

    gpio_a_writel(GPIO_ODR, 0x00000001);

    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A001);

    gpio_a_set_irq(0, 0);

    g_assert_cmpuint(gpio_a_readl(GPIO_DISCONNECTED_PINS), ==, 0xFFFF);

    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A001);

    /* Configuring pin 0 in open-drain output mode */
    gpio_a_writel(GPIO_OTYPER, 0x00000001);

    /* Trying to set the pin and checking it doesn't work */
    gpio_a_set_irq(0, 1);

    g_assert_cmpuint(gpio_a_readl(GPIO_DISCONNECTED_PINS), ==, 0xFFFF);

    /* Resetting the pin and checking it works */
    gpio_a_set_irq(0, 0);

    g_assert_cmpuint(gpio_a_readl(GPIO_DISCONNECTED_PINS), ==, 0xFFFE);

    g_assert_cmpuint(gpio_a_readl(GPIO_IDR), ==, 0x0000A000);

    /* Cleaning the test */
    gpio_a_writel(GPIO_DISCONNECTED_PINS, 0xFFFF);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();
    qtest_add_func("stm32l4x5/gpio/test_idr_reset_value",
                   test_idr_reset_value);
    qtest_add_func("stm32l4x5/gpio/test_gpio_output_mode",
                   test_gpio_output_mode);
    qtest_add_func("stm32l4x5/gpio/test_gpio_input_mode",
                   test_gpio_input_mode);
    qtest_add_func("stm32l4x5/gpio/test_pull_up_pull_down",
                   test_pull_up_pull_down);
    qtest_add_func("stm32l4x5/gpio/test_no_short_circuit",
                   test_no_short_circuit);

    qtest_start("-machine b-l475e-iot01a");
    ret = g_test_run();
    qtest_end();

    return ret;
}
