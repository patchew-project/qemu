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

    error_info = qtest_mcd_qry_error_info(&qts);
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

int main(int argc, char *argv[])
{
    char *v_env = getenv("V");
    verbose = v_env && atoi(v_env) >= 1;
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("mcd/initialize-mcdtest", test_initialize_mcdtest);
    qtest_add_func("mcd/initialize", test_initialize);
    return g_test_run();
}
