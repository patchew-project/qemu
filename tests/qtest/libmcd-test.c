/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QTest helpers for functions of the MCD API
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libmcd-test.h"
#include "mcd/mcd_api.h"
#include "mcd/libmcd_qapi.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qjson.h"
#include "qapi/qapi-commands-mcd.h"
#include "qapi/qapi-visit-mcd.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/compat-policy.h"

/*
 * We can use the %p format specifier of qtest_qmp() to automatically
 * serialize the arguments into JSON.
 * The serialization works only after the arguments have been converted into
 * a QDict.
 */

 #define MARSHAL_ARGS(type) do {                     \
    v = qobject_output_visitor_new_qmp(&marshal);    \
    ok = visit_start_struct(v, NULL, (void **)&args, \
                            sizeof(type), NULL);     \
    g_assert(ok);                                    \
    ok = visit_type_##type##_members(v, args, NULL); \
    g_assert(ok);                                    \
    ok = visit_check_struct(v, NULL);                \
    g_assert(ok);                                    \
    visit_end_struct(v, (void **)&args);             \
    visit_complete(v, &marshal);                     \
    visit_free(v);                                   \
    arg = qobject_to(QDict, marshal);                \
} while (0)

#define UNMARSHAL_RESULT(type) do {                    \
    ret = qdict_get(resp, "return");                   \
    g_assert(ret);                                     \
    v = qobject_input_visitor_new(ret);                \
    ok = visit_type_##type(v, NULL, &unmarshal, NULL); \
    g_assert(ok);                                      \
    visit_free(v);                                     \
    qobject_unref(resp);                               \
} while (0)

MCDInitializeResult *qtest_mcd_initialize(QTestState *qts,
                                          q_obj_mcd_initialize_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDInitializeResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_initialize_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-initialize',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDInitializeResult);

    return unmarshal;
}

MCDErrorInfo *qtest_mcd_qry_error_info(QTestState *qts,
                                       q_obj_mcd_qry_error_info_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDErrorInfo *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_error_info_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-qry-error-info',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDErrorInfo);

    return unmarshal;
}


void qtest_mcd_exit(QTestState *qts)
{
    QDict *resp = qtest_qmp(qts, "{'execute': 'mcd-exit' }");
    qobject_unref(resp);
}

MCDQryServersResult *qtest_mcd_qry_servers(QTestState *qts,
                                           q_obj_mcd_qry_servers_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryServersResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_servers_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-qry-servers',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryServersResult);

    return unmarshal;
}

MCDOpenServerResult *qtest_mcd_open_server(QTestState *qts,
                                           q_obj_mcd_open_server_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDOpenServerResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_open_server_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-open-server',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDOpenServerResult);

    return unmarshal;
}

MCDCloseServerResult *qtest_mcd_close_server(QTestState *qts,
                                             q_obj_mcd_close_server_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDCloseServerResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_close_server_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-close-server',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDCloseServerResult);

    return unmarshal;
}

MCDQrySystemsResult *qtest_mcd_qry_systems(QTestState *qts,
                                            q_obj_mcd_qry_systems_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQrySystemsResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_systems_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-qry-systems',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQrySystemsResult);

    return unmarshal;
}

MCDQryDevicesResult *qtest_mcd_qry_devices(QTestState *qts,
                                           q_obj_mcd_qry_devices_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryDevicesResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_devices_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-qry-devices',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryDevicesResult);

    return unmarshal;
}

MCDQryCoresResult *qtest_mcd_qry_cores(QTestState *qts,
                                       q_obj_mcd_qry_cores_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryCoresResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_cores_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-qry-cores',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryCoresResult);

    return unmarshal;
}

MCDOpenCoreResult *qtest_mcd_open_core(QTestState *qts,
                                       q_obj_mcd_open_core_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDOpenCoreResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_open_core_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-open-core',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDOpenCoreResult);

    return unmarshal;
}

MCDCloseCoreResult *qtest_mcd_close_core(QTestState *qts,
                                         q_obj_mcd_close_core_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDCloseCoreResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_close_core_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-close-core',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDCloseCoreResult);

    return unmarshal;
}

MCDQryMemSpacesResult *qtest_mcd_qry_mem_spaces(
    QTestState *qts, q_obj_mcd_qry_mem_spaces_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryMemSpacesResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_mem_spaces_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-qry-mem-spaces',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryMemSpacesResult);

    return unmarshal;
}

MCDQryRegGroupsResult *qtest_mcd_qry_reg_groups(
    QTestState *qts, q_obj_mcd_qry_reg_groups_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryRegGroupsResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_reg_groups_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-qry-reg-groups',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryRegGroupsResult);

    return unmarshal;
}

MCDQryRegMapResult *qtest_mcd_qry_reg_map(QTestState *qts,
                                          q_obj_mcd_qry_reg_map_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryRegMapResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_reg_map_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-qry-reg-map',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryRegMapResult);

    return unmarshal;

}

MCDRunResult *qtest_mcd_run(QTestState *qts, q_obj_mcd_run_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDRunResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_run_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-run',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDRunResult);

    return unmarshal;
}

MCDStopResult *qtest_mcd_stop(QTestState *qts, q_obj_mcd_stop_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDStopResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_stop_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-stop',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDStopResult);

    return unmarshal;
}

MCDQryStateResult *qtest_mcd_qry_state(QTestState *qts,
                                       q_obj_mcd_qry_state_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryStateResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_state_arg);

    resp = qtest_qmp(qts, "{'execute': 'mcd-qry-state',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryStateResult);

    return unmarshal;
}
