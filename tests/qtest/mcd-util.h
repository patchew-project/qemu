/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mcdutil - Utility functions for the MCD API test suite
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TEST_MCD_UTILS_H
#define TEST_MCD_UTILS_H

#include "libqtest.h"

typedef struct {
    QTestState *qts;
    int mcd_fd;
} QTestStateMCD;

#endif /* TEST_MCD_UTILS_H */
