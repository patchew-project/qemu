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

MCDCoreConInfo *marshal_mcd_core_con_info(const mcd_core_con_info_st *con_info)
{
    MCDCoreConInfo *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDCoreConInfo) {
        .host = g_strdup(con_info->host),
        .server_port = con_info->server_port,
        .server_key = g_strdup(con_info->server_key),
        .system_key = g_strdup(con_info->system_key),
        .device_key = g_strdup(con_info->device_key),
        .system = g_strdup(con_info->system),
        .system_instance = g_strdup(con_info->system_instance),
        .acc_hw = g_strdup(con_info->acc_hw),
        .device_type = con_info->device_type,
        .device = g_strdup(con_info->device),
        .device_id = con_info->device_id,
        .core = g_strdup(con_info->core),
        .core_type = con_info->core_type,
        .core_id = con_info->core_id,
    };

    return marshal;
}

mcd_core_con_info_st unmarshal_mcd_core_con_info(MCDCoreConInfo *con_info)
{
    mcd_core_con_info_st unmarshal = {
        .server_port = con_info->server_port,
        .device_type = con_info->device_type,
        .device_id = con_info->device_id,
        .core_type = con_info->core_type,
        .core_id = con_info->core_id,
    };

    strncpy(unmarshal.host, con_info->host, MCD_HOSTNAME_LEN - 1);
    strncpy(unmarshal.server_key, con_info->server_key, MCD_KEY_LEN - 1);
    strncpy(unmarshal.system_key, con_info->system_key, MCD_KEY_LEN - 1);
    strncpy(unmarshal.device_key, con_info->device_key, MCD_KEY_LEN - 1);
    strncpy(unmarshal.system, con_info->system, MCD_UNIQUE_NAME_LEN - 1);
    strncpy(unmarshal.system_instance, con_info->system_instance,
            MCD_UNIQUE_NAME_LEN - 1);
    strncpy(unmarshal.acc_hw, con_info->acc_hw, MCD_UNIQUE_NAME_LEN - 1);
    strncpy(unmarshal.device, con_info->device, MCD_UNIQUE_NAME_LEN - 1);
    strncpy(unmarshal.core, con_info->core, MCD_UNIQUE_NAME_LEN - 1);

    return unmarshal;
}

MCDMemspace *marshal_mcd_memspace(const mcd_memspace_st *mem_space)
{
    MCDMemspace *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDMemspace) {
        .mem_space_id = mem_space->mem_space_id,
        .mem_space_name = g_strdup(mem_space->mem_space_name),
        .mem_type = mem_space->mem_type,
        .bits_per_mau = mem_space->bits_per_mau,
        .invariance = mem_space->invariance,
        .endian = mem_space->endian,
        .min_addr = mem_space->min_addr,
        .max_addr = mem_space->max_addr,
        .num_mem_blocks = mem_space->num_mem_blocks,
        .supported_access_options = mem_space->supported_access_options,
        .core_mode_mask_read = mem_space->core_mode_mask_read,
        .core_mode_mask_write = mem_space->core_mode_mask_write,
    };

    return marshal;
}

MCDRegisterGroup *marshal_mcd_register_group(
    const mcd_register_group_st *reg_group)
{
    MCDRegisterGroup *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDRegisterGroup) {
        .reg_group_id = reg_group->reg_group_id,
        .reg_group_name = g_strdup(reg_group->reg_group_name),
        .n_registers = reg_group->n_registers,
    };

    return marshal;
}

MCDAddr *marshal_mcd_addr(const mcd_addr_st *addr)
{
    MCDAddr *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDAddr) {
        .address = addr->address,
        .mem_space_id = addr->mem_space_id,
        .addr_space_id = addr->addr_space_id,
        .addr_space_type = addr->addr_space_type,
    };

    return marshal;
}

MCDRegisterInfo *marshal_mcd_register_info(const mcd_register_info_st *reg_info)
{
    MCDRegisterInfo *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDRegisterInfo) {
        .addr = marshal_mcd_addr(&reg_info->addr),
        .reg_group_id = reg_info->reg_group_id,
        .regname = g_strdup(reg_info->regname),
        .regsize = reg_info->regsize,
        .core_mode_mask_read = reg_info->core_mode_mask_read,
        .core_mode_mask_write = reg_info->core_mode_mask_write,
        .side_effects_read = reg_info->has_side_effects_read,
        .side_effects_write = reg_info->has_side_effects_write,
        .reg_type = reg_info->reg_type,
        .hw_thread_id = reg_info->hw_thread_id,
    };

    return marshal;
}

MCDCoreState *marshal_mcd_core_state(const mcd_core_state_st *state)
{
    MCDCoreState *marshal = g_malloc0(sizeof(*marshal));

    *marshal = (MCDCoreState) {
        .state = state->state,
        .event = state->event,
        .hw_thread_id = state->hw_thread_id,
        .trig_id = state->trig_id,
        .stop_str = g_strdup(state->stop_str),
        .info_str = g_strdup(state->info_str),
    };

    return marshal;
}
