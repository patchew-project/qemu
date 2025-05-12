/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Architecture independent QTests for the MCD server with QAPI stub
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "mcd/mcd_api.h"
#include "mcd/libmcd_qapi.h"
#include "qapi/qapi-visit-mcd.h"
#include "qapi/qapi-types-mcd.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/compat-policy.h"
#include "libmcd-test.h"

#define QEMU_EXTRA_ARGS ""

static bool verbose;

static void test_initialize(void)
{
    QTestState *qts = qtest_init(QEMU_EXTRA_ARGS);
    MCDErrorInfo *error_info;

    mcd_api_version_st version_req = {
        .v_api_major = MCD_API_VER_MAJOR,
        .v_api_minor = MCD_API_VER_MINOR,
        .author = "",
    };

    q_obj_mcd_initialize_arg qapi_args = {
        .version_req = marshal_mcd_api_version(&version_req),
    };

    MCDInitializeResult *result = qtest_mcd_initialize(qts, &qapi_args);
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
    result = qtest_mcd_initialize(qts, &qapi_args);
    g_assert(result->return_status != MCD_RET_ACT_NONE);

    error_info = qtest_mcd_qry_error_info(qts);
    g_assert(error_info->error_code == MCD_ERR_GENERAL);

    if (verbose) {
        fprintf(stderr, "[INFO]\tInitialization with newer API failed "
                        "successfully: %s\n", error_info->error_str);
    }

    qapi_free_MCDAPIVersion(qapi_args.version_req);
    qapi_free_MCDInitializeResult(result);
    qapi_free_MCDErrorInfo(error_info);

    qtest_quit(qts);
}

static void test_qry_servers(void)
{
    QTestState *qts = qtest_init(QEMU_EXTRA_ARGS);

    char host[] = "";

    q_obj_mcd_qry_servers_arg qapi_args = {
        .host = host,
        .running = true,
        .start_index = 0,
        .num_servers = 0,
    };

    MCDQryServersResult *result = qtest_mcd_qry_servers(qts, &qapi_args);
    g_assert(result->return_status == MCD_RET_ACT_NONE);
    g_assert(result->has_num_servers);
    g_assert(result->num_servers == 1);
    g_assert(result->has_server_info == false);

    qapi_args.num_servers = result->num_servers;
    qapi_free_MCDQryServersResult(result);

    result = qtest_mcd_qry_servers(qts, &qapi_args);

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
    qtest_quit(qts);
}

static void test_open_server(void)
{
    QTestState *qts = qtest_init(QEMU_EXTRA_ARGS);

    char empty_string[] = "";

    q_obj_mcd_open_server_arg open_server_args = {
        .system_key = empty_string,
        .config_string = empty_string,
    };

    q_obj_mcd_close_server_arg close_server_args;

    MCDOpenServerResult *open_server_result;
    MCDCloseServerResult *close_server_result;

    open_server_result = qtest_mcd_open_server(qts, &open_server_args);
    g_assert(open_server_result->return_status == MCD_RET_ACT_NONE);
    g_assert(open_server_result->has_server_uid);

    close_server_args.server_uid = open_server_result->server_uid;
    qapi_free_MCDOpenServerResult(open_server_result);

    /* Check that server cannot be opened twice */
    open_server_result = qtest_mcd_open_server(qts, &open_server_args);
    g_assert(open_server_result->return_status != MCD_RET_ACT_NONE);

    if (verbose) {
        MCDErrorInfo *error_info = qtest_mcd_qry_error_info(qts);
        fprintf(stderr, "[INFO]\tServer cannot be opened twice: %s\n",
                        error_info->error_str);
        qapi_free_MCDErrorInfo(error_info);
    }

    qapi_free_MCDOpenServerResult(open_server_result);
    close_server_result = qtest_mcd_close_server(qts, &close_server_args);
    g_assert(close_server_result->return_status == MCD_RET_ACT_NONE);
    qapi_free_MCDCloseServerResult(close_server_result);

    /* Check that server cannot be closed twice */
    close_server_result = qtest_mcd_close_server(qts, &close_server_args);
    g_assert(close_server_result->return_status != MCD_RET_ACT_NONE);

    if (verbose) {
        MCDErrorInfo *error_info = qtest_mcd_qry_error_info(qts);
        fprintf(stderr, "[INFO]\tServer cannot be closed twice: %s\n",
                        error_info->error_str);
        qapi_free_MCDErrorInfo(error_info);
    }

    qapi_free_MCDCloseServerResult(close_server_result);
    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    char *v_env = getenv("V");
    verbose = v_env && atoi(v_env) >= 1;
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("mcd/initialize", test_initialize);
    qtest_add_func("mcd/qry-servers", test_qry_servers);
    qtest_add_func("mcd/open-server", test_open_server);
    return g_test_run();
}
