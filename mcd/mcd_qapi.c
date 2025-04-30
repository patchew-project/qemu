/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QAPI marshalling helpers for structures of the MCD API
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "mcd_qapi.h"

MCDAPIVersion *marshal_mcd_api_version(const mcd_api_version_st *api_version)
{
    MCDAPIVersion *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDAPIVersion) {
        .v_api_major = api_version->v_api_major,
        .v_api_minor = api_version->v_api_minor,
        .author = g_strdup(api_version->author),
    };

    return marshal;
}

mcd_api_version_st unmarshal_mcd_api_version(MCDAPIVersion *api_version)
{
    mcd_api_version_st unmarshal =  {
        .v_api_major = api_version->v_api_major,
        .v_api_minor = api_version->v_api_minor,
    };
    strncpy(unmarshal.author, api_version->author, MCD_API_IMP_VENDOR_LEN - 1);
    return unmarshal;
}

MCDImplVersionInfo *marshal_mcd_impl_version_info(
    const mcd_impl_version_info_st *impl_info)
{
    MCDImplVersionInfo *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDImplVersionInfo) {
        .v_api = marshal_mcd_api_version(&impl_info->v_api),
        .v_imp_major = impl_info->v_imp_major,
        .v_imp_minor = impl_info->v_imp_minor,
        .v_imp_build = impl_info->v_imp_build,
        .vendor = g_strdup(impl_info->vendor),
        .date = g_strdup(impl_info->date),
    };

    return marshal;
}

MCDErrorInfo *marshal_mcd_error_info(const mcd_error_info_st *error_info)
{
    MCDErrorInfo *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDErrorInfo) {
        .return_status = error_info->return_status,
        .error_code = error_info->error_code,
        .error_events = error_info->error_events,
        .error_str = g_strdup(error_info->error_str),
    };

    return marshal;
}

MCDServerInfo *marshal_mcd_server_info(const mcd_server_info_st *server_info)
{
    MCDServerInfo *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDServerInfo) {
        .server = g_strdup(server_info->server),
        .system_instance = g_strdup(server_info->system_instance),
        .acc_hw = g_strdup(server_info->acc_hw),
    };

    return marshal;
}
