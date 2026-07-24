/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest testcase for the L2VIC Interrupt Controller
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/hexagon/hexagon.h"

#include "hw/hexagon/machine_cfg_v66g_1024.h.inc"
#include "hw/hexagon/machine_cfg_v68n_1024.h.inc"

/* L2VIC register offsets exercised by this test */
#define L2VIC_INT_ENABLEn 0x100 /* Read/Write */
#define L2VIC_INT_ENABLE_CLEARn 0x180 /* Write */
#define L2VIC_INT_ENABLE_SETn 0x200 /* Write */
#define L2VIC_INT_TYPEn 0x280 /* Read/Write */
#define L2VIC_INT_STATUSn 0x380 /* Read */
#define L2VIC_INT_CLEARn 0x400 /* Write */
#define L2VIC_SOFT_INTn 0x480 /* Write */
#define L2VIC_INT_PENDINGn 0x500 /* Read */
#define L2VIC_INT_GRPn_0 0x600 /* Read/Write */
#define L2VIC_INT_GRPn_1 0x680 /* Read/Write */
#define L2VIC_INT_GRPn_2 0x700 /* Read/Write */
#define L2VIC_INT_GRPn_3 0x780 /* Read/Write */

typedef struct {
    const char *machine;
    const struct hexagon_machine_config *cfg;
} L2VICMachineCfg;

static const L2VICMachineCfg l2vic_machines[] = {
    { "virt",      &v68n_1024 },
    { "V66G_1024", &v66g_1024 },
};

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
static void test_l2vic_register_access(uint64_t base)
{
    uint32_t val;

    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x1);
    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x1, ==, 0x1);

    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0x1);
    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x1, ==, 0x0);
}

/* Test interrupt enable/disable */
static void test_l2vic_interrupt_enable(uint64_t base)
{
    uint32_t val;

    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val, ==, 0);

    /* Enable IRQ 0 and 2 */
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x5);
    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x5, ==, 0x5);

    /* Disable IRQ 0, leaving IRQ 2 enabled */
    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0x1);
    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x1, ==, 0x0);
    g_assert_cmpuint(val & 0x4, ==, 0x4);
}

/* Test register read/write without triggering interrupts */
static void test_l2vic_basic_functionality(uint64_t base)
{
    l2vic_read32(base, L2VIC_INT_ENABLEn);
    l2vic_read32(base, L2VIC_INT_PENDINGn);
    l2vic_read32(base, L2VIC_INT_STATUSn);
    l2vic_read32(base, L2VIC_INT_TYPEn);

    /* Exercise write-only registers */
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0);
    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0);
}

static void test_l2vic_irq_outputs(uint64_t base)
{
    uint32_t val;

    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0xFFFFFFFF);
    l2vic_write32(base, L2VIC_INT_CLEARn, 0xFFFFFFFF);
    l2vic_write32(base, L2VIC_INT_TYPEn, 0);

    /* Group 0 / IRQ2: soft interrupts require edge-triggered config */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x1);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x1);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x1);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x1, ==, 0x1);

    l2vic_write32(base, L2VIC_INT_CLEARn, 0x1);
    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x1, ==, 0x0);

    /* Group 1 / IRQ3 */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x2);
    l2vic_write32(base, L2VIC_INT_GRPn_1, 0x2);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x2);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x2);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x2, ==, 0x2);
    val = l2vic_read32(base, L2VIC_INT_GRPn_1);
    g_assert_cmpuint(val & 0x2, ==, 0x2);

    l2vic_write32(base, L2VIC_INT_CLEARn, 0x2);

    /* Group 2 / IRQ4 */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x4);
    l2vic_write32(base, L2VIC_INT_GRPn_2, 0x4);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x4);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x4);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x4, ==, 0x4);
    val = l2vic_read32(base, L2VIC_INT_GRPn_2);
    g_assert_cmpuint(val & 0x4, ==, 0x4);

    l2vic_write32(base, L2VIC_INT_CLEARn, 0x4);

    /* Group 3 / IRQ5 */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x8);
    l2vic_write32(base, L2VIC_INT_GRPn_3, 0x8);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x8);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x8);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x8, ==, 0x8);
    val = l2vic_read32(base, L2VIC_INT_GRPn_3);
    g_assert_cmpuint(val & 0x8, ==, 0x8);

    l2vic_write32(base, L2VIC_INT_CLEARn, 0x8);

    /* Multiple pending: at most one active at a time */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0xF);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0xF);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0xF);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0xF, !=, 0x0);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    l2vic_write32(base, L2VIC_INT_CLEARn, val & 0xF);
    val = l2vic_read32(base, L2VIC_INT_STATUSn);

    /* Level-triggered sources ignore soft interrupts */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x0);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x20);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x20);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x20, ==, 0x0);

    /* Same source, now edge-triggered, does fire */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x20);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x20);
    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x20, ==, 0x20);

    /* Clean up */
    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0xFFFFFFFF);
    l2vic_write32(base, L2VIC_INT_CLEARn, 0xFFFFFFFF);
    l2vic_write32(base, L2VIC_INT_GRPn_0, 0);
    l2vic_write32(base, L2VIC_INT_GRPn_1, 0);
    l2vic_write32(base, L2VIC_INT_GRPn_2, 0);
    l2vic_write32(base, L2VIC_INT_GRPn_3, 0);
}

static void test_l2vic_on_machine(gconstpointer data)
{
    const L2VICMachineCfg *mc = data;
    g_autofree char *args = g_strdup_printf("-machine %s", mc->machine);
    uint64_t base = mc->cfg->l2vic_base;

    qtest_start(args);

    /* Run all the L2VIC tests */
    test_l2vic_register_access(base);
    test_l2vic_interrupt_enable(base);
    test_l2vic_basic_functionality(base);
    test_l2vic_irq_outputs(base);

    qtest_end();
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(l2vic_machines); i++) {
        g_autofree char *path = g_strdup_printf("/l2vic/%s/all-tests",
                                                l2vic_machines[i].machine);
        qtest_add_data_func(path, &l2vic_machines[i], test_l2vic_on_machine);
    }

    return g_test_run();
}
