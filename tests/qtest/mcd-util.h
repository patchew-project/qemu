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
#include "mcd/mcd-qapi-visit.h"

typedef struct {
    QTestState *qts;
    int mcd_fd;
} QTestStateMCD;

MCDInitializeResult *qtest_mcd_initialize(QTestStateMCD *qts,
                                          q_obj_mcd_initialize_arg *args);

MCDErrorInfo *qtest_mcd_qry_error_info(QTestStateMCD *qts);

MCDQryServersResult *qtest_mcd_qry_servers(QTestStateMCD *qts,
                                           q_obj_mcd_qry_servers_arg *args);

MCDOpenServerResult *qtest_mcd_open_server(QTestStateMCD *qts,
                                           q_obj_mcd_open_server_arg *args);

MCDCloseServerResult *qtest_mcd_close_server(QTestStateMCD *qts,
                                             q_obj_mcd_close_server_arg *args);

#endif /* TEST_MCD_UTILS_H */
