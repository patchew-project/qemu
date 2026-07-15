/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest testcase for the L2VIC Interrupt Controller
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/intc/hex-l2vic.h"

/* Base addresses for L2VIC */
#define L2VIC_BASE 0xfc910000
#define L2VIC_FAST_BASE 0xfc920000

/* Helper functions */
static uint32_t l2vic_read32(uint64_t base, uint32_t offset)
{
    return readl(base + offset);
}

static void l2vic_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    writel(base + offset, value);
}

/* Test basic register access */
static void test_l2vic_register_access(void)
{
    uint32_t val;

    /* Test that basic registers can be read/written */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0x1);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    /* Should have enabled interrupt 0 */
    g_assert_cmpuint(val & 0x1, ==, 0x1);

    /* Clear it */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_CLEARn, 0x1);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    /* Should be cleared now */
    g_assert_cmpuint(val & 0x1, ==, 0x0);
}

/* Test interrupt enable/disable */
static void test_l2vic_interrupt_enable(void)
{
    uint32_t val;

    /* Initially all interrupts should be disabled */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val, ==, 0);

    /* Enable some interrupts */
    /* Enable IRQ 0 and 2 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0x5);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x5, ==, 0x5);

    /* Disable one */
    /* Disable IRQ 0 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_CLEARn, 0x1);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x1, ==, 0x0);  /* IRQ 0 should be disabled */
    g_assert_cmpuint(val & 0x4, ==, 0x4);  /* IRQ 2 should still be enabled */
}

/* Test register read/write without triggering interrupts */
static void test_l2vic_basic_functionality(void)
{
    l2vic_read32(L2VIC_BASE, L2VIC_INT_ENABLEn);
    l2vic_read32(L2VIC_BASE, L2VIC_INT_PENDINGn);
    l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    l2vic_read32(L2VIC_BASE, L2VIC_INT_TYPEn);

    /* Exercise write-only registers */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_CLEARn, 0);
}

/*
 * Test L2VIC IRQ outputs
 */
static void test_l2vic_irq_outputs(void)
{
    uint32_t val;

    /* Clear any previous configuration */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_CLEARn, 0xFFFFFFFF);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_CLEARn, 0xFFFFFFFF);
    /* Reset all to level-triggered */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_TYPEn, 0);

    /* Test Group 0 interrupts (default group: IRQ2) */
    /* Configure interrupt 0 as edge-triggered (required for soft interrupts) */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_TYPEn, 0x1);
    /* Enable interrupt 0 and generate software interrupt */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0x1);
    l2vic_write32(L2VIC_BASE, L2VIC_SOFT_INTn, 0x1);

    /* Verify interrupt is active (would assert IRQ2 in real hardware) */
    /* In L2VIC: pending interrupts become active and clear from pending */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x1, ==, 0x1);

    /* Clear the interrupt using INT_CLEARn register */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_CLEARn, 0x1);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x1, ==, 0x0);

    /* Test Group 1 interrupts: IRQ3 */
    /* Configure interrupt 1 as edge-triggered and to group 1 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_TYPEn, 0x2);
    /* Set bit 1 for interrupt 1 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_GRPn_1, 0x2);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0x2);
    l2vic_write32(L2VIC_BASE, L2VIC_SOFT_INTn, 0x2);

    /* Verify interrupt is active in group 1 (would assert IRQ3) */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x2, ==, 0x2);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_GRPn_1);
    g_assert_cmpuint(val & 0x2, ==, 0x2);

    /* Clear the interrupt using INT_CLEARn register */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_CLEARn, 0x2);

    /* Test Group 2 interrupts: IRQ4 */
    /* Configure interrupt 2 as edge-triggered and to group 2 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_TYPEn, 0x4);
    /* Set bit 2 for interrupt 2 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_GRPn_2, 0x4);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0x4);
    l2vic_write32(L2VIC_BASE, L2VIC_SOFT_INTn, 0x4);

    /* Verify interrupt is active in group 2 (would assert IRQ4) */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x4, ==, 0x4);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_GRPn_2);
    g_assert_cmpuint(val & 0x4, ==, 0x4);

    /* Clear the interrupt using INT_CLEARn register */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_CLEARn, 0x4);

    /* Test Group 3 interrupts: IRQ5 */
    /* Configure interrupt 3 as edge-triggered and to group 3 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_TYPEn, 0x8);
    /* Set bit 3 for interrupt 3 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_GRPn_3, 0x8);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0x8);
    l2vic_write32(L2VIC_BASE, L2VIC_SOFT_INTn, 0x8);

    /* Verify interrupt is active in group 3 (would assert IRQ5) */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x8, ==, 0x8);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_GRPn_3);
    g_assert_cmpuint(val & 0x8, ==, 0x8);

    /* Clear the interrupt using INT_CLEARn register */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_CLEARn, 0x8);

    /* Test multiple pending interrupts (only one can be active at a time) */
    /* Configure all interrupts 0-3 as edge-triggered */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_TYPEn, 0xF);
    /* Enable interrupts 0-3 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0xF);
    /* Generate all 4 interrupts */
    l2vic_write32(L2VIC_BASE, L2VIC_SOFT_INTn, 0xF);

    /* Verify that highest prio interrupt becomes active */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    /* At least one bit should be set, but not necessarily all */
    g_assert_cmpuint(val & 0xF, !=, 0x0);

    /* Clear all active interrupts */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_CLEARn, val & 0xF);

    /* After clearing, check if more interrupts become active */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    /* Could be 0 if all processed, or have more if pending */

    /* Test that software interrupts require edge-triggered configuration */
    /* Configure interrupt 5 as level-triggered (default) */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_TYPEn, 0x0);
    /* Enable interrupt 5 */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_SETn, 0x20);
    /* Try to generate level interrupt */
    l2vic_write32(L2VIC_BASE, L2VIC_SOFT_INTn, 0x20);

    /* Should not work - no interrupt should be active */
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    /* No interrupt because level-triggered */
    g_assert_cmpuint(val & 0x20, ==, 0x0);

    /* Now configure as edge-triggered and try again */
    /* Set edge-triggered */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_TYPEn, 0x20);
    /* Generate interrupt */
    l2vic_write32(L2VIC_BASE, L2VIC_SOFT_INTn, 0x20);
    val = l2vic_read32(L2VIC_BASE, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x20, ==, 0x20);

    /* Clean up - clear all configuration */
    l2vic_write32(L2VIC_BASE, L2VIC_INT_ENABLE_CLEARn, 0xFFFFFFFF);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_CLEARn, 0xFFFFFFFF);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_GRPn_0, 0);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_GRPn_1, 0);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_GRPn_2, 0);
    l2vic_write32(L2VIC_BASE, L2VIC_INT_GRPn_3, 0);
}

static void test_l2vic_on_machine(gconstpointer data)
{
    const char *machine = (const char *)data;
    g_autofree char *args = g_strdup_printf("-machine %s", machine);

    qtest_start(args);

    /* Run all the L2VIC tests */
    test_l2vic_register_access();
    test_l2vic_interrupt_enable();
    test_l2vic_basic_functionality();
    test_l2vic_irq_outputs();

    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    /* Test on virt machine */
    qtest_add_data_func("/l2vic/virt/all-tests", "virt",
                        test_l2vic_on_machine);

    /* Test on V66G_1024 machine */
    qtest_add_data_func("/l2vic/V66G_1024/all-tests", "V66G_1024",
                        test_l2vic_on_machine);

    return g_test_run();
}
