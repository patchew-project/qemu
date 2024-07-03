/*
 * QTest testcase for RISC-V CSRs
 *
 * Copyright (c) 2024 Syntacore.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "libqtest.h"

static uint64_t qcsr_call(QTestState *qts, const char *name, uint64_t cpu,
                           int csrno, uint64_t *val)
{
    uint64_t res = 0;

    res = qtest_csr_call(qts, name, cpu, csrno, val);

    return res;
}

static int qcsr_get_csr(QTestState *qts, uint64_t cpu,
        int csrno, uint64_t *val)
{
    int res;

    res = qcsr_call(qts, "get_csr", cpu, csrno, val);

    return res;
}

static int qcsr_set_csr(QTestState *qts, uint64_t cpu,
        int csrno, uint64_t *val)
{
    int res;

    res = qcsr_call(qts, "set_csr", cpu, csrno, val);

    return res;
}

static void run_test_csr(void)
{

    uint64_t res;
    uint64_t val = 0;

    res = qcsr_call(global_qtest, "get_csr", 0, 0xf11, &val);

    g_assert_cmpint(res, ==, 0);
    g_assert_cmpint(val, ==, 0x100);

    val = 0xff;
    res = qcsr_call(global_qtest, "set_csr", 0, 0x342, &val);

    g_assert_cmpint(res, ==, 0);

    val = 0;
    res = qcsr_call(global_qtest, "get_csr", 0, 0x342, &val);

    g_assert_cmpint(res, ==, 0);
    g_assert_cmpint(val, ==, 0xff);

    qtest_quit(global_qtest);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/cpu/csr", run_test_csr);

    qtest_start("-machine virt -cpu any,mvendorid=0x100");

    return g_test_run();

}
