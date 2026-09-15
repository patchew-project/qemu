/*
 * QTest regression test for HMP expression evaluator overflow.
 *
 * Copyright (c) 2026 Bin Guo
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

static void assert_overflow_response(QTestState *qts, const char *cmd)
{
    g_autofree char *resp = qtest_hmp(qts, "%s", cmd);

    g_assert(strstr(resp, "integer overflow") != NULL);
}

static void assert_ok_response(QTestState *qts, const char *cmd)
{
    g_autofree char *resp = qtest_hmp(qts, "%s", cmd);

    g_assert(strstr(resp, "integer overflow") == NULL);
    g_assert(strstr(resp, "error") == NULL);
}

static void test_expr_overflow(void)
{
    QTestState *qts = qtest_init("-M none -m 2");

    /* Addition overflow */
    assert_overflow_response(qts, "print 0x7fffffffffffffff + 1");

    /* Subtraction overflow */
    assert_overflow_response(qts, "print -0x8000000000000000 - 1");

    /* Multiplication overflow */
    assert_overflow_response(qts, "print 0x4000000000000000 * 2");

    /* Unary negation of INT64_MIN */
    assert_overflow_response(qts, "print -0x8000000000000000");

    /* Division overflow */
    assert_overflow_response(qts, "print -0x8000000000000000 / -1");
    assert_overflow_response(qts, "print -0x8000000000000000 % -1");

    /* Sanity: non-overflowing expressions still work */
    assert_ok_response(qts, "print 1 + 1");
    assert_ok_response(qts, "print 0x7fffffffffffffff");
    assert_ok_response(qts, "print -0x7fffffffffffffff");

    qtest_quit(qts);
}

static void test_balloon_m_overflow(void)
{
    QTestState *qts;

    /* q35 is x86-only; skip this test on other architectures. */
    if (!qtest_has_machine("q35")) {
        return;
    }

    /* Balloon command takes an 'M' suffix size argument in MB. */
    qts = qtest_init("-M q35 -m 128 -device virtio-balloon-pci");

    /* 0x7fffffffffffffff is positive as int64_t, but * MiB overflows. */
    assert_overflow_response(qts, "balloon 0x7fffffffffffffff");

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("hmp/expr-overflow", test_expr_overflow);
    qtest_add_func("hmp/balloon-m-overflow", test_balloon_m_overflow);

    return g_test_run();
}
