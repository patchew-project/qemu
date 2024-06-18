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

#include "libqos/csr.h"

static void run_test_csr(void)
{

    uint64_t res;
    uint64_t val = 0;

    res = qcsr_get_csr(global_qtest, 0, 0xf11, &val);

    g_assert_cmpint(res, ==, 0);
    g_assert_cmpint(val, ==, 0x100);

    qtest_quit(global_qtest);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/cpu/csr", run_test_csr);

    qtest_start("-machine virt -cpu any,mvendorid=0x100");

    return g_test_run();

}
