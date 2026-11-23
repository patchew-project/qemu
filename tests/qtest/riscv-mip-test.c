/*
 * QTest testcase for RISC-V interrupt-pending state under qtest
 *
 * Copyright (c) 2026 Zixin Wang <w1355457260@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define ACLINT_MSWI_BASE     0x2000000
#define ACLINT_MTIMECMP_BASE 0x2004000
#define CSR_MIP              0x344
#define MIP_MSIP             (1ULL << 3)
#define MIP_MTIP             (1ULL << 7)
#define NS_PER_MS            1000000

#define QEMU_ARGS "-M virt,aia=none,aclint=on -cpu rv64 -smp 1 -bios none"

static uint64_t read_mip(QTestState *qts)
{
    uint64_t val = 0;

    qtest_csr_call(qts, "get_csr", 0, CSR_MIP, &val);
    return val;
}

static void test_msip(void)
{
    QTestState *qts = qtest_init(QEMU_ARGS);

    /* Program a far-future MTIMECMP so MTIP stays clear. */
    qtest_writeq(qts, ACLINT_MTIMECMP_BASE, UINT64_MAX);
    g_assert_cmphex(read_mip(qts), ==, 0);

    qtest_writel(qts, ACLINT_MSWI_BASE, 1);
    g_assert_cmphex(read_mip(qts), ==, MIP_MSIP);

    qtest_writel(qts, ACLINT_MSWI_BASE, 0);
    g_assert_cmphex(read_mip(qts), ==, 0);

    qtest_quit(qts);
}

static void test_mtip(void)
{
    QTestState *qts = qtest_init(QEMU_ARGS);

    /*
     * MTIMECMP=1 is in the future at reset (MTIME is 0), so MTIP is
     * raised by the timer callback once the clock steps past it.
     */
    qtest_writeq(qts, ACLINT_MTIMECMP_BASE, 1);
    qtest_clock_step(qts, NS_PER_MS);
    g_assert_cmphex(read_mip(qts), ==, MIP_MTIP);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("virt")) {
        qtest_add_func("/riscv/mip/msip", test_msip);
        qtest_add_func("/riscv/mip/mtip", test_mtip);
    }

    return g_test_run();
}
