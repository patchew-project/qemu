/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mcdtest - Test suite for the Multi-Core Debug (MCD) API implementation
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#endif /* _WIN32 */
#ifdef __linux__
#include <sys/prctl.h>
#endif /* __linux__ */
#ifdef __FreeBSD__
#include <sys/procctl.h>
#endif /* __FreeBSD__ */

#include "mcd/mcd_qapi.h"
#include "libqtest.h"
#include "mcd-util.h"

#define QEMU_EXTRA_ARGS ""

static bool verbose;

static QTestStateMCD mcdtest_init(const char *extra_args)
{
    QTestStateMCD qts_mcd;
    int sock;

    g_autofree gchar *sock_path = g_strdup_printf("%s/qtest-%d.mcd",
                                    g_get_tmp_dir(), getpid());

    /* remove possible orphan from earlier test run */
    unlink(sock_path);
    sock = qtest_socket_server(sock_path);

    g_autoptr(GString) args = g_string_new(extra_args);
    g_string_append_printf(args, " -chardev socket,path=%s,id=mcdsock "
                                 "-mcd chardev:mcdsock",
                                 sock_path);

    qts_mcd.qts = qtest_init_without_qmp_handshake(args->str);
    g_assert(qts_mcd.qts);

    qts_mcd.mcd_fd = accept(sock, NULL, NULL);
    unlink(sock_path);
    g_assert(qts_mcd.mcd_fd >= 0);

    return qts_mcd;
}

static void mcdtest_quit(QTestStateMCD *qts)
{
    qtest_quit(qts->qts);
    close(qts->mcd_fd);

    qts->qts = NULL;
    qts->mcd_fd = -1;
}

static void test_initialize_mcdtest(void)
{
    QTestStateMCD qts = mcdtest_init(QEMU_EXTRA_ARGS);
    mcdtest_quit(&qts);
}

static MCDQryCoresResult *open_server_query_cores(QTestStateMCD *qts)
{
    char empty_string[] = "";

    q_obj_mcd_open_server_arg open_server_args = {
        .system_key = empty_string,
        .config_string = empty_string,
    };

    q_obj_mcd_qry_systems_arg qry_systems_args = {
        .start_index = 0,
        .num_systems = 1,
    };

    q_obj_mcd_qry_devices_arg qry_devices_args = {
        .start_index = 0,
        .num_devices = 1,
    };

    /* first for num_cores only */
    q_obj_mcd_qry_cores_arg qry_cores_args = {
        .start_index = 0,
        .num_cores = 0,
    };

    MCDOpenServerResult *open_server_result;
    MCDQrySystemsResult *qry_systems_result;
    MCDQryDevicesResult *qry_devices_result;
    MCDQryCoresResult *qry_cores_result;

    open_server_result = qtest_mcd_open_server(qts, &open_server_args);
    g_assert(open_server_result->return_status == MCD_RET_ACT_NONE);
    g_assert(open_server_result->has_server_uid);
    qapi_free_MCDOpenServerResult(open_server_result);

    qry_systems_result = qtest_mcd_qry_systems(qts, &qry_systems_args);
    g_assert(qry_systems_result->return_status == MCD_RET_ACT_NONE);
    g_assert(qry_systems_result->has_system_con_info);

    qry_devices_args.system_con_info =
        qry_systems_result->system_con_info->value;

    qry_devices_result = qtest_mcd_qry_devices(qts, &qry_devices_args);
    g_assert(qry_devices_result->return_status == MCD_RET_ACT_NONE);
    g_assert(qry_devices_result->has_device_con_info);
    qapi_free_MCDQrySystemsResult(qry_systems_result);

    qry_cores_args.connection_info =
        qry_devices_result->device_con_info->value;

    qry_cores_result = qtest_mcd_qry_cores(qts, &qry_cores_args);
    g_assert(qry_cores_result->return_status == MCD_RET_ACT_NONE);
    g_assert(qry_cores_result->has_num_cores);
    g_assert(qry_cores_result->num_cores > 0);
    qry_cores_args.num_cores = qry_cores_result->num_cores;
    qapi_free_MCDQryCoresResult(qry_cores_result);
    qry_cores_result = qtest_mcd_qry_cores(qts, &qry_cores_args);
    g_assert(qry_cores_result->return_status == MCD_RET_ACT_NONE);
    g_assert(qry_cores_result->has_num_cores);
    g_assert(qry_cores_result->num_cores > 0);
    qapi_free_MCDQryDevicesResult(qry_devices_result);

    return qry_cores_result;
}

static void test_initialize(void)
{
    QTestStateMCD qts = mcdtest_init(QEMU_EXTRA_ARGS);
    MCDErrorInfo *error_info;

    mcd_api_version_st version_req = {
        .v_api_major = MCD_API_VER_MAJOR,
        .v_api_minor = MCD_API_VER_MINOR,
        .author = "",
    };

    q_obj_mcd_initialize_arg qapi_args = {
        .version_req = marshal_mcd_api_version(&version_req),
    };

    q_obj_mcd_qry_error_info_arg qry_error_info_args = { .core_uid = 0 };

    MCDInitializeResult *result = qtest_mcd_initialize(&qts, &qapi_args);
    g_assert(result->return_status == MCD_RET_ACT_NONE);

    if (verbose) {
        fprintf(stderr, "[INFO]\tAPI v%d.%d (%s)\n",
                        result->impl_info->v_api->v_api_major,
                        result->impl_info->v_api->v_api_minor,
                        result->impl_info->v_api->author);
        fprintf(stderr, "[INFO]\tImplementation v%d.%d.%d %s (%s)\n",
                        result->impl_info->v_imp_major,
                        result->impl_info->v_imp_minor,
                        result->impl_info->v_imp_build,
                        result->impl_info->date,
                        result->impl_info->vendor);
    }

    qapi_free_MCDAPIVersion(qapi_args.version_req);
    qapi_free_MCDInitializeResult(result);

    /* Incompatible version */
    version_req = (mcd_api_version_st) {
        .v_api_major = MCD_API_VER_MAJOR,
        .v_api_minor = MCD_API_VER_MINOR + 1,
        .author = "",
    };

    qapi_args.version_req = marshal_mcd_api_version(&version_req);
    result = qtest_mcd_initialize(&qts, &qapi_args);
    g_assert(result->return_status != MCD_RET_ACT_NONE);

    error_info = qtest_mcd_qry_error_info(&qts, &qry_error_info_args);
    g_assert(error_info->error_code == MCD_ERR_GENERAL);

    if (verbose) {
        fprintf(stderr, "[INFO]\tInitialization with newer API failed "
                        "successfully: %s\n", error_info->error_str);
    }

    qapi_free_MCDAPIVersion(qapi_args.version_req);
    qapi_free_MCDInitializeResult(result);
    qapi_free_MCDErrorInfo(error_info);

    mcdtest_quit(&qts);
}

static void test_qry_servers(void)
{
    QTestStateMCD qts = mcdtest_init(QEMU_EXTRA_ARGS);

    char host[] = "";

    q_obj_mcd_qry_servers_arg qapi_args = {
        .host = host,
        .running = true,
        .start_index = 0,
        .num_servers = 0,
    };

    MCDQryServersResult *result = qtest_mcd_qry_servers(&qts, &qapi_args);
    g_assert(result->return_status == MCD_RET_ACT_NONE);
    g_assert(result->has_num_servers);
    g_assert(result->num_servers == 1);
    g_assert(result->has_server_info == false);

    qapi_args.num_servers = result->num_servers;
    qapi_free_MCDQryServersResult(result);

    result = qtest_mcd_qry_servers(&qts, &qapi_args);

    g_assert(result->return_status == MCD_RET_ACT_NONE);
    g_assert(result->has_num_servers);
    g_assert(result->num_servers == 1);
    g_assert(result->has_server_info);

    if (verbose) {
        MCDServerInfo *server_info = result->server_info->value;
        fprintf(stderr, "[INFO]\tServer info: %s (%s)\n",
                        server_info->server,
                        server_info->system_instance);
    }

    qapi_free_MCDQryServersResult(result);
    mcdtest_quit(&qts);
}

static void test_open_server(void)
{
    QTestStateMCD qts = mcdtest_init(QEMU_EXTRA_ARGS);

    char empty_string[] = "";

    q_obj_mcd_open_server_arg open_server_args = {
        .system_key = empty_string,
        .config_string = empty_string,
    };

    q_obj_mcd_close_server_arg close_server_args;

    MCDOpenServerResult *open_server_result;
    MCDCloseServerResult *close_server_result;

    open_server_result = qtest_mcd_open_server(&qts, &open_server_args);
    g_assert(open_server_result->return_status == MCD_RET_ACT_NONE);
    g_assert(open_server_result->has_server_uid);

    close_server_args.server_uid = open_server_result->server_uid;
    qapi_free_MCDOpenServerResult(open_server_result);

    /* Check that server cannot be opened twice */
    open_server_result = qtest_mcd_open_server(&qts, &open_server_args);
    g_assert(open_server_result->return_status != MCD_RET_ACT_NONE);

    if (verbose) {
        q_obj_mcd_qry_error_info_arg qry_error_info_args = { .core_uid = 0 };
        MCDErrorInfo *error_info = qtest_mcd_qry_error_info(&qts,
            &qry_error_info_args);
        fprintf(stderr, "[INFO]\tServer cannot be opened twice: %s\n",
                        error_info->error_str);
        qapi_free_MCDErrorInfo(error_info);
    }

    qapi_free_MCDOpenServerResult(open_server_result);
    close_server_result = qtest_mcd_close_server(&qts, &close_server_args);
    g_assert(close_server_result->return_status == MCD_RET_ACT_NONE);
    qapi_free_MCDCloseServerResult(close_server_result);

    /* Check that server cannot be closed twice */
    close_server_result = qtest_mcd_close_server(&qts, &close_server_args);
    g_assert(close_server_result->return_status != MCD_RET_ACT_NONE);

    if (verbose) {
        q_obj_mcd_qry_error_info_arg qry_error_info_args = { .core_uid = 0 };
        MCDErrorInfo *error_info = qtest_mcd_qry_error_info(&qts,
            &qry_error_info_args);
        fprintf(stderr, "[INFO]\tServer cannot be closed twice: %s\n",
                        error_info->error_str);
        qapi_free_MCDErrorInfo(error_info);
    }

    qapi_free_MCDCloseServerResult(close_server_result);
    mcdtest_quit(&qts);
}

static void test_qry_cores(void)
{
    QTestStateMCD qts = mcdtest_init(QEMU_EXTRA_ARGS);

    MCDQryCoresResult *qry_cores_result = open_server_query_cores(&qts);

    if (verbose) {
        MCDCoreConInfoList *core_head = qry_cores_result->core_con_info;
        for (uint32_t c = 0; c < qry_cores_result->num_cores; c++) {
            MCDCoreConInfo *core_con = core_head->value;
            if (verbose) {
                fprintf(stderr, "[INFO]\tSystem: %s\n"
                                "\tDevice: %s\n"
                                "\tCore:   %s (#%d)\n",
                                core_con->system,
                                core_con->device,
                                core_con->core, core_con->core_id);
            }
            core_head = core_head->next;
        }
    }

    qapi_free_MCDQryCoresResult(qry_cores_result);
    qtest_mcd_exit(&qts);
    mcdtest_quit(&qts);
}

static void test_open_core(void)
{
    QTestStateMCD qts = mcdtest_init(QEMU_EXTRA_ARGS);
    MCDQryCoresResult *cores_query = open_server_query_cores(&qts);

    MCDCoreConInfoList *core_head = cores_query->core_con_info;
    for (uint32_t c = 0; c < cores_query->num_cores; c++) {
        q_obj_mcd_close_core_arg close_core_args;
        MCDCloseCoreResult *close_core_result;

        MCDCoreConInfo *core_con_info = core_head->value;
        q_obj_mcd_open_core_arg open_core_args = {
            .core_con_info = core_con_info,
        };

        q_obj_mcd_qry_error_info_arg error_info_args = {
            .core_uid = 0,
        };
        MCDErrorInfo *last_server_error;

        MCDOpenCoreResult *open_core_result =
            qtest_mcd_open_core(&qts, &open_core_args);
        g_assert(open_core_result->return_status == MCD_RET_ACT_NONE);
        g_assert(open_core_result->has_core_uid);

        if (verbose) {
            fprintf(stderr, "[INFO]\tCore #%d open with UID %d\n",
                             core_con_info->core_id,
                             open_core_result->core_uid);
        }

        close_core_args.core_uid = open_core_result->core_uid;

        /* Verify that core cannot be opened twice */
        qapi_free_MCDOpenCoreResult(open_core_result);
        open_core_result = qtest_mcd_open_core(&qts, &open_core_args);
        g_assert(open_core_result->return_status != MCD_RET_ACT_NONE);

        last_server_error = qtest_mcd_qry_error_info(&qts, &error_info_args);
        if (verbose) {
            fprintf(stderr, "[INFO]\tCore cannot be opened twice: %s\n",
                            last_server_error->error_str);
        }
        qapi_free_MCDErrorInfo(last_server_error);

        close_core_result = qtest_mcd_close_core(&qts, &close_core_args);
        g_assert(close_core_result->return_status == MCD_RET_ACT_NONE);

        if (verbose) {
            fprintf(stderr, "[INFO]\tCore with UID %d closed\n",
                            close_core_args.core_uid);
        }

        /* Check that core cannot be closed twice */
        qapi_free_MCDCloseCoreResult(close_core_result);
        close_core_result = qtest_mcd_close_core(&qts, &close_core_args);
        g_assert(close_core_result->return_status != MCD_RET_ACT_NONE);

        last_server_error = qtest_mcd_qry_error_info(&qts, &error_info_args);
        if (verbose) {
            fprintf(stderr, "[INFO]\tCore cannot be closed twice: %s\n",
                            last_server_error->error_str);
        }
        qapi_free_MCDErrorInfo(last_server_error);

        qapi_free_MCDCloseCoreResult(close_core_result);
        qapi_free_MCDOpenCoreResult(open_core_result);
        core_head = core_head->next;
    }

    qapi_free_MCDQryCoresResult(cores_query);
    qtest_mcd_exit(&qts);
    mcdtest_quit(&qts);
}

int main(int argc, char *argv[])
{
    char *v_env = getenv("V");
    verbose = v_env && atoi(v_env) >= 1;
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("mcd/initialize-mcdtest", test_initialize_mcdtest);
    qtest_add_func("mcd/initialize", test_initialize);
    qtest_add_func("mcd/qry-servers", test_qry_servers);
    qtest_add_func("mcd/open-server", test_open_server);
    qtest_add_func("mcd/qry-cores", test_qry_cores);
    qtest_add_func("mcd/open-core", test_open_core);
    return g_test_run();
}
