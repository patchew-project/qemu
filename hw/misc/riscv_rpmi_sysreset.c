/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * RISC-V RPMI System Reset service.
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 * Author:
 *  Subrahmanya Lingappa <subrahmanya.lingappa@oss.qualcomm.com>
 */

#include "qemu/osdep.h"
#include "riscv_rpmi_internal.h"
#include "qemu/log.h"
#include "librpmi_env.h"

typedef struct RiscvRpmiSysresetType {
    uint32_t type;
    void (*action)(RiscvRpmiState *s);
} RiscvRpmiSysresetType;

typedef struct RiscvRpmiSysresetGroup {
    struct rpmi_service_group group;
    struct rpmi_service services[RPMI_SYSRST_SRV_ID_MAX];
    RiscvRpmiState *rpmi;
} RiscvRpmiSysresetGroup;

static void riscv_rpmi_sysreset_reboot(RiscvRpmiState *s)
{
    const RiscvRpmiMachineOps *ops = s->machine_ops;

    if (ops && ops->system_reset) {
        ops->system_reset();
    }
}

static void riscv_rpmi_sysreset_shutdown(RiscvRpmiState *s)
{
    const RiscvRpmiMachineOps *ops = s->machine_ops;

    if (ops && ops->system_shutdown) {
        ops->system_shutdown();
    }
}

static const RiscvRpmiSysresetType riscv_rpmi_sysreset_types[] = {
    {
        .type = RPMI_SYSRST_TYPE_SHUTDOWN,
        .action = riscv_rpmi_sysreset_shutdown,
    }, {
        .type = RPMI_SYSRST_TYPE_COLD_REBOOT,
        .action = riscv_rpmi_sysreset_reboot,
    },
};

static const RiscvRpmiSysresetType *riscv_rpmi_sysreset_type_by_id(
    uint32_t reset_type)
{
    for (uint32_t index = 0; index < ARRAY_SIZE(riscv_rpmi_sysreset_types);
         index++) {
        if (riscv_rpmi_sysreset_types[index].type == reset_type) {
            return &riscv_rpmi_sysreset_types[index];
        }
    }

    return NULL;
}

static void riscv_rpmi_do_system_reset(RiscvRpmiState *s,
                                       rpmi_uint32_t reset_type)
{
    const RiscvRpmiSysresetType *type;

    type = riscv_rpmi_sysreset_type_by_id(reset_type);
    if (type) {
        type->action(s);
        return;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported reset type %u\n",
                  __func__, reset_type);
}

static enum rpmi_error riscv_rpmi_sysreset_get_attributes(
    struct rpmi_service_group *group, struct rpmi_service *service,
    struct rpmi_transport *trans, rpmi_uint16_t request_data_len,
    const rpmi_uint8_t *request_data, rpmi_uint16_t *response_data_len,
    rpmi_uint8_t *response_data)
{
    uint32_t reset_type = ldl_le_p(request_data);
    uint32_t *resp = (uint32_t *)response_data;

    *response_data_len = 2 * sizeof(*resp);
    stl_le_p(&resp[0], RPMI_SUCCESS);
    stl_le_p(&resp[1], riscv_rpmi_sysreset_type_by_id(reset_type) ?
             RPMI_SYSRST_ATTRS_FLAGS_RESETTYPE : 0);

    return RPMI_SUCCESS;
}

static enum rpmi_error riscv_rpmi_sysreset_do_reset(
    struct rpmi_service_group *group, struct rpmi_service *service,
    struct rpmi_transport *trans, rpmi_uint16_t request_data_len,
    const rpmi_uint8_t *request_data, rpmi_uint16_t *response_data_len,
    rpmi_uint8_t *response_data)
{
    RiscvRpmiSysresetGroup *sysreset = group->priv;
    uint32_t reset_type = ldl_le_p(request_data);
    uint32_t *resp = (uint32_t *)response_data;

    *response_data_len = sizeof(*resp);

    if (!riscv_rpmi_sysreset_type_by_id(reset_type)) {
        stl_le_p(resp, (uint32_t)RPMI_ERR_INVALID_PARAM);
        return RPMI_SUCCESS;
    }

    stl_le_p(resp, RPMI_SUCCESS);
    riscv_rpmi_do_system_reset(sysreset->rpmi, reset_type);
    return RPMI_SUCCESS;
}

static const struct rpmi_service riscv_rpmi_sysreset_services[] = {
    [RPMI_SYSRST_SRV_ENABLE_NOTIFICATION] = {
        .service_id = RPMI_SYSRST_SRV_ENABLE_NOTIFICATION,
        .min_a2p_request_datalen = 8,
    },
    [RPMI_SYSRST_SRV_GET_ATTRIBUTES] = {
        .service_id = RPMI_SYSRST_SRV_GET_ATTRIBUTES,
        .min_a2p_request_datalen = 4,
        .process_a2p_request = riscv_rpmi_sysreset_get_attributes,
    },
    [RPMI_SYSRST_SRV_SYSTEM_RESET] = {
        .service_id = RPMI_SYSRST_SRV_SYSTEM_RESET,
        .min_a2p_request_datalen = 4,
        .process_a2p_request = riscv_rpmi_sysreset_do_reset,
    },
};

static struct rpmi_service_group *riscv_rpmi_sysreset_create(RiscvRpmiState *s)
{
    RiscvRpmiSysresetGroup *sysreset;
    struct rpmi_service_group *group;

    sysreset = g_new0(RiscvRpmiSysresetGroup, 1);
    sysreset->rpmi = s;
    memcpy(sysreset->services, riscv_rpmi_sysreset_services,
           sizeof(riscv_rpmi_sysreset_services));

    group = &sysreset->group;
    group->name = "sysreset";
    group->servicegroup_id = RPMI_SRVGRP_SYSTEM_RESET;
    group->max_service_id = RPMI_SYSRST_SRV_ID_MAX;
    group->servicegroup_version =
        RPMI_BASE_VERSION(RPMI_SPEC_VERSION_MAJOR, RPMI_SPEC_VERSION_MINOR);
    group->privilege_level_bitmap = RPMI_PRIVILEGE_M_MODE_MASK;
    group->services = sysreset->services;
    group->lock = rpmi_env_alloc_lock();
    group->priv = sysreset;

    return group;
}

static void riscv_rpmi_sysreset_destroy(struct rpmi_service_group *group)
{
    if (!group) {
        return;
    }

    rpmi_env_free_lock(group->lock);
    g_free(group->priv);
}

bool riscv_rpmi_sysreset_add(RiscvRpmiState *s, Error **errp)
{
    struct rpmi_service_group *group;

    if (s->sysreset_group) {
        error_setg(errp, "duplicate RPMI sysreset service descriptor");
        return false;
    }

    group = riscv_rpmi_sysreset_create(s);
    if (!group) {
        error_setg(errp, "failed to create RPMI sysreset service group");
        return false;
    }

    if (!riscv_rpmi_context_add_group(s, group, "sysreset", errp)) {
        riscv_rpmi_sysreset_destroy(group);
        return false;
    }

    s->sysreset_group = group;
    return true;
}

void riscv_rpmi_sysreset_remove(RiscvRpmiState *s)
{
    if (!s->sysreset_group) {
        return;
    }

    riscv_rpmi_context_remove_group(s, s->sysreset_group);
    riscv_rpmi_sysreset_destroy(s->sysreset_group);
    s->sysreset_group = NULL;
}
