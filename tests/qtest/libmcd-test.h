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

MCDErrorInfo *qtest_mcd_qry_error_info(QTestState *qts,
                                       q_obj_mcd_qry_error_info_arg *args);

void qtest_mcd_exit(QTestState *qts);

MCDQryServersResult *qtest_mcd_qry_servers(QTestState *qts,
                                           q_obj_mcd_qry_servers_arg *args);

MCDOpenServerResult *qtest_mcd_open_server(QTestState *qts,
                                           q_obj_mcd_open_server_arg *args);

MCDCloseServerResult *qtest_mcd_close_server(QTestState *qts,
                                             q_obj_mcd_close_server_arg *args);

MCDQrySystemsResult *qtest_mcd_qry_systems(QTestState *qts,
                                           q_obj_mcd_qry_systems_arg *args);

MCDQryDevicesResult *qtest_mcd_qry_devices(QTestState *qts,
                                           q_obj_mcd_qry_devices_arg *args);

MCDQryCoresResult *qtest_mcd_qry_cores(QTestState *qts,
                                       q_obj_mcd_qry_cores_arg *args);

MCDOpenCoreResult *qtest_mcd_open_core(QTestState *qts,
                                       q_obj_mcd_open_core_arg *args);

MCDCloseCoreResult *qtest_mcd_close_core(QTestState *qts,
                                         q_obj_mcd_close_core_arg *args);

MCDQryMemSpacesResult *qtest_mcd_qry_mem_spaces(QTestState *qts,
    q_obj_mcd_qry_mem_spaces_arg *args);

MCDQryRegGroupsResult *qtest_mcd_qry_reg_groups(QTestState *qts,
    q_obj_mcd_qry_reg_groups_arg *args);

MCDQryRegMapResult *qtest_mcd_qry_reg_map(QTestState *qts,
                                          q_obj_mcd_qry_reg_map_arg *args);

MCDRunResult *qtest_mcd_run(QTestState *qts,
                            q_obj_mcd_run_arg *args);

MCDStopResult *qtest_mcd_stop(QTestState *qts,
                              q_obj_mcd_stop_arg *args);

MCDQryStateResult *qtest_mcd_qry_state(QTestState *qts,
                                       q_obj_mcd_qry_state_arg *args);

#endif /* LIBMCD_TEST_H */
