/*
 * QTest regression test for OpenCores Ethernet MII register read
 *
 * Copyright (c) 2026 Bin Guo
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/*
 * lx60 (MMU) maps its system_io at 0xf0000000; open_eth registers live at
 * offset 0x0d030000 inside that region.
 */
#define OPEN_ETH_BASE 0xfd030000

/* 32-bit word register indices */
#define OPEN_ETH_MIICOMMAND  (OPEN_ETH_BASE + 0x2c)
#define OPEN_ETH_MIIADDRESS  (OPEN_ETH_BASE + 0x30)
#define OPEN_ETH_MIIRX_DATA  (OPEN_ETH_BASE + 0x38)

#define MIIADDRESS_FIAD      0x00000001
#define MIIADDRESS_RGAD_SHIFT 8
#define MIICOMMAND_RSTAT     0x00000002

/*
 * Regression test for GitLab issue #4364:
 * MIIADDRESS.RGAD is a 5-bit field (0..31), but the local PHY register
 * array only has 16 entries.  A read with RGAD >= 16 must not perform an
 * out-of-bounds access.
 */
static void test_mii_register_out_of_range(void)
{
    QTestState *s;

    s = qtest_init("-machine lx60");

    /* Select default PHY (FIAD == 1) and first out-of-range register. */
    qtest_writel(s, OPEN_ETH_MIIADDRESS,
                 MIIADDRESS_FIAD | (16 << MIIADDRESS_RGAD_SHIFT));

    /* Trigger MII read command. */
    qtest_writel(s, OPEN_ETH_MIICOMMAND, MIICOMMAND_RSTAT);

    /*
     * With the bug, the read path evaluates s->regs[16] and trips ASan/UBSan.
     * With the fix, MIIRX_DATA.PRSD should read as 0xffff (unimplemented PHY
     * register), matching the non-default-PHY fallback in the driver.
     */
    g_assert_cmphex(qtest_readl(s, OPEN_ETH_MIIRX_DATA), ==, 0xffff);

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("lx60")) {
        qtest_add_func("/opencores-eth/mii-register-out-of-range",
                       test_mii_register_out_of_range);
    }

    return g_test_run();
}
