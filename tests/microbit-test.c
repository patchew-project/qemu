 /*
 * QTest testcase for Microbit board using the Nordic Semiconductor nRF51 SoC.
 *
 * nRF51:
 * Reference Manual: http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.pdf
 * Product Spec: http://infocenter.nordicsemi.com/pdf/nRF51822_PS_v3.1.pdf
 *
 * Microbit Board: http://microbit.org/
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */


#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "libqtest.h"

#include "hw/arm/nrf51.h"
#include "hw/nvram/nrf51_nvm.h"
#include "hw/gpio/nrf51_gpio.h"

#define FLASH_SIZE          (256 * NRF51_PAGE_SIZE)

static void fill_and_erase(hwaddr base, hwaddr size, uint32_t address_reg)
{
    hwaddr i;

    /* Erase Page */
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    writel(NRF51_NVMC_BASE + address_reg, base);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    /* Check memory */
    for (i = 0; i < size / 4; i++) {
        g_assert_cmpuint(readl(base + i * 4), ==, 0xFFFFFFFF);
    }

    /* Fill memory */
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x01);
    for (i = 0; i < size / 4; i++) {
        writel(base + i * 4, i);
        g_assert_cmpuint(readl(base + i * 4), ==, i);
    }
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);
}

static void test_nrf51_nvmc(void)
{
    uint32_t value;
    hwaddr i;

    /* Test always ready */
    value = readl(NRF51_NVMC_BASE + NRF51_NVMC_READY);
    g_assert_cmpuint(value & 0x01, ==, 0x01);

    /* Test write-read config register */
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x03);
    g_assert_cmpuint(readl(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG), ==, 0x03);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);
    g_assert_cmpuint(readl(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG), ==, 0x00);

    /* Test PCR0 */
    fill_and_erase(NRF51_FLASH_BASE, NRF51_PAGE_SIZE, NRF51_NVMC_ERASEPCR0);
    fill_and_erase(NRF51_FLASH_BASE + NRF51_PAGE_SIZE,
                   NRF51_PAGE_SIZE, NRF51_NVMC_ERASEPCR0);

    /* Test PCR1 */
    fill_and_erase(NRF51_FLASH_BASE, NRF51_PAGE_SIZE, NRF51_NVMC_ERASEPCR1);
    fill_and_erase(NRF51_FLASH_BASE + NRF51_PAGE_SIZE,
                   NRF51_PAGE_SIZE, NRF51_NVMC_ERASEPCR1);

    /* Erase all */
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_ERASEALL, 0x01);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x01);
    for (i = 0; i < FLASH_SIZE / 4; i++) {
        writel(NRF51_FLASH_BASE + i * 4, i);
        g_assert_cmpuint(readl(NRF51_FLASH_BASE + i * 4), ==, i);
    }
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_ERASEALL, 0x01);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    for (i = 0; i < FLASH_SIZE / 4; i++) {
        g_assert_cmpuint(readl(NRF51_FLASH_BASE + i * 4), ==, 0xFFFFFFFF);
    }

    /* Erase UICR */
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_ERASEUICR, 0x01);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    for (i = 0; i < NRF51_UICR_SIZE / 4; i++) {
        g_assert_cmpuint(readl(NRF51_UICR_BASE + i * 4), ==, 0xFFFFFFFF);
    }

    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x01);
    for (i = 0; i < NRF51_UICR_SIZE / 4; i++) {
        writel(NRF51_UICR_BASE + i * 4, i);
        g_assert_cmpuint(readl(NRF51_UICR_BASE + i * 4), ==, i);
    }
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_ERASEUICR, 0x01);
    writel(NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    for (i = 0; i < NRF51_UICR_SIZE / 4; i++) {
        g_assert_cmpuint(readl(NRF51_UICR_BASE + i * 4), ==, 0xFFFFFFFF);
    }
}

static void test_nrf51_gpio(void)
{
    size_t i;
    uint32_t actual, expected;

    struct {
        hwaddr addr;
        uint32_t expected;
    } const reset_state[] = {
        {NRF51_GPIO_REG_OUT, 0x00000000}, {NRF51_GPIO_REG_OUTSET, 0x00000000},
        {NRF51_GPIO_REG_OUTCLR, 0x00000000}, {NRF51_GPIO_REG_IN, 0x00000000},
        {NRF51_GPIO_REG_DIR, 0x00000000}, {NRF51_GPIO_REG_DIRSET, 0x00000000},
        {NRF51_GPIO_REG_DIRCLR, 0x00000000}
    };

    /* Check reset state */
    for (i = 0; i < ARRAY_SIZE(reset_state); i++) {
        expected = reset_state[i].expected;
        actual = readl(NRF51_GPIO_BASE + reset_state[i].addr);
        g_assert_cmpuint(actual, ==, expected);
    }

    for (i = 0; i < NRF51_GPIO_PINS; i++) {
        expected = 0x00000002;
        actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START + i * 4);
        g_assert_cmpuint(actual, ==, expected);
    }

    /* Check dir bit consistency between dir and cnf */
    /* Check set via DIRSET */
    expected = 0x80000001;
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIRSET, expected);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR);
    g_assert_cmpuint(actual, ==, expected);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_END) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);

    /* Check clear via DIRCLR */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIRCLR, 0x80000001);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR);
    g_assert_cmpuint(actual, ==, 0x00000000);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_END) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);

    /* Check set via DIR */
    expected = 0x80000001;
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR, expected);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR);
    g_assert_cmpuint(actual, ==, expected);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_END) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);

    /* Reset DIR */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR, 0x00000000);

    /* Check Input propagates */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x00);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 0);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 1);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, -1);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x02);

    /* Check pull-up working */
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 0);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0000);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b1110);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x02);

    /* Check pull-down working */
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 1);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0000);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0110);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x02);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, -1);

    /* Check Output propagates */
    irq_intercept_out("/machine/nrf51");
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0011);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTSET, 0x01);
    g_assert_true(get_irq(0));
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTCLR, 0x01);
    g_assert_false(get_irq(0));

    /* Check self-stimulation */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTSET, 0x01);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);

    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTCLR, 0x01);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);

    /*
     * Check short-circuit - generates an guest_error which must be checked
     * manually as long as qtest can not scan qemu_log messages
     */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTSET, 0x01);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 0);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    global_qtest = qtest_initf("-machine microbit");

    qtest_add_func("/microbit/nrf51/nvmc", test_nrf51_nvmc);
    qtest_add_func("/microbit/nrf51/gpio", test_nrf51_gpio);

    ret = g_test_run();

    qtest_quit(global_qtest);
    return ret;
}
