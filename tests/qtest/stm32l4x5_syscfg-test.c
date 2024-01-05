/*
 * QTest testcase for STM32L4x5_SYSCFG
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define SYSCFG_BASE_ADDR 0x40010000
#define SYSCFG_MEMRMP 0x00
#define SYSCFG_CFGR1 0x04
#define SYSCFG_EXTICR1 0x08
#define SYSCFG_EXTICR2 0x0C
#define SYSCFG_EXTICR3 0x10
#define SYSCFG_EXTICR4 0x14
#define SYSCFG_SCSR 0x18
#define SYSCFG_CFGR2 0x1C
#define SYSCFG_SWPR 0x20
#define SYSCFG_SKR 0x24
#define SYSCFG_SWPR2 0x28
#define INVALID_ADDR 0x2C

#define EXTI_BASE_ADDR 0x40010400
#define EXTI_IMR1 0x00
#define EXTI_RTSR1 0x08
#define EXTI_FTSR1 0x0C

static void syscfg_writel(unsigned int offset, uint32_t value)
{
    writel(SYSCFG_BASE_ADDR + offset, value);
}

static uint32_t syscfg_readl(unsigned int offset)
{
    return readl(SYSCFG_BASE_ADDR + offset);
}

static void exti_writel(unsigned int offset, uint32_t value)
{
    writel(EXTI_BASE_ADDR + offset, value);
}

static void system_reset(void)
{
    QDict *response;
    response = qtest_qmp(global_qtest, "{'execute': 'system_reset'}");
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

static void test_reset(void)
{
    /*
     * Test that registers are initialized at the correct values
     */
    const uint32_t memrmp = syscfg_readl(SYSCFG_MEMRMP);
    g_assert_cmpuint(memrmp, ==, 0x00000000);

    const uint32_t cfgr1 = syscfg_readl(SYSCFG_CFGR1);
    g_assert_cmpuint(cfgr1, ==, 0x7C000001);

    const uint32_t exticr1 = syscfg_readl(SYSCFG_EXTICR1);
    g_assert_cmpuint(exticr1, ==, 0x00000000);

    const uint32_t exticr2 = syscfg_readl(SYSCFG_EXTICR2);
    g_assert_cmpuint(exticr2, ==, 0x00000000);

    const uint32_t exticr3 = syscfg_readl(SYSCFG_EXTICR3);
    g_assert_cmpuint(exticr3, ==, 0x00000000);

    const uint32_t exticr4 = syscfg_readl(SYSCFG_EXTICR4);
    g_assert_cmpuint(exticr4, ==, 0x00000000);

    const uint32_t scsr = syscfg_readl(SYSCFG_SCSR);
    g_assert_cmpuint(scsr, ==, 0x00000000);

    const uint32_t cfgr2 = syscfg_readl(SYSCFG_CFGR2);
    g_assert_cmpuint(cfgr2, ==, 0x00000000);

    const uint32_t swpr = syscfg_readl(SYSCFG_SWPR);
    g_assert_cmpuint(swpr, ==, 0x00000000);

    const uint32_t skr = syscfg_readl(SYSCFG_SKR);
    g_assert_cmpuint(skr, ==, 0x00000000);

    const uint32_t swpr2 = syscfg_readl(SYSCFG_SWPR2);
    g_assert_cmpuint(swpr2, ==, 0x00000000);
}

static void test_reserved_bits(void)
{
    /*
     * Test that reserved bits stay at reset value
     * (which is 0 for all of them) by writing '1'
     * in all reserved bits (keeping reset value for
     * other bits) and checking that the
     * register is still at reset value
     */
    syscfg_writel(SYSCFG_MEMRMP, 0xFFFFFEF8);
    const uint32_t memrmp = syscfg_readl(SYSCFG_MEMRMP);
    g_assert_cmpuint(memrmp, ==, 0x00000000);

    syscfg_writel(SYSCFG_CFGR1, 0x7F00FEFF);
    const uint32_t cfgr1 = syscfg_readl(SYSCFG_CFGR1);
    g_assert_cmpuint(cfgr1, ==, 0x7C000001);

    syscfg_writel(SYSCFG_EXTICR1, 0xFFFF0000);
    const uint32_t exticr1 = syscfg_readl(SYSCFG_EXTICR1);
    g_assert_cmpuint(exticr1, ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR2, 0xFFFF0000);
    const uint32_t exticr2 = syscfg_readl(SYSCFG_EXTICR2);
    g_assert_cmpuint(exticr2, ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR3, 0xFFFF0000);
    const uint32_t exticr3 = syscfg_readl(SYSCFG_EXTICR3);
    g_assert_cmpuint(exticr3, ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR4, 0xFFFF0000);
    const uint32_t exticr4 = syscfg_readl(SYSCFG_EXTICR4);
    g_assert_cmpuint(exticr4, ==, 0x00000000);

    syscfg_writel(SYSCFG_SKR, 0xFFFFFF00);
    const uint32_t skr = syscfg_readl(SYSCFG_SKR);
    g_assert_cmpuint(skr, ==, 0x00000000);
}

static void test_set_and_clear(void)
{
    /*
     * Test that regular bits can be set and cleared
     */
    syscfg_writel(SYSCFG_MEMRMP, 0x00000107);
    uint32_t memrmp = syscfg_readl(SYSCFG_MEMRMP);
    g_assert_cmpuint(memrmp, ==, 0x00000107);
    syscfg_writel(SYSCFG_MEMRMP, 0x00000000);
    memrmp = syscfg_readl(SYSCFG_MEMRMP);
    g_assert_cmpuint(memrmp, ==, 0x00000000);

    /* cfgr1 bit 0 is clear only so we keep it set */
    syscfg_writel(SYSCFG_CFGR1, 0xFCFF0101);
    uint32_t cfgr1 = syscfg_readl(SYSCFG_CFGR1);
    g_assert_cmpuint(cfgr1, ==, 0xFCFF0101);
    syscfg_writel(SYSCFG_CFGR1, 0x00000001);
    cfgr1 = syscfg_readl(SYSCFG_CFGR1);
    g_assert_cmpuint(cfgr1, ==, 0x00000001);

    syscfg_writel(SYSCFG_EXTICR1, 0x0000FFFF);
    uint32_t exticr1 = syscfg_readl(SYSCFG_EXTICR1);
    g_assert_cmpuint(exticr1, ==, 0x0000FFFF);
    syscfg_writel(SYSCFG_EXTICR1, 0x00000000);
    exticr1 = syscfg_readl(SYSCFG_EXTICR1);
    g_assert_cmpuint(exticr1, ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR2, 0x0000FFFF);
    uint32_t exticr2 = syscfg_readl(SYSCFG_EXTICR2);
    g_assert_cmpuint(exticr2, ==, 0x0000FFFF);
    syscfg_writel(SYSCFG_EXTICR2, 0x00000000);
    exticr2 = syscfg_readl(SYSCFG_EXTICR2);
    g_assert_cmpuint(exticr2, ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR3, 0x0000FFFF);
    uint32_t exticr3 = syscfg_readl(SYSCFG_EXTICR3);
    g_assert_cmpuint(exticr3, ==, 0x0000FFFF);
    syscfg_writel(SYSCFG_EXTICR3, 0x00000000);
    exticr3 = syscfg_readl(SYSCFG_EXTICR3);
    g_assert_cmpuint(exticr3, ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR4, 0x0000FFFF);
    uint32_t exticr4 = syscfg_readl(SYSCFG_EXTICR4);
    g_assert_cmpuint(exticr4, ==, 0x0000FFFF);
    syscfg_writel(SYSCFG_EXTICR4, 0x00000000);
    exticr4 = syscfg_readl(SYSCFG_EXTICR4);
    g_assert_cmpuint(exticr4, ==, 0x00000000);

    syscfg_writel(SYSCFG_SKR, 0x000000FF);
    uint32_t skr = syscfg_readl(SYSCFG_SKR);
    g_assert_cmpuint(skr, ==, 0x000000FF);
    syscfg_writel(SYSCFG_SKR, 0x00000000);
    skr = syscfg_readl(SYSCFG_SKR);
    g_assert_cmpuint(skr, ==, 0x00000000);
}

static void test_clear_by_writing_1(void)
{
    /*
     * Test that writing '1' doesn't set the bit
     */
    syscfg_writel(SYSCFG_CFGR2, 0x00000100);
    const uint32_t cfgr2 = syscfg_readl(SYSCFG_CFGR2);
    g_assert_cmpuint(cfgr2, ==, 0x00000000);
}

static void test_set_only_bits(void)
{
    /*
     * Test that set only bits stay can't be cleared
     */
    syscfg_writel(SYSCFG_CFGR2, 0x0000000F);
    syscfg_writel(SYSCFG_CFGR2, 0x00000000);
    const uint32_t exticr3 = syscfg_readl(SYSCFG_CFGR2);
    g_assert_cmpuint(exticr3, ==, 0x0000000F);

    syscfg_writel(SYSCFG_SWPR, 0xFFFFFFFF);
    syscfg_writel(SYSCFG_SWPR, 0x00000000);
    const uint32_t swpr = syscfg_readl(SYSCFG_SWPR);
    g_assert_cmpuint(swpr, ==, 0xFFFFFFFF);

    syscfg_writel(SYSCFG_SWPR2, 0xFFFFFFFF);
    syscfg_writel(SYSCFG_SWPR2, 0x00000000);
    const uint32_t swpr2 = syscfg_readl(SYSCFG_SWPR2);
    g_assert_cmpuint(swpr2, ==, 0xFFFFFFFF);

    system_reset();
}

static void test_clear_only_bits(void)
{
    /*
     * Test that clear only bits stay can't be set
     */
    syscfg_writel(SYSCFG_CFGR1, 0x00000000);
    syscfg_writel(SYSCFG_CFGR1, 0x00000001);
    const uint32_t cfgr1 = syscfg_readl(SYSCFG_CFGR1);
    g_assert_cmpuint(cfgr1, ==, 0x00000000);

    system_reset();
}

static void test_interrupt(void)
{
    /*
     * Test that GPIO rising lines result in an irq
     * with the right configuration
     */
    qtest_irq_intercept_in(global_qtest, "/machine/unattached/device[0]/exti");
    /* Enable interrupt on rising edge of GPIO PA[0] */
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000001);

    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 0, 1);

    g_assert_true(get_irq(0));

    /* Enable interrupt on rising edge of GPIO PA[15] */
    exti_writel(EXTI_IMR1, 0x00008000);
    exti_writel(EXTI_RTSR1, 0x00008000);

    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 15, 1);

    g_assert_true(get_irq(15));

    /* Enable interrupt on rising edge of GPIO PB[1] */
    syscfg_writel(SYSCFG_EXTICR1, 0x00000010);
    exti_writel(EXTI_IMR1, 0x00000002);
    exti_writel(EXTI_RTSR1, 0x00000002);

    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 17, 1);

    g_assert_true(get_irq(1));

    /* Clean the test */
    syscfg_writel(SYSCFG_EXTICR1, 0x00000000);
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 0, 0);
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 15, 0);
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 17, 0);
}

static void test_irq_pin_multiplexer(void)
{
    /*
     * Test that syscfg irq sets the right exti irq
     */

    qtest_irq_intercept_in(global_qtest, "/machine/unattached/device[0]/exti");

    /* Enable interrupt on rising edge of GPIO PA[0] */
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000001);

    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 0, 1);

    /* Check that irq 0 was set and irq 15 wasn't */
    g_assert_true(get_irq(0));
    g_assert_false(get_irq(15));

    /* Clean the test */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 0, 0);

    /* Enable interrupt on rising edge of GPIO PA[15] */
    exti_writel(EXTI_IMR1, 0x00008000);
    exti_writel(EXTI_RTSR1, 0x00008000);

    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 15, 1);

    /* Check that irq 15 was set and irq 0 wasn't */
    g_assert_true(get_irq(15));
    g_assert_false(get_irq(0));

    /* Clean the test */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 15, 0);
}

static void test_irq_gpio_multiplexer(void)
{
    /*
     * Test that an irq is generated only by the right GPIO
     */

    qtest_irq_intercept_in(global_qtest, "/machine/unattached/device[0]/exti");

    /* Enable interrupt on rising edge of GPIO PA[0] */
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000001);

    /* Check that setting rising pin GPIOA[0] generates an irq */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 0, 1);

    g_assert_true(get_irq(0));

    /* Clean the test */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 0, 0);

    /* Check that setting rising pin GPIOB[0] doesn't generate an irq */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 16, 1);

    g_assert_false(get_irq(0));

    /* Clean the test */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 16, 0);

    /* Enable interrupt on rising edge of GPIO PB[0] */
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000001);
    syscfg_writel(SYSCFG_EXTICR1, 0x00000001);

    /* Check that setting rising pin GPIOA[0] doesn't generate an irq */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 0, 1);

    g_assert_false(get_irq(0));

    /* Clean the test */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 0, 0);

    /* Check that setting rising pin GPIOB[0] generates an irq */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 16, 1);

    g_assert_true(get_irq(0));

    /* Clean the test */
    qtest_set_irq_in(global_qtest, "/machine/unattached/device[0]/syscfg",
                     NULL, 16, 0);
    syscfg_writel(SYSCFG_EXTICR1, 0x00000000);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("stm32l4x5/syscfg/test_reset", test_reset);
    qtest_add_func("stm32l4x5/syscfg/test_reserved_bits",
                   test_reserved_bits);
    qtest_add_func("stm32l4x5/syscfg/test_set_and_clear",
                   test_set_and_clear);
    qtest_add_func("stm32l4x5/syscfg/test_clear_by_writing_1",
                   test_clear_by_writing_1);
    qtest_add_func("stm32l4x5/syscfg/test_set_only_bits",
                   test_set_only_bits);
    qtest_add_func("stm32l4x5/syscfg/test_clear_only_bits",
                   test_clear_only_bits);
    qtest_add_func("stm32l4x5/syscfg/test_interrupt",
                   test_interrupt);
    qtest_add_func("stm32l4x5/syscfg/test_irq_pin_multiplexer",
                   test_irq_pin_multiplexer);
    qtest_add_func("stm32l4x5/syscfg/test_irq_gpio_multiplexer",
                   test_irq_gpio_multiplexer);

    qtest_start("-machine b-l475e-iot01a");
    ret = g_test_run();
    qtest_end();

    return ret;
}
