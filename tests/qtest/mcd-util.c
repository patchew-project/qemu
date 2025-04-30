/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mcdutil - Utility functions for the MCD API test suite
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "mcd-util.h"
#include "qapi/compat-policy.h"
#include "qapi/qobject-input-visitor.h"
#include "qobject/qdict.h"

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

static QDict *qtest_mcd(QTestStateMCD *s, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

static QDict *qtest_mcd(QTestStateMCD *s, const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    qmp_fd_vsend(s->mcd_fd, fmt, ap);
    va_end(ap);

    response = qmp_fd_receive(s->mcd_fd);

    return response;
}

MCDInitializeResult *qtest_mcd_initialize(QTestStateMCD *qts,
                                          q_obj_mcd_initialize_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDInitializeResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_initialize_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-initialize',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDInitializeResult);

    return unmarshal;
}

MCDErrorInfo *qtest_mcd_qry_error_info(QTestStateMCD *qts,
                                       q_obj_mcd_qry_error_info_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDErrorInfo *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_error_info_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-qry-error-info',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDErrorInfo);

    return unmarshal;
}

void qtest_mcd_exit(QTestStateMCD *qts)
{
    QDict *resp = qtest_mcd(qts, "{'execute': 'mcd-exit' }");
    qobject_unref(resp);
}


MCDQryServersResult *qtest_mcd_qry_servers(QTestStateMCD *qts,
                                           q_obj_mcd_qry_servers_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryServersResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_servers_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-qry-servers',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryServersResult);

    return unmarshal;
}

MCDOpenServerResult *qtest_mcd_open_server(QTestStateMCD *qts,
                                           q_obj_mcd_open_server_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDOpenServerResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_open_server_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-open-server',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDOpenServerResult);

    return unmarshal;
}

MCDCloseServerResult *qtest_mcd_close_server(QTestStateMCD *qts,
                                             q_obj_mcd_close_server_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDCloseServerResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_close_server_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-close-server',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDCloseServerResult);

    return unmarshal;
}

MCDQrySystemsResult *qtest_mcd_qry_systems(QTestStateMCD *qts,
                                            q_obj_mcd_qry_systems_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQrySystemsResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_systems_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-qry-systems',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQrySystemsResult);

    return unmarshal;
}

MCDQryDevicesResult *qtest_mcd_qry_devices(QTestStateMCD *qts,
                                           q_obj_mcd_qry_devices_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryDevicesResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_devices_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-qry-devices',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryDevicesResult);

    return unmarshal;
}

MCDQryCoresResult *qtest_mcd_qry_cores(QTestStateMCD *qts,
                                       q_obj_mcd_qry_cores_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryCoresResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_cores_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-qry-cores',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryCoresResult);

    return unmarshal;
}

MCDOpenCoreResult *qtest_mcd_open_core(QTestStateMCD *qts,
                                       q_obj_mcd_open_core_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDOpenCoreResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_open_core_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-open-core',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDOpenCoreResult);

    return unmarshal;
}

MCDCloseCoreResult *qtest_mcd_close_core(QTestStateMCD *qts,
                                         q_obj_mcd_close_core_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDCloseCoreResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_close_core_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-close-core',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDCloseCoreResult);

    return unmarshal;
}

MCDQryMemSpacesResult *qtest_mcd_qry_mem_spaces(
    QTestStateMCD *qts, q_obj_mcd_qry_mem_spaces_arg *args)
{
    Visitor *v;
    QObject *marshal;
    QDict *arg, *resp;
    QObject *ret;
    bool ok;
    MCDQryMemSpacesResult *unmarshal;

    MARSHAL_ARGS(q_obj_mcd_qry_mem_spaces_arg);

    resp = qtest_mcd(qts, "{'execute': 'mcd-qry-mem-spaces',"
                          "'arguments': %p}", arg);

    UNMARSHAL_RESULT(MCDQryMemSpacesResult);

    return unmarshal;
}
