/*
 * ARM GICv2 Virtual Interface Control and Virtual CPU Interface Tests
 *
 * Copyright (c) 2026 Linaro Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* Addresses in the virt machine when virtualization=on */
#define GICH_BASE 0x08030000
#define GICV_BASE 0x08040000

/* Hypervisor Control Registers */
#define GICH_HCR        0x0000
#define GICH_LR0        0x0100

/* Virtual CPU Interface Registers */
#define GICV_CTLR       0x0000
#define GICV_PMR        0x0004
#define GICV_IAR        0x000c
#define GICV_EOIR       0x0010
#define GICV_HPPIR      0x0018

/*
 * Test Highest Priority Pending Interrupt Register (GICV_HPPIR)
 * and Interrupt Acknowledge Register (GICV_IAR) for a virtual SGI.
 *
 * In GICv2, for an SGI, both HPPIR and IAR report:
 *   Bits [9:0]:   INTID
 *   Bits [12:10]: CPUID of the requesting processor
 * (See Issue 4402: GICV_HPPIR dropped the CPUID field and returned only INTID).
 */
static void test_virtual_interface_sgi(void)
{
    QTestState *qts = qtest_init("-machine virt,virtualization=on,gic-version=2");

    /* Enable virtual CPU interface and allow all interrupt priorities */
    qtest_writel(qts, GICV_BASE + GICV_CTLR, 0x1);
    qtest_writel(qts, GICV_BASE + GICV_PMR, 0xff);

    /* Enable virtual interface in hypervisor control */
    qtest_writel(qts, GICH_BASE + GICH_HCR, 0x1);

    /*
     * Configure GICH_LR0 with a pending SGI:
     *   VirtualID = 5 (bits [9:0])
     *   CPUID = 1     (bits [12:10]) -> 1 << 10 = 0x400
     *   Priority = 4  (bits [27:23]) -> 4 << 23 = 0x02000000
     *   State = 1     (bits [29:28]) -> PENDING = 0x10000000
     * Value: 0x12000405
     */
    qtest_writel(qts, GICH_BASE + GICH_LR0, 0x12000405);

    /* Read highest pending interrupt and acknowledge register */
    uint32_t hppir = qtest_readl(qts, GICV_BASE + GICV_HPPIR);
    uint32_t iar = qtest_readl(qts, GICV_BASE + GICV_IAR);

    g_test_message("GICV_HPPIR = 0x%08x, GICV_IAR = 0x%08x", hppir, iar);

    /* GICV_IAR must report CPUID 1 and INTID 5 (0x405) */
    g_assert_cmphex(iar, ==, 0x405);

    /*
     * GICV_HPPIR must also report CPUID 1 and INTID 5 (0x405).
     * In buggy QEMU, GICV_HPPIR erroneously returns 0x5.
     */
    g_assert_cmphex(hppir, ==, 0x405);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("virt")) {
        qtest_add_func("/arm/gicv2/virtual_interface_sgi", test_virtual_interface_sgi);
    }

    return g_test_run();
}
