/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QTest helpers for functions of the MCD API
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBMCD_TEST_H
#define LIBMCD_TEST_H

#include "libqtest.h"
#include "mcd/mcd_api.h"
#include "qapi/qapi-visit-mcd.h"

MCDInitializeResult *qtest_mcd_initialize(QTestState *qts,
                                          q_obj_mcd_initialize_arg *args);

MCDErrorInfo *qtest_mcd_qry_error_info(QTestState *qts);

#endif /* LIBMCD_TEST_H */
