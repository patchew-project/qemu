/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * RISC-V RPMI definitions shared by RPMI transport and FDT helpers.
 *
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 * Author:
 *  Subrahmanya Lingappa <subrahmanya.lingappa@oss.qualcomm.com>
 */

#ifndef HW_MISC_RISCV_RPMI_H
#define HW_MISC_RISCV_RPMI_H

#include "exec/hwaddr.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/notify.h"

#ifdef CONFIG_LIBRPMI
#include "librpmi.h"
#else
enum rpmi_servicegroup_id {
    RPMI_SRVGRP_SYSTEM_RESET = 0x0003,
    RPMI_SRVGRP_SYSTEM_SUSPEND = 0x0004,
    RPMI_SRVGRP_HSM = 0x0005,
};
#endif

#define RPMI_QUEUE_SLOT_SIZE 64
#define RPMI_DBREG_SIZE      0x1000

#define RPMI_ALL_NUM_QUEUES 4
#define RPMI_A2P_NUM_QUEUES 2
#define RPMI_ALL_NUM_REGS   (RPMI_ALL_NUM_QUEUES + 1)
#define RPMI_A2P_NUM_REGS   (RPMI_A2P_NUM_QUEUES + 1)

#define VIRT_RPMI_A2P_REQ_SIZE (16 * RPMI_QUEUE_SLOT_SIZE)
#define VIRT_RPMI_P2A_REQ_SIZE 0

#define TYPE_RISCV_RPMI "riscv-rpmi"
OBJECT_DECLARE_SIMPLE_TYPE(RiscvRpmiState, RISCV_RPMI)

struct rpmi_context;
struct rpmi_service_group;
struct rpmi_shmem;
struct rpmi_transport;

typedef struct RiscvRpmiMachineOps {
    void (*system_reset)(void);
    void (*system_shutdown)(void);
} RiscvRpmiMachineOps;

typedef struct RiscvRpmiServiceConfig {
    const char *node_name;
    const char *compatible;
    enum rpmi_servicegroup_id service_group;
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

DeviceState *riscv_rpmi_create(const RiscvRpmiConfig *cfg, Error **errp);

#endif
