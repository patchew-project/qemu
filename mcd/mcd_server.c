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
#include "qemu/cutils.h"
#include "mcd_api.h"
#include "hw/boards.h"
#include "exec/tswap.h"
#include "exec/gdbstub.h"
#include "hw/core/cpu.h"
#include "system/runstate.h"
#include "system/hw_accel.h"

/* Custom memory space type */
static const mcd_mem_type_et MCD_MEM_SPACE_IS_SECURE = 0x00010000;

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

static const mcd_error_info_st MCD_ERROR_SERVER_NOT_OPEN = {
    .return_status = MCD_RET_ACT_HANDLE_ERROR,
    .error_code = MCD_ERR_CONNECTION,
    .error_events = MCD_ERR_EVT_NONE,
    .error_str = "server is not open",
};

static const mcd_error_info_st MCD_ERROR_UNKNOWN_CORE = {
    .return_status = MCD_RET_ACT_HANDLE_ERROR,
    .error_code = MCD_ERR_PARAM,
    .error_events = MCD_ERR_EVT_NONE,
    .error_str = "specified core is unknown to server",
};

static const mcd_error_info_st MCD_ERROR_NONE = {
    .return_status = MCD_RET_ACT_NONE,
    .error_code = MCD_ERR_NONE,
    .error_events = MCD_ERR_EVT_NONE,
    .error_str = "",
};

/**
 * struct mcdcore_state - State of a core.
 *
 * @last_error:      Error info of most recent executed core-related function.
 * @custom_error:    Reserves memory for custom MCD errors.
 * @info:            Core connection information.
 * @open_core:       Open core instance as allocated in mcd_open_core_f().
 * @cpu:             QEMU's internal CPU handle.
 * @memory_spaces:   Memory spaces as queried by mcd_qry_mem_spaces_f().
 * @register_groups: Register groups as queried by mcd_qry_reg_groups_f().
 * @registers:       Registers as queried by mcd_qry_reg_map_f().
 *
 * MCD is mainly being used on the core level:
 * After the initial query functions, a core connection is opened in
 * mcd_open_core_f(). The allocated mcd_core_st instance is then the basis
 * of subsequent operations.
 *
 * @cpu is the internal CPU handle through which core specific debug
 * functions are implemented.
 */
typedef struct mcdcore_state {
    const mcd_error_info_st *last_error;
    mcd_error_info_st custom_error;
    mcd_core_con_info_st info;
    mcd_core_st *open_core;
    CPUState *cpu;
    GArray *memory_spaces;
    GArray *register_groups;
    GArray *registers;
} mcdcore_state;

/**
 * struct mcdserver_state - State of the MCD server
 *
 * @last_error:   Error info of most recent executed function.
 * @custom_error: Reserves memory for custom MCD errors.
 * @open_server:  Open server instance as allocated in mcd_open_server_f().
 * @system_key:   System key as provided in mcd_open_server_f()
 * @cores:        Internal core information database.
 */
typedef struct mcdserver_state {
    const mcd_error_info_st *last_error;
    mcd_error_info_st custom_error;
    mcd_server_st *open_server;
    char system_key[MCD_KEY_LEN];
    GArray *cores;
} mcdserver_state;

static mcdserver_state g_server_state = {
    .last_error = &MCD_ERROR_NONE,
    .open_server = NULL,
    .system_key = "",
    .cores = NULL,
};

static mcdcore_state *find_core(const mcd_core_con_info_st *core_con_info)
{
    uint32_t core_id;
    mcdcore_state *core;

    if (!core_con_info || !g_server_state.cores) {
        return NULL;
    }

    core_id = core_con_info->core_id;
    if (core_id > g_server_state.cores->len) {
        return NULL;
    }

    core = &g_array_index(g_server_state.cores, mcdcore_state, core_id);
    return core;
}

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
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_GENERAL,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "incompatible versions",
        };
        g_server_state.last_error = &g_server_state.custom_error;
    }

    return g_server_state.last_error->return_status;
}

void mcd_exit_f(void)
{
    if (g_server_state.open_server) {
        mcd_close_server_f(g_server_state.open_server);
    }

    return;
}

mcd_return_et mcd_qry_servers_f(const char *host, bool running,
                                uint32_t start_index, uint32_t *num_servers,
                                mcd_server_info_st *server_info)
{
    if (start_index >= 1) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_PARAM,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "QEMU only has one MCD server",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    if (!num_servers) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    if (!running) {
        /* MCD server is always running */
        *num_servers = 0;
        g_server_state.last_error = &MCD_ERROR_NONE;
        return g_server_state.last_error->return_status;
    }

    if (*num_servers == 0) {
        *num_servers = 1;
        g_server_state.last_error = &MCD_ERROR_NONE;
        return g_server_state.last_error->return_status;
    }

    /* num_servers != 0 => return server information */

    if (!server_info) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    *server_info = (mcd_server_info_st) {
        .server = "QEMU"
    };
    snprintf(server_info->system_instance, MCD_UNIQUE_NAME_LEN,
             "Process ID: %d", (int) getpid());

    *num_servers = 1;

    g_server_state.last_error = &MCD_ERROR_NONE;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_open_server_f(const char *system_key,
                                const char *config_string,
                                mcd_server_st **server)
{
    CPUState *cpu;

    if (g_server_state.open_server) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_CONNECTION,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "server already open",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    if (!server) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    g_server_state.open_server = g_malloc(sizeof(mcd_server_st));
    *g_server_state.open_server = (mcd_server_st) {
        .instance = NULL,
        .host = "QEMU",
        .config_string = "",
    };

    *server = g_server_state.open_server;

    if (system_key) {
        pstrcpy(g_server_state.system_key, MCD_KEY_LEN, system_key);
    }

    /* update the internal core information data base */
    g_server_state.cores = g_array_new(false, true, sizeof(mcdcore_state));
    CPU_FOREACH(cpu) {
        ObjectClass *oc = object_get_class(OBJECT(cpu));
        const char *cpu_model = object_class_get_name(oc);
        mcdcore_state c = {
            .info = (mcd_core_con_info_st) {
                .core_id = g_server_state.cores->len,
            },
            .last_error = &MCD_ERROR_NONE,
            .open_core = NULL,
            .cpu = cpu,
            .memory_spaces = g_array_new(false, true, sizeof(mcd_memspace_st)),
            .register_groups = g_array_new(false, true,
                                           sizeof(mcd_register_group_st)),
            .registers = g_array_new(false, true,
                                     sizeof(mcd_register_info_st)),
        };
        pstrcpy(c.info.core, MCD_UNIQUE_NAME_LEN, cpu_model);
        g_array_append_val(g_server_state.cores, c);
    }

    g_server_state.last_error = &MCD_ERROR_NONE;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_close_server_f(const mcd_server_st *server)
{
    if (!g_server_state.open_server) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_CONNECTION,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "server not open",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    if (server != g_server_state.open_server) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_CONNECTION,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "unknown server",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    for (int i = 0; i < g_server_state.cores->len; i++) {
        mcdcore_state *c = &g_array_index(g_server_state.cores,
                                          mcdcore_state, i);
        if (c->open_core) {
            mcd_close_core_f(c->open_core);
        }

        g_array_free(c->memory_spaces, true);
        g_array_free(c->register_groups, true);
        g_array_free(c->registers, true);
    }

    g_array_free(g_server_state.cores, true);
    g_free(g_server_state.open_server);
    g_server_state.open_server = NULL;
    g_server_state.system_key[0] = '\0';

    g_server_state.last_error = &MCD_ERROR_NONE;
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
    if (!num_systems) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    if (*num_systems == 0) {
        *num_systems = 1;
        g_server_state.last_error = &MCD_ERROR_NONE;
        return g_server_state.last_error->return_status;
    }

    if (start_index >= 1) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_PARAM,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "QEMU only emulates one system",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    /* num_systems != 0 => return system information */

    if (!system_con_info) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    *system_con_info = (mcd_core_con_info_st) {};
    pstrcpy(system_con_info->system, MCD_UNIQUE_NAME_LEN, g_get_prgname());
    pstrcpy(system_con_info->system_key, MCD_KEY_LEN,
            g_server_state.system_key);
    snprintf(system_con_info->system_instance, MCD_UNIQUE_NAME_LEN ,
             "Process ID: %d", (int) getpid());

    *num_systems = 1;

    g_server_state.last_error = &MCD_ERROR_NONE;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_devices_f(const mcd_core_con_info_st *system_con_info,
                                uint32_t start_index, uint32_t *num_devices,
                                mcd_core_con_info_st *device_con_info)
{
    MachineClass *mc = MACHINE_GET_CLASS(current_machine);

    if (!system_con_info || !num_devices) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    if (*num_devices == 0) {
        *num_devices = 1;
        g_server_state.last_error = &MCD_ERROR_NONE;
        return g_server_state.last_error->return_status;
    }

    if (start_index >= 1) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_PARAM,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "QEMU only emulates one machine",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    if (!device_con_info) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    *device_con_info = *system_con_info;
    pstrcpy(device_con_info->device, MCD_UNIQUE_NAME_LEN, mc->name);

    *num_devices = 1;

    g_server_state.last_error = &MCD_ERROR_NONE;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_cores_f(const mcd_core_con_info_st *connection_info,
                              uint32_t start_index, uint32_t *num_cores,
                              mcd_core_con_info_st *core_con_info)
{
    uint32_t i;

    if (!g_server_state.open_server) {
        g_server_state.last_error = &MCD_ERROR_SERVER_NOT_OPEN;
        return g_server_state.last_error->return_status;
    }

    if (!connection_info || !num_cores) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    /* array is allocated during core database update in mcd_server_open_f */
    g_assert(g_server_state.cores);

    if (*num_cores == 0) {
        *num_cores = g_server_state.cores->len;
        g_server_state.last_error = &MCD_ERROR_NONE;
        return g_server_state.last_error->return_status;
    }

    if (start_index >= g_server_state.cores->len) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_PARAM,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "start_index exceeds the number of cores",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    if (!core_con_info) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    for (i = 0;
         i < *num_cores && start_index + i < g_server_state.cores->len;
         i++) {

        mcdcore_state *c = &g_array_index(g_server_state.cores, mcdcore_state,
                                          start_index + i);
        core_con_info[i] = *connection_info;
        core_con_info[i].core_id = c->info.core_id;
        pstrcpy(core_con_info[i].core, MCD_UNIQUE_NAME_LEN, c->info.core);
    }

    *num_cores = i;

    g_server_state.last_error = &MCD_ERROR_NONE;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_core_modes_f(const mcd_core_st *core,
                                   uint32_t start_index, uint32_t *num_modes,
                                   mcd_core_mode_info_st *core_mode_info)
{
    g_server_state.last_error = &MCD_ERROR_NOT_IMPLEMENTED;
    return g_server_state.last_error->return_status;
}

static mcd_return_et query_memspaces(mcdcore_state *core_state)
{
    g_array_set_size(core_state->memory_spaces, 0);

    mcd_endian_et endian = target_big_endian() ? MCD_ENDIAN_BIG
                                               : MCD_ENDIAN_LITTLE;

    for (uint32_t i = 0; i < core_state->cpu->num_ases; i++) {
        AddressSpace *as = cpu_get_address_space(core_state->cpu, i);

        int secure_flag = 0;
        if (core_state->cpu->num_ases > 1) {
            int sid = cpu_asidx_from_attrs(core_state->cpu,
                                           (MemTxAttrs) { .secure = 1 });
            if (i == sid) {
                secure_flag = MCD_MEM_SPACE_IS_SECURE;
            }
        }

        const char *as_name = as->name;
        const char *mr_name = as->root->name;

        mcd_memspace_st physical = {
            /* mem space ID 0 is reserved */
            .mem_space_id = core_state->memory_spaces->len + 1,
            .mem_type = MCD_MEM_SPACE_IS_PHYSICAL | secure_flag,
            .endian = endian,
        };
        strncpy(physical.mem_space_name, mr_name, MCD_MEM_SPACE_NAME_LEN - 1);

        g_array_append_val(core_state->memory_spaces, physical);

        mcd_memspace_st logical = {
            .mem_space_id = core_state->memory_spaces->len + 1,
            .mem_type = MCD_MEM_SPACE_IS_LOGICAL | secure_flag,
            .endian = endian,
        };
        strncpy(logical.mem_space_name, as_name, MCD_MEM_SPACE_NAME_LEN - 1);

        g_array_append_val(core_state->memory_spaces, logical);
    }

    mcd_memspace_st gdb_registers = {
        .mem_space_id = core_state->memory_spaces->len + 1,
        .mem_space_name = "GDB Registers",
        .mem_type = MCD_MEM_SPACE_IS_REGISTERS,
        .endian = endian,
    };
    g_array_append_val(core_state->memory_spaces, gdb_registers);

    return MCD_RET_ACT_NONE;
}

static mcd_return_et query_registers(mcdcore_state *core_state)
{
    GArray *gdb_regs = core_state->cpu->gdb_regs;
    assert(gdb_regs);

    g_array_set_size(core_state->register_groups, 0);
    g_array_set_size(core_state->registers, 0);

    for (int feature_id = 0; feature_id < gdb_regs->len; feature_id++) {
        GDBRegisterState *f = &g_array_index(gdb_regs, GDBRegisterState,
                                             feature_id);
        /* register group ID 0 is reserved */
        uint32_t group_id = feature_id + 1;
        uint32_t num_regs = 0;

        GByteArray *mem_buf = g_byte_array_new();
        for (int i = 0; i < f->feature->num_regs; i++) {
            const char *name = f->feature->regs[i];
            if (name) {
                int reg_id = f->base_reg + i;
                int bitsize = gdb_read_register(core_state->cpu,
                                                mem_buf, reg_id) * 8;
                mcd_register_info_st r = {
                    .addr = {
                        .address = (uint64_t) reg_id,
                        /* memory space "GDB Registers" */
                        .mem_space_id = core_state->memory_spaces->len,
                        .addr_space_type = MCD_NOTUSED_ID,
                    },
                    .reg_group_id = group_id,
                    .regsize = (uint32_t) bitsize,
                    .reg_type = MCD_REG_TYPE_SIMPLE,
                    /* ID 0 reserved */
                    .hw_thread_id = core_state->info.core_id + 1,
                };
                strncpy(r.regname, name, MCD_REG_NAME_LEN - 1);
                g_array_append_val(core_state->registers, r);
                num_regs++;
            }
        }
        g_byte_array_free(mem_buf, true);

        mcd_register_group_st rg = {
            .reg_group_id = group_id,
            .n_registers = num_regs,
        };
        strncpy(rg.reg_group_name, f->feature->name, MCD_REG_NAME_LEN - 1);
        g_array_append_val(core_state->register_groups, rg);
    }

    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_open_core_f(const mcd_core_con_info_st *core_con_info,
                              mcd_core_st **core)
{
    uint32_t core_id;
    mcdcore_state *core_state;
    mcd_core_con_info_st *info;

    if (!g_server_state.open_server) {
        g_server_state.last_error = &MCD_ERROR_SERVER_NOT_OPEN;
        return g_server_state.last_error->return_status;
    }

    if (!core_con_info || !core) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    core_id = core_con_info->core_id;
    if (core_id > g_server_state.cores->len) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_PARAM,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "specified core index exceeds the number of cores",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    core_state = &g_array_index(g_server_state.cores, mcdcore_state, core_id);
    if (core_state->open_core) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_CONNECTION,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "core already open",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    if (query_memspaces(core_state) != MCD_RET_ACT_NONE) {
        return g_server_state.last_error->return_status;
    }

    if (query_registers(core_state) != MCD_RET_ACT_NONE) {
        return g_server_state.last_error->return_status;
    }

    *core = g_malloc(sizeof(mcd_core_st));
    info = g_malloc(sizeof(mcd_core_con_info_st));
    *info = *core_con_info;
    (*core)->core_con_info = info;
    (*core)->instance = NULL;
    core_state->open_core = *core;
    core_state->last_error = &MCD_ERROR_NONE;

    g_server_state.last_error = &MCD_ERROR_NONE;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_close_core_f(const mcd_core_st *core)
{
    mcdcore_state *core_state;

    if (!core) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    core_state = find_core(core->core_con_info);
    if (!core_state) {
        g_server_state.last_error = &MCD_ERROR_UNKNOWN_CORE;
        return g_server_state.last_error->return_status;
    }

    if (core_state->open_core != core) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_CONNECTION,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "core not open",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    g_free((void *)core->core_con_info);
    g_free((void *)core);
    core_state->open_core = NULL;
    core_state->cpu = NULL;
    g_array_set_size(core_state->memory_spaces, 0);
    g_array_set_size(core_state->register_groups, 0);
    g_array_set_size(core_state->registers, 0);

    g_server_state.last_error = &MCD_ERROR_NONE;
    return g_server_state.last_error->return_status;
}

void mcd_qry_error_info_f(const mcd_core_st *core,
                          mcd_error_info_st *error_info)
{
    mcdcore_state *core_state;

    if (!error_info) {
        return;
    }

    if (!core) {
        *error_info = *g_server_state.last_error;
        return;
    }

    core_state = find_core(core->core_con_info);
    if (!core_state)  {
        *error_info = MCD_ERROR_UNKNOWN_CORE;
    } else if (core_state->open_core != core) {
        *error_info = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_CONNECTION,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "core not open",
        };
    } else {
        *error_info = *core_state->last_error;
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
    uint32_t i;
    mcdcore_state *core_state;

    if (!core || !num_mem_spaces) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    core_state = find_core(core->core_con_info);
    if (!core_state || core_state->open_core != core) {
        g_server_state.last_error = &MCD_ERROR_UNKNOWN_CORE;
        return g_server_state.last_error->return_status;
    }

    g_assert(core_state->memory_spaces);

    if (core_state->memory_spaces->len == 0) {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_NO_MEM_SPACES,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    if (*num_mem_spaces == 0) {
        *num_mem_spaces = core_state->memory_spaces->len;
        core_state->last_error = &MCD_ERROR_NONE;
        return core_state->last_error->return_status;
    }

    if (start_index >= core_state->memory_spaces->len) {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_PARAM,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "start_index exceeds the number of memory spaces",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    if (!mem_spaces) {
        core_state->last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return core_state->last_error->return_status;
    }

    for (i = 0; i < *num_mem_spaces &&
         start_index + i < core_state->memory_spaces->len; i++) {

        mem_spaces[i] = g_array_index(core_state->memory_spaces,
                                      mcd_memspace_st, start_index + i);
    }

    *num_mem_spaces = i;

    core_state->last_error = &MCD_ERROR_NONE;
    return core_state->last_error->return_status;
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
    uint32_t i;
    mcdcore_state *core_state;

    if (!core || !num_reg_groups) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    core_state = find_core(core->core_con_info);
    if (!core_state || core_state->open_core != core) {
        g_server_state.last_error = &MCD_ERROR_UNKNOWN_CORE;
        return g_server_state.last_error->return_status;
    }

    g_assert(core_state->register_groups);

    if (core_state->register_groups->len == 0) {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_NO_REG_GROUPS,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    if (*num_reg_groups == 0) {
        *num_reg_groups = core_state->register_groups->len;
        core_state->last_error = &MCD_ERROR_NONE;
        return core_state->last_error->return_status;
    }

    if (start_index >= core_state->register_groups->len) {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_PARAM,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "start_index exceeds the number of register groups",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    if (!reg_groups) {
        core_state->last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return core_state->last_error->return_status;
    }

    for (i = 0; i < *num_reg_groups &&
         start_index + i < core_state->register_groups->len; i++) {

        reg_groups[i] = g_array_index(core_state->register_groups,
                                      mcd_register_group_st, start_index + i);
    }

    *num_reg_groups = i;

    core_state->last_error = &MCD_ERROR_NONE;
    return core_state->last_error->return_status;
}

mcd_return_et mcd_qry_reg_map_f(const mcd_core_st *core, uint32_t reg_group_id,
                                uint32_t start_index, uint32_t *num_regs,
                                mcd_register_info_st *reg_info)
{
    mcdcore_state *core_state;
    bool query_all_regs = reg_group_id == 0;
    bool query_num_only;

    if (!core || !num_regs) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    query_num_only = *num_regs == 0;

    core_state = find_core(core->core_con_info);
    if (!core_state || core_state->open_core != core) {
        g_server_state.last_error = &MCD_ERROR_UNKNOWN_CORE;
        return g_server_state.last_error->return_status;
    }

    if (core_state->register_groups->len == 0 ||
        reg_group_id > core_state->register_groups->len) {

        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_REG_GROUP_ID,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    /*
     * Depending on reg_group_id, start_index refers either to the total list of
     * register or a single register group.
     */

    if (query_all_regs) {
        if (start_index >= core_state->registers->len) {
            core_state->custom_error = (mcd_error_info_st) {
                .return_status = MCD_RET_ACT_HANDLE_ERROR,
                .error_code = MCD_ERR_PARAM,
                .error_events = MCD_ERR_EVT_NONE,
                .error_str = "start_index exceeds the number of registers",
            };
            core_state->last_error = &core_state->custom_error;
            return core_state->last_error->return_status;
        };

        if (*num_regs == 0) {
            *num_regs = core_state->registers->len;
        } else if (*num_regs > core_state->registers->len - start_index) {
            *num_regs = core_state->registers->len - start_index;
        }
    } else {
        mcd_register_group_st *rg = &g_array_index(core_state->register_groups,
            mcd_register_group_st, reg_group_id - 1);

        if (start_index > rg->n_registers) {
            core_state->custom_error = (mcd_error_info_st) {
                .return_status = MCD_RET_ACT_HANDLE_ERROR,
                .error_code = MCD_ERR_PARAM,
                .error_events = MCD_ERR_EVT_NONE,
                .error_str = "start_index exceeds the number of registers",
            };
            core_state->last_error = &core_state->custom_error;
            return core_state->last_error->return_status;
        }

        if (*num_regs == 0) {
            *num_regs = rg->n_registers;
        } else if (*num_regs > rg->n_registers - start_index) {
            *num_regs = rg->n_registers - start_index;
        }

        for (uint32_t rg_id = 0; rg_id < reg_group_id - 1; rg_id++) {
            mcd_register_group_st *prev_rg = &g_array_index(
                core_state->register_groups, mcd_register_group_st, rg_id);
            start_index += prev_rg->n_registers;
        }
    }

    if (!query_num_only) {
        for (uint32_t i = 0; i < *num_regs; i++) {
            reg_info[i] = g_array_index(
                core_state->registers, mcd_register_info_st, start_index + i);
        }
    }

    core_state->last_error = &MCD_ERROR_NONE;
    return core_state->last_error->return_status;
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

static mcd_return_et execute_memory_tx(mcdcore_state *core_state, mcd_tx_st *tx,
                                       mcd_mem_type_et type)
{
    MemTxResult result = MEMTX_ERROR;

    /* each address space has one physical and one virtual memory */
    int address_space_id = (tx->addr.mem_space_id - 1) / 2;
    AddressSpace *as = cpu_get_address_space(core_state->cpu, address_space_id);

    hwaddr addr = tx->addr.address;
    hwaddr len = tx->access_width > 0 ? tx->access_width : tx->num_bytes;
    bool is_write;

    if (tx->access_type == MCD_TX_AT_R) {
        is_write = false;
    } else if (tx->access_type == MCD_TX_AT_W) {
        is_write = true;
    } else {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_TXLIST_TX,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "tx access type not supported",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    if (type & MCD_MEM_SPACE_IS_PHYSICAL) {
        MemTxAttrs attrs = {
            .secure = !!(type & MCD_MEM_SPACE_IS_SECURE),
            .space = address_space_id,
        };

        for (tx->num_bytes_ok = 0;
             tx->num_bytes_ok < tx->num_bytes;
             tx->num_bytes_ok += len) {
            void *buf = tx->data + tx->num_bytes_ok;
            result = address_space_rw(as, addr + tx->num_bytes_ok, attrs,
                                      buf, len, is_write);
            if (result != MEMTX_OK) {
                break;
            }
        }
    } else if (type & MCD_MEM_SPACE_IS_LOGICAL) {
        for (tx->num_bytes_ok = 0;
             tx->num_bytes_ok < tx->num_bytes;
             tx->num_bytes_ok += len) {
            void *buf = tx->data + tx->num_bytes_ok;
            int ret = cpu_memory_rw_debug(core_state->cpu,
                                          addr + tx->num_bytes_ok,
                                          buf, len, is_write);
            result = (ret == 0) ? MEMTX_OK : MEMTX_ERROR;
            if (result != MEMTX_OK) {
                break;
            }
        }
    } else {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_TXLIST_TX,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "unknown mem space type",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    if (result != MEMTX_OK) {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = is_write ? MCD_ERR_TXLIST_WRITE : MCD_ERR_TXLIST_READ,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "",
        };
        snprintf(core_state->custom_error.error_str, MCD_INFO_STR_LEN,
                 "Memory tx failed with error code %d", result);
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    tx->num_bytes_ok = tx->num_bytes;
    core_state->last_error = &MCD_ERROR_NONE;
    return core_state->last_error->return_status;
}

static mcd_return_et execute_register_tx(mcdcore_state *core_state,
                                         mcd_tx_st *tx)
{
    if (tx->access_type == MCD_TX_AT_R) {
        GByteArray *mem_buf = g_byte_array_new();
        int read_bytes = gdb_read_register(core_state->cpu, mem_buf,
                                           tx->addr.address);
        if (read_bytes > tx->num_bytes) {
            g_byte_array_free(mem_buf, true);
            core_state->custom_error = (mcd_error_info_st) {
                .return_status = MCD_RET_ACT_HANDLE_ERROR,
                .error_code = MCD_ERR_TXLIST_READ,
                .error_events = MCD_ERR_EVT_NONE,
                .error_str = "too many bytes read",
            };
            core_state->last_error = &core_state->custom_error;
            return core_state->last_error->return_status;
        }
        memcpy(tx->data, mem_buf->data, read_bytes);
        g_byte_array_free(mem_buf, true);
        tx->num_bytes_ok = read_bytes;
    } else if (tx->access_type == MCD_TX_AT_W) {
        int written_bytes = gdb_write_register(core_state->cpu, tx->data,
                                               tx->addr.address);
        if (written_bytes > tx->num_bytes) {
            core_state->custom_error = (mcd_error_info_st) {
                .return_status = MCD_RET_ACT_HANDLE_ERROR,
                .error_code = MCD_ERR_TXLIST_READ,
                .error_events = MCD_ERR_EVT_NONE,
                .error_str = "too many bytes written",
            };
            core_state->last_error = &core_state->custom_error;
            return core_state->last_error->return_status;
        }
        tx->num_bytes_ok = written_bytes;
    } else {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_TXLIST_TX,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "tx access type not supported",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    core_state->last_error = &MCD_ERROR_NONE;
    return core_state->last_error->return_status;
}

static mcd_return_et execute_tx(mcdcore_state *core_state, mcd_tx_st *tx)
{
    mcd_memspace_st *ms;

    uint32_t ms_id = tx->addr.mem_space_id;
    if (ms_id == 0 || ms_id > core_state->memory_spaces->len) {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_PARAM,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "unknown memory space ID",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    if (tx->access_width > 0 && tx->num_bytes % tx->access_width != 0) {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_TXLIST_TX,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "alignment error",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    ms = &g_array_index(core_state->memory_spaces, mcd_memspace_st, ms_id - 1);
    if (ms->mem_type & MCD_MEM_SPACE_IS_PHYSICAL ||
        ms->mem_type & MCD_MEM_SPACE_IS_LOGICAL) {
        return execute_memory_tx(core_state, tx, ms->mem_type);
    } else if (ms->mem_type & MCD_MEM_SPACE_IS_REGISTERS) {
        return execute_register_tx(core_state, tx);
    } else {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_TXLIST_TX,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "unknown memory space type",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }
}

mcd_return_et mcd_execute_txlist_f(const mcd_core_st *core,
                                   mcd_txlist_st *txlist)
{
    mcdcore_state *core_state;

    if (!core || !txlist) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    core_state = find_core(core->core_con_info);
    if (!core_state || core_state->open_core != core) {
        g_server_state.last_error = &MCD_ERROR_UNKNOWN_CORE;
        return g_server_state.last_error->return_status;
    }

    for (uint32_t i = 0; i < txlist->num_tx; i++) {
        mcd_tx_st *tx = txlist->tx + i;
        if (execute_tx(core_state, tx) != MCD_RET_ACT_NONE) {
            return core_state->last_error->return_status;
        } else {
            txlist->num_tx_ok++;
        }
    }

    core_state->last_error = &MCD_ERROR_NONE;
    return core_state->last_error->return_status;
}

mcd_return_et mcd_run_f(const mcd_core_st *core, bool global)
{
    mcdcore_state *core_state;

    if (g_server_state.cores->len > 1 && global) {
        vm_start();
        g_server_state.last_error = &MCD_ERROR_NONE;
        return g_server_state.last_error->return_status;
    }

    if (!core) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    core_state = find_core(core->core_con_info);
    if (!core_state || core_state->open_core != core) {
        g_server_state.last_error = &MCD_ERROR_UNKNOWN_CORE;
        return g_server_state.last_error->return_status;
    }

    if (!runstate_needs_reset() && !runstate_is_running() &&
        !vm_prepare_start(false)) {
        cpu_resume(core_state->cpu);
        qemu_clock_enable(QEMU_CLOCK_VIRTUAL, true);
    }

    core_state->last_error = &MCD_ERROR_NONE;
    return core_state->last_error->return_status;
}

mcd_return_et mcd_stop_f(const mcd_core_st *core, bool global)
{
    if (g_server_state.cores->len > 1 && !global) {
        g_server_state.custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_FN_UNIMPLEMENTED,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "core-specific stop not implemented",
        };
        g_server_state.last_error = &g_server_state.custom_error;
        return g_server_state.last_error->return_status;
    }

    if (runstate_is_running()) {
        vm_stop(RUN_STATE_DEBUG);
    }

    g_server_state.last_error = &MCD_ERROR_NONE;
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
    mcdcore_state *core_state;

    if (!core) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    core_state = find_core(core->core_con_info);
    if (!core_state || core_state->open_core != core) {
        g_server_state.last_error = &MCD_ERROR_UNKNOWN_CORE;
        return g_server_state.last_error->return_status;
    }

    if (!enable) {
        core_state->custom_error = (mcd_error_info_st) {
            .return_status = MCD_RET_ACT_HANDLE_ERROR,
            .error_code = MCD_ERR_GENERAL,
            .error_events = MCD_ERR_EVT_NONE,
            .error_str = "global stop activities cannot be disabled",
        };
        core_state->last_error = &core_state->custom_error;
        return core_state->last_error->return_status;
    }

    g_server_state.last_error = &MCD_ERROR_NONE;
    return g_server_state.last_error->return_status;
}

mcd_return_et mcd_qry_state_f(const mcd_core_st *core, mcd_core_state_st *state)
{
    mcdcore_state *core_state;
    RunState rs;

    if (!core || !state) {
        g_server_state.last_error = &MCD_ERROR_INVALID_NULL_PARAM;
        return g_server_state.last_error->return_status;
    }

    *state = (mcd_core_state_st) {
        .stop_str = "",
        .info_str = "",
    };

    core_state = find_core(core->core_con_info);
    if (!core_state || core_state->open_core != core) {
        g_server_state.last_error = &MCD_ERROR_UNKNOWN_CORE;
        return g_server_state.last_error->return_status;
    }

    cpu_synchronize_state(core_state->cpu);
    rs = runstate_get();
    switch (rs) {
    case RUN_STATE_PRELAUNCH:
    case RUN_STATE_DEBUG:
    case RUN_STATE_PAUSED:
        state->state = MCD_CORE_STATE_DEBUG;
        pstrcpy(state->stop_str, MCD_INFO_STR_LEN, "RUN_STATE_PAUSED");
        break;
    case RUN_STATE_RUNNING:
        if (core_state->cpu->running) {
            state->state = MCD_CORE_STATE_RUNNING;
        } else if (core_state->cpu->stopped) {
            state->state = MCD_CORE_STATE_DEBUG;
        } else if (core_state->cpu->halted) {
            state->state = MCD_CORE_STATE_HALTED;
            pstrcpy(state->info_str, MCD_INFO_STR_LEN, "halted");
        } else {
            state->state = MCD_CORE_STATE_UNKNOWN;
        }
        break;
    default:
        state->state = MCD_CORE_STATE_UNKNOWN;
        break;
    }

    g_server_state.last_error = &MCD_ERROR_NONE;
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
