/*
 * QTest testcase for the ASPEED AST2500 and AST2600 SCU.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2025 Tan Siewert
 */

#ifndef TESTS_ASPEED_SCU_UTILS_H
#define TESTS_ASPEED_SCU_UTILS_H

#include "qemu/osdep.h"
#include "libqtest-single.h"

/**
 * Assert that a given register matches an expected value.
 *
 * Reads the register and checks if its value equals the expected value,
 * without requiring a temporary variable in the caller.
 *
 * @param *s - QTest machine state
 * @param reg - Address of the register to be checked
 * @param expected - Expected register value
 */
void assert_register_eq(QTestState *s, uint32_t reg, uint32_t expected);

/**
 * Assert that a given register does not match a specific value.
 *
 * Reads the register and checks that its value is not equal to the
 * provided value.
 *
 * @param *s - QTest machine state
 * @param reg - Address of the register to be checked
 * @param not_expected - Value the register must not contain
 */
void assert_register_neq(QTestState *s, uint32_t reg, uint32_t not_expected);

#endif /* TESTS_ASPEED_SCU_UTILS_H */
