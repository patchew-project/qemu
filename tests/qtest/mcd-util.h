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

MCDErrorInfo *qtest_mcd_qry_error_info(QTestStateMCD *qts,
                                       q_obj_mcd_qry_error_info_arg *args);

void qtest_mcd_exit(QTestStateMCD *qts);

MCDQryServersResult *qtest_mcd_qry_servers(QTestStateMCD *qts,
                                           q_obj_mcd_qry_servers_arg *args);

MCDOpenServerResult *qtest_mcd_open_server(QTestStateMCD *qts,
                                           q_obj_mcd_open_server_arg *args);

MCDCloseServerResult *qtest_mcd_close_server(QTestStateMCD *qts,
                                             q_obj_mcd_close_server_arg *args);

MCDQrySystemsResult *qtest_mcd_qry_systems(QTestStateMCD *qts,
                                           q_obj_mcd_qry_systems_arg *args);

MCDQryDevicesResult *qtest_mcd_qry_devices(QTestStateMCD *qts,
                                           q_obj_mcd_qry_devices_arg *args);

MCDQryCoresResult *qtest_mcd_qry_cores(QTestStateMCD *qts,
                                       q_obj_mcd_qry_cores_arg *args);

MCDOpenCoreResult *qtest_mcd_open_core(QTestStateMCD *qts,
                                       q_obj_mcd_open_core_arg *args);

MCDCloseCoreResult *qtest_mcd_close_core(QTestStateMCD *qts,
                                         q_obj_mcd_close_core_arg *args);

MCDQryMemSpacesResult *qtest_mcd_qry_mem_spaces(QTestStateMCD *qts,
    q_obj_mcd_qry_mem_spaces_arg *args);

#endif /* TEST_MCD_UTILS_H */
