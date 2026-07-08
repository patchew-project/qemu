/*
 * QTest testcase for the HPET
 *
 * Copyright (C) 2026 Intel Corporation.
 *
 * Authors:
 *  Zhao Liu <zhao1.liu@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/timer.h"
#include "hw/timer/hpet.h"

#define HPET_TN_BASE_ADDR(n)    (HPET_TN_BASE + (n) * 0x20)
#define HPET_TN_CFG_ADDR(n)     (HPET_TN_BASE_ADDR(n) + HPET_TN_CFG)
#define HPET_TN_CMP_ADDR(n)     (HPET_TN_BASE_ADDR(n) + HPET_TN_CMP)
#define HPET_TN_FSB_ADDR(n)     (HPET_TN_BASE_ADDR(n) + HPET_TN_ROUTE)

/*
 * Normally, the MSI should be written to the Local APIC address space.
 * However, since the goal for current MSI testing is to verify
 * memory store and load, use a RAM for simplicity.
 */
#define MSI_TARGET_ADDR         0x100000
#define MSI_DATA                0x19680718U

static void test_hpet_fsb_store(void)
{
    QTestState *s;
    uint32_t got;
    uint64_t tn_cfg;

    s = qtest_init("-machine pc,hpet=on -global hpet.msi=on");

    qtest_writel(s, MSI_TARGET_ADDR, 0);
    g_assert_cmphex(qtest_readl(s, MSI_TARGET_ADDR), ==, 0);

    /* Ensure HPET (timer 0) supports FSB first. */
    tn_cfg = qtest_readq(s, HPET_BASE + HPET_TN_CFG_ADDR(0));
    g_assert(tn_cfg & HPET_TN_FSB_CAP);

    /* Configure FSB for timer 0. */
    qtest_writeq(s, HPET_BASE + HPET_TN_FSB_ADDR(0),
                 ((uint64_t)MSI_TARGET_ADDR << 32) | MSI_DATA);

    /* Enable the timer 0's interrupt and FSB (non-periodic mode). */
    qtest_writeq(s, HPET_BASE + HPET_TN_CFG_ADDR(0),
                 tn_cfg | HPET_TN_ENABLE | HPET_TN_FSB_ENABLE);

    /*
     * Expire time: 0x100 = 256 ticks.
     * Considering HPET_CLK_PERIOD, it's 2560 ns.
     */
    qtest_writeq(s, HPET_BASE + HPET_TN_CMP_ADDR(0), 0x100);

    /* Start main counter to run - arm the timer 0. */
    qtest_writeq(s, HPET_BASE + HPET_CFG, HPET_CFG_ENABLE);

    /* Advance 10000 ns, which is enough for timer 0 to expire. */
    qtest_clock_step(s, 10000);

    /* The 32-bit data must now be in guest RAM now, little-endian. */
    got = qtest_readl(s, MSI_TARGET_ADDR);
    g_assert_cmphex(got, ==, MSI_DATA);

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/hpet/fsb-store", test_hpet_fsb_store);
    return g_test_run();
}
