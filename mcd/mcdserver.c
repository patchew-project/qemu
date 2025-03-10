/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mcdserver - Multi-Core Debug (MCD) API implementation
 *
 * Copyright (c) 2025 Lauterbach GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "mcd_api.h"

static const mcd_error_info_st MCD_ERROR_NOT_IMPLEMENTED = {
    .return_status = MCD_RET_ACT_HANDLE_ERROR,
    .error_code = MCD_ERR_FN_UNIMPLEMENTED,
    .error_events = MCD_ERR_EVT_NONE,
    .error_str = "",
};

static const mcd_error_info_st MCD_ERROR_INVALID_NULL_PARAM = {
    .return_status = MCD_RET_ACT_HANDLE_ERROR,
    .error_code = MCD_ERR_PARAM,
    .error_events = MCD_ERR_EVT_NONE,
    .error_str = "null was invalidly passed as a parameter",
};

static const mcd_error_info_st MCD_ERROR_NONE = {
    .return_status = MCD_RET_ACT_NONE,
    .error_code = MCD_ERR_NONE,
    .error_events = MCD_ERR_EVT_NONE,
    .error_str = "",
};

/* reserves memory for custom errors */
static mcd_error_info_st custom_mcd_error;

/**
 * struct mcdserver_state - State of the MCD server
 *
 * @last_error: Error info of most recent executed function.
 */
typedef struct mcdserver_state {
    const mcd_error_info_st *last_error;
} mcdserver_state;

static mcdserver_state g_server_state = {
    .last_error = &MCD_ERROR_NONE,
};

mcd_return_et mcd_initialize_f(const mcd_api_version_st *version_req,
                               mcd_impl_version_info_st *impl_info)
{
    if (!version_req || !impl_info) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    *impl_info = (mcd_impl_version_info_st) {
        .v_api = (mcd_api_version_st) {
            .v_api_major = MCD_API_VER_MAJOR,
            .v_api_minor = MCD_API_VER_MINOR,
            .author = MCD_API_VER_AUTHOR,
        },
        .v_imp_major = QEMU_VERSION_MAJOR,
        .v_imp_minor = QEMU_VERSION_MINOR,
        .v_imp_build = 0,
        .vendor = "QEMU",
        .date = __DATE__,
    };

    if (version_req->v_api_major == MCD_API_VER_MAJOR &&
        version_req->v_api_minor <= MCD_API_VER_MINOR) {
        g_server_state.last_error = &MCD_ERROR_NONE;
    } else {
        custom_mcd_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_GENERAL,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "incompatible versions",
        };
        g_server_state.last_error = &custom_mcd_error;
    }

    return g_server_state.last_error->return_status;
}

void mcd_exit_f(void)
{
    g_server_state.last_error = &MCD_ERROR_NONE;
    return;
}

mcd_return_et mcd_qry_servers_f(const char *host, bool running,
                                uint32_t start_index, uint32_t *num_servers,
                                mcd_server_info_st *server_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_open_server_f(const char *system_key,
                                const char *config_string,
                                mcd_server_st **server)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_close_server_f(const mcd_server_st *server)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_set_server_config_f(const mcd_server_st *server,
                                      const char *config_string)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_server_config_f(const mcd_server_st *server,
                                      uint32_t *max_len,
                                      char *config_string)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_systems_f(uint32_t start_index, uint32_t *num_systems,
                                mcd_core_con_info_st *system_con_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_devices_f(const mcd_core_con_info_st *system_con_info,
                                uint32_t start_index, uint32_t *num_devices,
                                mcd_core_con_info_st *device_con_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_cores_f(const mcd_core_con_info_st *connection_info,
                              uint32_t start_index, uint32_t *num_cores,
                              mcd_core_con_info_st *core_con_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_core_modes_f(const mcd_core_st *core,
                                   uint32_t start_index, uint32_t *num_modes,
                                   mcd_core_mode_info_st *core_mode_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_open_core_f(const mcd_core_con_info_st *core_con_info,
                              mcd_core_st **core)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_close_core_f(const mcd_core_st *core)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

void mcd_qry_error_info_f(const mcd_core_st *core,
                          mcd_error_info_st *error_info)
{
    if (error_info) {
        *error_info = *g_server_state.last_error;
    }
}

mcd_return_et mcd_qry_device_description_f(const mcd_core_st *core, char *url,
                                           uint32_t *url_length)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_max_payload_size_f(const mcd_core_st *core,
                                         uint32_t *max_payload)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_input_handle_f(const mcd_core_st *core,
                                     uint32_t *input_handle)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_mem_spaces_f(const mcd_core_st *core,
                                   uint32_t start_index,
                                   uint32_t *num_mem_spaces,
                                   mcd_memspace_st *mem_spaces)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_mem_blocks_f(const mcd_core_st *core,
                                   uint32_t mem_space_id, uint32_t start_index,
                                   uint32_t *num_mem_blocks,
                                   mcd_memblock_st *mem_blocks)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_active_overlays_f(const mcd_core_st *core,
                                        uint32_t start_index,
                                        uint32_t *num_active_overlays,
                                        uint32_t *active_overlays)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_reg_groups_f(const mcd_core_st *core,
                                   uint32_t start_index,
                                   uint32_t *num_reg_groups,
                                   mcd_register_group_st *reg_groups)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_reg_map_f(const mcd_core_st *core, uint32_t reg_group_id,
                                uint32_t start_index, uint32_t *num_regs,
                                mcd_register_info_st *reg_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_reg_compound_f(const mcd_core_st *core,
                                     uint32_t compound_reg_id,
                                     uint32_t start_index,
                                     uint32_t *num_reg_ids,
                                     uint32_t *reg_id_array)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_trig_info_f(const mcd_core_st *core,
                                  mcd_trig_info_st *trig_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_ctrigs_f(const mcd_core_st *core, uint32_t start_index,
                               uint32_t *num_ctrigs,
                               mcd_ctrig_info_st *ctrig_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_create_trig_f(const mcd_core_st *core, void *trig,
                                uint32_t *trig_id)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_trig_f(const mcd_core_st *core, uint32_t trig_id,
                             uint32_t max_trig_size, void *trig)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_remove_trig_f(const mcd_core_st *core, uint32_t trig_id)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_trig_state_f(const mcd_core_st *core, uint32_t trig_id,
                                   mcd_trig_state_st *trig_state)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_activate_trig_set_f(const mcd_core_st *core)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_remove_trig_set_f(const mcd_core_st *core)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_trig_set_f(const mcd_core_st *core, uint32_t start_index,
                                 uint32_t *num_trigs, uint32_t *trig_ids)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_trig_set_state_f(const mcd_core_st *core,
                                       mcd_trig_set_state_st *trig_state)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_execute_txlist_f(const mcd_core_st *core,
                                   mcd_txlist_st *txlist)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_run_f(const mcd_core_st *core, bool global)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_stop_f(const mcd_core_st *core, bool global)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_run_until_f(const mcd_core_st *core, bool global,
                              bool absolute_time, uint64_t run_until_time)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_current_time_f(const mcd_core_st *core,
                                     uint64_t *current_time)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_step_f(const mcd_core_st *core, bool global,
                         mcd_core_step_type_et step_type, uint32_t n_steps)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_set_global_f(const mcd_core_st *core, bool enable)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_state_f(const mcd_core_st *core, mcd_core_state_st *state)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_execute_command_f(const mcd_core_st *core,
                                    const char *command_string,
                                    uint32_t result_string_size,
                                    char *result_string)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_rst_classes_f(const mcd_core_st *core,
                                    uint32_t *rst_class_vector)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_rst_class_info_f(const mcd_core_st *core,
                                       uint8_t rst_class,
                                       mcd_rst_info_st *rst_info)
{

    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_rst_f(const mcd_core_st *core, uint32_t rst_class_vector,
                        bool rst_and_halt)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_chl_open_f(const mcd_core_st *core, mcd_chl_st *channel)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_send_msg_f(const mcd_core_st *core, const mcd_chl_st *channel,
                             uint32_t msg_len, const uint8_t *msg)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_receive_msg_f(const mcd_core_st *core,
                                const mcd_chl_st *channel, uint32_t timeout,
                                uint32_t *msg_len, uint8_t *msg)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_chl_reset_f(const mcd_core_st *core,
                              const mcd_chl_st *channel)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_chl_close_f(const mcd_core_st *core,
                              const mcd_chl_st *channel)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_traces_f(const mcd_core_st *core, uint32_t start_index,
                               uint32_t *num_traces,
                               mcd_trace_info_st *trace_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_trace_state_f(const mcd_core_st *core, uint32_t trace_id,
                                    mcd_trace_state_st *state)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_set_trace_state_f(const mcd_core_st *core, uint32_t trace_id,
                                    mcd_trace_state_st *state)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_read_trace_f(const mcd_core_st *core, uint32_t trace_id,
                               uint64_t start_index, uint32_t *num_frames,
                               uint32_t trace_data_size, void *trace_data)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}
