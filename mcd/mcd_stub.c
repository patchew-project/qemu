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
 * @open_server:         Open server instance as allocated in
 *                       mcd_open_server_f().
 * @open_server_uid:     Unique identifier of the open server.
 * @open_cores:          Array of open cores.
 * @custom_error:        Last error which occurred in the server stub.
 * @on_error_ask_server: Call mcd_qry_error_info_f() when asked for most recent
 *                       error.
 */
typedef struct mcdstub_state {
    mcd_server_st *open_server;
    uint32_t open_server_uid;
    GPtrArray *open_cores;
    mcd_error_info_st custom_error;
    bool on_error_ask_server;
} mcdstub_state;


static mcdstub_state g_stub_state = {
    .open_server = NULL,
    .open_server_uid = 0,
    .on_error_ask_server = true,
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

static uint32_t store_open_core(mcd_core_st *core)
{
    /* core_uid 0 is reserved */
    uint32_t core_uid = core->core_con_info->core_id + 1;
    mcd_core_st **core_p;

    if (!g_stub_state.open_cores) {
        g_stub_state.open_cores = g_ptr_array_new();
    }

    if (core_uid > g_stub_state.open_cores->len) {
        g_ptr_array_set_size(g_stub_state.open_cores, core_uid);
    }

    core_p = (mcd_core_st **) &g_ptr_array_index(g_stub_state.open_cores,
                                                 core_uid - 1);
    *core_p = core;
    return core_uid;
}

static void remove_closed_core(uint32_t core_uid)
{
    if (core_uid <= g_stub_state.open_cores->len) {
        mcd_core_st ** core_p = (mcd_core_st **) &g_ptr_array_index(
            g_stub_state.open_cores, core_uid - 1);
        *core_p = NULL;
    }
}

static mcd_return_et retrieve_open_core(uint32_t core_uid, mcd_core_st **core)
{
    if (core_uid > 0 &&
       (!g_stub_state.open_cores || core_uid > g_stub_state.open_cores->len)) {
        g_stub_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_PARAM,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "stub: core UID not found",
        };
        return g_stub_state.custom_error.return_status;
    }

    g_assert(core);

    if (!core_uid) {
        *core = NULL;
    } else {
        *core = g_ptr_array_index(g_stub_state.open_cores, core_uid - 1);
    }

    return MCD_RET_ACT_NONE;
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

    g_stub_state.on_error_ask_server = true;
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

    g_stub_state.on_error_ask_server = true;
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

    g_stub_state.on_error_ask_server = true;
    return result;
}

MCDCloseServerResult *qmp_mcd_close_server(uint32_t server_uid, Error **errp)
{
    MCDCloseServerResult *result = g_malloc0(sizeof(*result));
    mcd_server_st *server = retrieve_open_server(server_uid);
    result->return_status = mcd_close_server_f(server);
    return result;
}

MCDQrySystemsResult *qmp_mcd_qry_systems(uint32_t start_index,
                                         uint32_t num_systems, Error **errp)
{
    MCDCoreConInfoList **tailp;
    MCDCoreConInfo *info;
    mcd_core_con_info_st *system_con_info = NULL;
    bool query_num_only = num_systems == 0;
    MCDQrySystemsResult *result = g_malloc0(sizeof(*result));

    if (!query_num_only) {
        system_con_info = g_malloc0(num_systems * sizeof(*system_con_info));
    }

    result->return_status = mcd_qry_systems_f(start_index, &num_systems,
                                              system_con_info);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->has_num_systems = true;
        result->num_systems = num_systems;
        if (!query_num_only) {
            result->has_system_con_info = true;
            tailp = &(result->system_con_info);
            for (uint32_t i = 0; i < num_systems; i++) {
                info = marshal_mcd_core_con_info(system_con_info + i);
                QAPI_LIST_APPEND(tailp, info);
            }
        }
    }

    if (!query_num_only) {
        g_free(system_con_info);
    }

    g_stub_state.on_error_ask_server = true;
    return result;
}

MCDQryDevicesResult *qmp_mcd_qry_devices(MCDCoreConInfo *system_con_info,
                                         uint32_t start_index,
                                         uint32_t num_devices, Error **errp)
{
    MCDCoreConInfoList **tailp;
    MCDCoreConInfo *info;
    mcd_core_con_info_st *device_con_info = NULL;
    bool query_num_only = num_devices == 0;
    MCDQryDevicesResult *result = g_malloc0(sizeof(*result));
    mcd_core_con_info_st system_con_info_unmarshalled =
        unmarshal_mcd_core_con_info(system_con_info);

    if (!query_num_only) {
        device_con_info = g_malloc0(num_devices * sizeof(*device_con_info));
    }

    result->return_status = mcd_qry_devices_f(&system_con_info_unmarshalled,
                                              start_index, &num_devices,
                                              device_con_info);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->has_num_devices = true;
        result->num_devices = num_devices;
        if (!query_num_only) {
            result->has_device_con_info = true;
            tailp = &(result->device_con_info);
            for (uint32_t i = 0; i < num_devices; i++) {
                info = marshal_mcd_core_con_info(device_con_info + i);
                QAPI_LIST_APPEND(tailp, info);
            }
        }
    }

    if (!query_num_only) {
        g_free(device_con_info);
    }

    g_stub_state.on_error_ask_server = true;
    return result;
}

MCDQryCoresResult *qmp_mcd_qry_cores(MCDCoreConInfo *connection_info,
                                     uint32_t start_index, uint32_t num_cores,
                                     Error **errp)
{
    MCDCoreConInfoList **tailp;
    MCDCoreConInfo *info;
    mcd_core_con_info_st *core_con_info = NULL;
    bool query_num_only = num_cores == 0;
    MCDQryCoresResult *result = g_malloc0(sizeof(*result));
    mcd_core_con_info_st connection_info_unmarshalled =
        unmarshal_mcd_core_con_info(connection_info);

    if (!query_num_only) {
        core_con_info = g_malloc0(num_cores * sizeof(*core_con_info));
    }

    result->return_status = mcd_qry_cores_f(&connection_info_unmarshalled,
                                            start_index, &num_cores,
                                            core_con_info);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->has_num_cores = true;
        result->num_cores = num_cores;
        if (!query_num_only) {
            result->has_core_con_info = true;
            tailp = &(result->core_con_info);
            for (uint32_t i = 0; i < num_cores; i++) {
                info = marshal_mcd_core_con_info(core_con_info + i);
                QAPI_LIST_APPEND(tailp, info);
            }
        }
    }

    if (!query_num_only) {
        g_free(core_con_info);
    }

    g_stub_state.on_error_ask_server = true;
    return result;
}

MCDOpenCoreResult *qmp_mcd_open_core(MCDCoreConInfo *core_con_info,
                                     Error **errp)
{
    MCDOpenCoreResult *result = g_malloc0(sizeof(*result));
    mcd_core_st *core;
    mcd_core_con_info_st core_con_info_unmarshalled =
        unmarshal_mcd_core_con_info(core_con_info);

    result->return_status =  mcd_open_core_f(&core_con_info_unmarshalled,
                                             &core);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->has_core_uid = true;
        result->core_uid = store_open_core(core);
        result->core_con_info = marshal_mcd_core_con_info(core->core_con_info);
    }

    g_stub_state.on_error_ask_server = true;
    return result;
}

MCDCloseCoreResult *qmp_mcd_close_core(uint32_t core_uid, Error **errp)
{
    MCDCloseCoreResult *result = g_malloc0(sizeof(*result));
    mcd_core_st *core = NULL;

    result->return_status = retrieve_open_core(core_uid, &core);
    if (result->return_status != MCD_RET_ACT_NONE) {
        g_stub_state.on_error_ask_server = false;
        return result;
    }

    result->return_status = mcd_close_core_f(core);

    if (result->return_status == MCD_RET_ACT_NONE) {
        remove_closed_core(core_uid);
    }

    g_stub_state.on_error_ask_server = true;
    return result;
}

MCDErrorInfo *qmp_mcd_qry_error_info(uint32_t core_uid, Error **errp)
{
    MCDErrorInfo *result;
    mcd_core_st *core = NULL;
    mcd_error_info_st error_info;

    if (retrieve_open_core(core_uid, &core) != MCD_RET_ACT_NONE) {
        g_stub_state.on_error_ask_server = false;
    }

    if (g_stub_state.on_error_ask_server) {
        mcd_qry_error_info_f(core, &error_info);
    } else {
        error_info = g_stub_state.custom_error;
    }

    result = marshal_mcd_error_info(&error_info);
    return result;
}

MCDQryMemSpacesResult *qmp_mcd_qry_mem_spaces(uint32_t core_uid,
                                              uint32_t start_index,
                                              uint32_t num_mem_spaces,
                                              Error **errp)
{
    MCDMemspaceList **tailp;
    MCDMemspace *ms;
    mcd_memspace_st *memspaces = NULL;
    bool query_num_only = num_mem_spaces == 0;
    MCDQryMemSpacesResult *result = g_malloc0(sizeof(*result));
    mcd_core_st *core = NULL;

    if (retrieve_open_core(core_uid, &core) != MCD_RET_ACT_NONE) {
        g_stub_state.on_error_ask_server = false;
    }

    if (!query_num_only) {
        memspaces = g_malloc0(num_mem_spaces * sizeof(*memspaces));
    }

    result->return_status = mcd_qry_mem_spaces_f(core, start_index,
                                                 &num_mem_spaces, memspaces);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->has_num_mem_spaces = true;
        result->num_mem_spaces = num_mem_spaces;
        if (!query_num_only) {
            result->has_mem_spaces = true;
            tailp = &(result->mem_spaces);
            for (uint32_t i = 0; i < num_mem_spaces; i++) {
                ms = marshal_mcd_memspace(memspaces + i);
                QAPI_LIST_APPEND(tailp, ms);
            }
        }
    }

    if (!query_num_only) {
        g_free(memspaces);
    }

    g_stub_state.on_error_ask_server = true;
    return result;
}

MCDQryRegGroupsResult *qmp_mcd_qry_reg_groups(uint32_t core_uid,
                                              uint32_t start_index,
                                              uint32_t num_reg_groups,
                                              Error **errp)
{
    MCDRegisterGroupList **tailp;
    MCDRegisterGroup *rg;
    mcd_register_group_st *reg_groups = NULL;
    bool query_num_only = num_reg_groups == 0;
    MCDQryRegGroupsResult *result = g_malloc0(sizeof(*result));
    mcd_core_st *core = NULL;

    result->return_status = retrieve_open_core(core_uid, &core);
    if (result->return_status != MCD_RET_ACT_NONE) {
        g_stub_state.on_error_ask_server = false;
        return result;
    }

    if (!query_num_only) {
        reg_groups = g_malloc0(num_reg_groups * sizeof(*reg_groups));
    }

    result->return_status = mcd_qry_reg_groups_f(core, start_index,
                                                 &num_reg_groups, reg_groups);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->has_num_reg_groups = true;
        result->num_reg_groups = num_reg_groups;
        if (!query_num_only) {
            result->has_reg_groups = true;
            tailp = &(result->reg_groups);
            for (uint32_t i = 0; i < num_reg_groups; i++) {
                rg = marshal_mcd_register_group(reg_groups + i);
                QAPI_LIST_APPEND(tailp, rg);
            }
        }
    }

    if (!query_num_only) {
        g_free(reg_groups);
    }

    g_stub_state.on_error_ask_server = true;
    return result;
}

MCDQryRegMapResult *qmp_mcd_qry_reg_map(uint32_t core_uid,
                                        uint32_t reg_group_id,
                                        uint32_t start_index, uint32_t num_regs,
                                        Error **errp)
{
    MCDRegisterInfoList **tailp;
    MCDRegisterInfo *r;
    mcd_register_info_st *regs = NULL;
    bool query_num_only = num_regs == 0;
    MCDQryRegMapResult *result = g_malloc0(sizeof(*result));
    mcd_core_st *core = NULL;

    result->return_status = retrieve_open_core(core_uid, &core);
    if (result->return_status != MCD_RET_ACT_NONE) {
        g_stub_state.on_error_ask_server = false;
        return result;
    }

    if (!query_num_only) {
        regs = g_malloc0(num_regs * sizeof(*regs));
    }

    result->return_status = mcd_qry_reg_map_f(core, reg_group_id,
                                              start_index, &num_regs,
                                              regs);

    if (result->return_status == MCD_RET_ACT_NONE) {
        result->has_num_regs = true;
        result->num_regs = num_regs;
        if (!query_num_only) {
            result->has_reg_info = true;
            tailp = &(result->reg_info);
            for (uint32_t i = 0; i < num_regs; i++) {
                r = marshal_mcd_register_info(regs + i);
                QAPI_LIST_APPEND(tailp, r);
            }
        }
    }

    if (!query_num_only) {
        g_free(regs);
    }

    g_stub_state.on_error_ask_server = true;
    return result;
}
