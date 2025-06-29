/*
 * QTest testcase for the ASPEED AST2500 and AST2600 SCU.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2025 Tan Siewert
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "aspeed-scu-utils.h"

void assert_register_eq(QTestState *s, uint32_t reg, uint32_t expected)
{
    uint32_t value = qtest_readl(s, reg);
    g_assert_cmphex(value, ==, expected);
}

void assert_register_neq(QTestState *s, uint32_t reg, uint32_t not_expected)
{
    uint32_t value = qtest_readl(s, reg);
    g_assert_cmphex(value, !=, not_expected);
}
