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


#define PAGE_SIZE           1024
#define FLASH_SIZE          (256 * PAGE_SIZE)
#define FLASH_BASE          0x00000000
#define UICR_BASE           0x10001000
#define UICR_SIZE           0x100
#define NVMC_BASE           0x4001E000UL
#define NVMC_READY          0x400
#define NVMC_CONFIG         0x504
#define NVMC_ERASEPAGE      0x508
#define NVMC_ERASEPCR1      0x508
#define NVMC_ERASEALL       0x50C
#define NVMC_ERASEPCR0      0x510
#define NVMC_ERASEUICR      0x514


static void fill_and_erase(hwaddr base, hwaddr size, uint32_t address_reg)
{
    /* Fill memory */
    writel(NVMC_BASE + NVMC_CONFIG, 0x01);
    for (hwaddr i = 0; i < size; ++i) {
        writeb(base + i, i);
        g_assert_cmpuint(readb(base + i), ==, i & 0xFF);
    }
    writel(NVMC_BASE + NVMC_CONFIG, 0x00);

    /* Erase Page */
    writel(NVMC_BASE + NVMC_CONFIG, 0x02);
    writel(NVMC_BASE + address_reg, base);
    writel(NVMC_BASE + NVMC_CONFIG, 0x00);

    /* Check memory */
    for (hwaddr i = 0; i < size; ++i) {
        g_assert_cmpuint(readb(base + i), ==, 0xFF);
    }
}

static void test_nrf51_nvmc(void)
{
    uint32_t value;
    /* Test always ready */
    value = readl(NVMC_BASE + NVMC_READY);
    g_assert_cmpuint(value & 0x01, ==, 0x01);

    /* Test write-read config register */
    writel(NVMC_BASE + NVMC_CONFIG, 0x03);
    g_assert_cmpuint(readl(NVMC_BASE + NVMC_CONFIG), ==, 0x03);
    writel(NVMC_BASE + NVMC_CONFIG, 0x00);
    g_assert_cmpuint(readl(NVMC_BASE + NVMC_CONFIG), ==, 0x00);

    /* Test PCR0 */
    fill_and_erase(FLASH_BASE, PAGE_SIZE, NVMC_ERASEPCR0);
    fill_and_erase(FLASH_BASE + PAGE_SIZE, PAGE_SIZE, NVMC_ERASEPCR0);

    /* Test PCR1 */
    fill_and_erase(FLASH_BASE, PAGE_SIZE, NVMC_ERASEPCR1);
    fill_and_erase(FLASH_BASE + PAGE_SIZE, PAGE_SIZE, NVMC_ERASEPCR1);

    /* Erase all */
    writel(NVMC_BASE + NVMC_CONFIG, 0x01);
    for (hwaddr i = 0; i < FLASH_SIZE / 4; i++) {
        writel(FLASH_BASE + i * 4, i);
        g_assert_cmpuint(readl(FLASH_BASE + i * 4), ==, i);
    }
    writel(NVMC_BASE + NVMC_CONFIG, 0x00);

    writel(NVMC_BASE + NVMC_CONFIG, 0x02);
    writel(NVMC_BASE + NVMC_ERASEALL, 0x01);
    writel(NVMC_BASE + NVMC_CONFIG, 0x00);

    for (hwaddr i = 0; i < FLASH_SIZE / 4; i++) {
        g_assert_cmpuint(readl(FLASH_BASE + i * 4), ==, 0xFFFFFFFF);
    }

    /* Erase UICR */
    writel(NVMC_BASE + NVMC_CONFIG, 0x01);
    for (hwaddr i = 0; i < UICR_SIZE / 4; i++) {
        writel(UICR_BASE + i * 4, i);
        g_assert_cmpuint(readl(UICR_BASE + i * 4), ==, i);
    }
    writel(NVMC_BASE + NVMC_CONFIG, 0x00);

    writel(NVMC_BASE + NVMC_CONFIG, 0x02);
    writel(NVMC_BASE + NVMC_ERASEUICR, 0x01);
    writel(NVMC_BASE + NVMC_CONFIG, 0x00);

    for (hwaddr i = 0; i < UICR_SIZE / 4; i++) {
        g_assert_cmpuint(readl(UICR_BASE + i * 4), ==, 0xFFFFFFFF);
    }
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    global_qtest = qtest_startf("-machine microbit");

    qtest_add_func("/microbit/nrf51/nvmc", test_nrf51_nvmc);

    ret = g_test_run();

    qtest_quit(global_qtest);
    return ret;
}
