/*
 * QTest testcase for STML4XX_EXTI
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define EXTI_BASE_ADDR 0x40010400
#define EXTI_IMR1 0x00
#define EXTI_EMR1 0x04
#define EXTI_RTSR1 0x08
#define EXTI_FTSR1 0x0C
#define EXTI_SWIER1 0x10
#define EXTI_PR1 0x14
#define EXTI_IMR2 0x20
#define EXTI_EMR2 0x24
#define EXTI_RTSR2 0x28
#define EXTI_FTSR2 0x2C
#define EXTI_SWIER2 0x30
#define EXTI_PR2 0x34

#define GPIO_0_IRQ 6

static void exti_writel(unsigned int offset, uint32_t value)
{
    writel(EXTI_BASE_ADDR + offset, value);
}

static uint32_t exti_readl(unsigned int offset)
{
    return readl(EXTI_BASE_ADDR + offset);
}

static void test_write_read(void)
{
    /* Test that we can write and retrieve a value from the device */
    exti_writel(EXTI_IMR1, 0xFFFFFFFF);
    const uint32_t imr1 = exti_readl(EXTI_IMR1);
    g_assert_cmpuint(imr1, ==, 0xFFFFFFFF);

    /* Test that reserved address are not written to */
    exti_writel(EXTI_IMR2, 0xFFFFFFFF);
    const uint32_t imr2 = exti_readl(EXTI_IMR2);
    g_assert_cmpuint(imr2, ==, 0x000001FF);
}

static void test_direct_lines_write(void)
{
    /* Test that Direct Lines are not written to */
    exti_writel(EXTI_RTSR2, 0xFFFFFFFF);
    const uint32_t rtsr2 = exti_readl(EXTI_RTSR2);
    g_assert_cmpuint(rtsr2, ==, 0x00000078);
}

static void test_software_interrupt(void)
{
    /* Test that we can launch irq using software in*/

    g_assert_false(get_irq(GPIO_0_IRQ));
    /* Bit 0 corresponds to GPIO Px_0 */
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_SWIER1, 0x00000001);
    uint32_t swier1 = exti_readl(EXTI_SWIER1);
    uint32_t pr1 = exti_readl(EXTI_PR1);

    g_assert_cmpuint(swier1, ==, 0x00000001);
    g_assert_cmpuint(pr1, ==, 0x00000001);

    g_assert_true(get_irq(GPIO_0_IRQ));

    /* Reset the irq and check the registers state */
    exti_writel(EXTI_PR1, 0x00000001);
    swier1 = exti_readl(EXTI_SWIER1);
    pr1 = exti_readl(EXTI_PR1);
    g_assert_cmpuint(swier1, ==, 0x00000000);
    g_assert_cmpuint(pr1, ==, 0x00000000);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("stm32l4x5/exti/write_read", test_write_read);
    qtest_add_func("stm32l4x5/exti/direct_lines_write", test_direct_lines_write);
    /* Fails for now, not implemented */
    qtest_add_func("stm32l4x5/exti/software_interrupt", test_software_interrupt);

    qtest_start("-machine b-l475e-iot01a");
    ret = g_test_run();
    qtest_end();

    return ret;
}
