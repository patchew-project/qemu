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

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    global_qtest = qtest_initf("-machine microbit");

    qtest_add_func("/microbit/nrf51/nvmc", test_nrf51_nvmc);

    ret = g_test_run();

    qtest_quit(global_qtest);
    return ret;
}
