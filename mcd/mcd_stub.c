/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mcdstub - Conversion of MCD QAPI requests to MCD server function calls
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "mcd_api.h"
#include "mcd_qapi.h"
#include "mcd/mcd-qapi-commands.h"

/**
 * struct mcdstub_state - State of the MCD server stub
 *
 * @open_server:     Open server instance as allocated in mcd_open_server_f().
 * @open_server_uid: Unique identifier of the open server.
 */
typedef struct mcdstub_state {
    mcd_server_st *open_server;
    uint32_t open_server_uid;
} mcdstub_state;


static mcdstub_state g_stub_state = {
    .open_server = NULL,
    .open_server_uid = 0,
};

static uint32_t store_open_server(mcd_server_st *server)
{
    g_stub_state.open_server = server;
    g_stub_state.open_server_uid++;
    return g_stub_state.open_server_uid;
}

static mcd_server_st *retrieve_open_server(uint32_t server_uid)
{
    if (server_uid == g_stub_state.open_server_uid) {
        return g_stub_state.open_server;
    } else {
        return NULL;
    }
}

MCDInitializeResult *qmp_mcd_initialize(MCDAPIVersion *version_req,
                                        Error **errp)
{
    mcd_impl_version_info_st impl_info;
    MCDInitializeResult *result = g_malloc0(sizeof(*result));
    mcd_api_version_st version_req_unmarshalled =
        unmarshal_mcd_api_version(version_req);

    result->return_status = mcd_initialize_f(&version_req_unmarshalled,
                                             &impl_info);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->impl_info = marshal_mcd_impl_version_info(&impl_info);
    }

    return result;
}

void qmp_mcd_exit(Error **errp)
{
    mcd_exit_f();
}

MCDQryServersResult *qmp_mcd_qry_servers(const char *host, bool running,
                                         uint32_t start_index,
                                         uint32_t num_servers, Error **errp)
{
    MCDServerInfoList **tailp;
    MCDServerInfo *info;
    mcd_server_info_st *server_info = NULL;
    bool query_num_only = num_servers == 0;
    MCDQryServersResult *result = g_malloc0(sizeof(*result));

    if (!query_num_only) {
        server_info = g_malloc0(num_servers * sizeof(*server_info));
    }

    result->return_status = mcd_qry_servers_f(host, running, start_index,
                                              &num_servers, server_info);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->has_num_servers = true;
        result->num_servers = num_servers;
        if (!query_num_only) {
            result->has_server_info = true;
            tailp = &(result->server_info);
            for (uint32_t i = 0; i < num_servers; i++) {
                info = marshal_mcd_server_info(server_info + i);
                QAPI_LIST_APPEND(tailp, info);
            }
        }
    }

    if (!query_num_only) {
        g_free(server_info);
    }

    return result;
}

MCDOpenServerResult *qmp_mcd_open_server(const char *system_key,
                                         const char *config_string,
                                         Error **errp)
{
    MCDOpenServerResult *result = g_malloc0(sizeof(*result));
    mcd_server_st *server;

    result->return_status = mcd_open_server_f(system_key, config_string,
                                              &server);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->has_server_uid = true;
        result->server_uid = store_open_server(server);
        result->host = g_strdup(server->host);
        result->config_string = g_strdup(server->config_string);
    }

    return result;
}

MCDCloseServerResult *qmp_mcd_close_server(uint32_t server_uid, Error **errp)
{
    MCDCloseServerResult *result = g_malloc0(sizeof(*result));
    mcd_server_st *server = retrieve_open_server(server_uid);
    result->return_status = mcd_close_server_f(server);
    return result;
}

MCDErrorInfo *qmp_mcd_qry_error_info(Error **errp)
{
    MCDErrorInfo *result;
    mcd_error_info_st error_info;
    mcd_qry_error_info_f(NULL, &error_info);
    result = marshal_mcd_error_info(&error_info);
    return result;
}
