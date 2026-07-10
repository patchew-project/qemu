/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * RISC-V RPMI definitions shared by RPMI transport and FDT helpers.
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 * Author:
 *  Subrahmanya Lingappa <subrahmanya.lingappa@qti.qualcomm.com>
 */

#ifndef HW_MISC_RISCV_RPMI_H
#define HW_MISC_RISCV_RPMI_H

#include "exec/hwaddr.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/notify.h"

#define RPMI_QUEUE_SLOT_SIZE 64
#define RPMI_DBREG_SIZE      0x1000

#define RPMI_ALL_NUM_QUEUES 4
#define RPMI_A2P_NUM_QUEUES 2
#define RPMI_ALL_NUM_REGS   (RPMI_ALL_NUM_QUEUES + 1)
#define RPMI_A2P_NUM_REGS   (RPMI_A2P_NUM_QUEUES + 1)

#define VIRT_RPMI_A2P_REQ_SIZE (16 * RPMI_QUEUE_SLOT_SIZE)
#define VIRT_RPMI_P2A_REQ_SIZE 0

#define RISCV_RPMI_SRVGRP_SYSTEM_RESET   3

#define TYPE_RISCV_RPMI "riscv-rpmi"
OBJECT_DECLARE_SIMPLE_TYPE(RiscvRpmiState, RISCV_RPMI)

struct rpmi_context;
struct rpmi_service_group;
struct rpmi_shmem;
struct rpmi_transport;

typedef enum RiscvRpmiServiceKind {
    RISCV_RPMI_SERVICE_INVALID = 0,
    RISCV_RPMI_SERVICE_SYSRESET,
} RiscvRpmiServiceKind;

typedef struct RiscvRpmiMachineOps {
    void (*system_reset)(void *opaque);
    void (*system_shutdown)(void *opaque);
} RiscvRpmiMachineOps;
typedef struct RiscvRpmiServiceConfig {
    RiscvRpmiServiceKind kind;
    const char *node_name;
    const char *compatible;
    uint32_t service_group;
    bool has_mpxy_channel;
    uint32_t mpxy_channel;
} RiscvRpmiServiceConfig;

typedef struct RiscvRpmiConfig {
    hwaddr doorbell_base;
    hwaddr shmem_base;
    hwaddr shmem_size;
    uint32_t a2p_req_size;
    uint32_t p2a_req_size;
    const char *platform_info;
    const RiscvRpmiMachineOps *machine_ops;
    void *machine_opaque;
    const uint32_t *hart_ids;
    uint32_t hart_count;
    const RiscvRpmiServiceConfig *services;
    uint32_t service_count;
} RiscvRpmiConfig;

struct RiscvRpmiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion shmem;
    uint64_t shmem_base;
    uint64_t shmem_size;
    uint32_t a2p_req_size;
    uint32_t p2a_req_size;
    char *platform_info;
    const RiscvRpmiMachineOps *machine_ops;
    void *machine_opaque;
    struct rpmi_service_group *sysreset_group;
    uint32_t *hart_ids;
    uint32_t hart_count;
    const RiscvRpmiServiceConfig *services;
    uint32_t service_count;
    uint32_t doorbell;
    struct rpmi_shmem *rpmi_shmem;
    struct rpmi_transport *transport;
    struct rpmi_context *context;
    bool has_shmem;
};

#ifdef CONFIG_LIBRPMI
DeviceState *riscv_rpmi_create(const RiscvRpmiConfig *cfg, Error **errp);
#else
static inline DeviceState *riscv_rpmi_create(const RiscvRpmiConfig *cfg,
                                             Error **errp)
{
    (void)cfg;
    (void)errp;
    return NULL;
}
#endif

#endif
