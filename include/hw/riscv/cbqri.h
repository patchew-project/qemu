/*
 * RISC-V Capacity and Bandwidth QoS Register Interface
 * URL: https://github.com/riscv-non-isa/riscv-cbqri/releases/tag/v1.0
 *
 * Copyright (c) 2023 BayLibre SAS
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_CBQRI_H
#define HW_RISCV_CBQRI_H

#include "qemu/typedefs.h"

#define RISCV_CBQRI_VERSION_MAJOR   1
#define RISCV_CBQRI_VERSION_MINOR   0

#define TYPE_RISCV_CBQRI_CC         "riscv.cbqri.capacity"
#define TYPE_RISCV_CBQRI_BC         "riscv.cbqri.bandwidth"

/* Capacity Controller hardware capabilities */
typedef struct RiscvCbqriCapacityCaps {
    uint16_t nb_mcids;
    uint16_t nb_rcids;

    uint16_t ncblks;

    bool supports_at_code:1;

    bool supports_alloc_op_config_limit:1;
    bool supports_alloc_op_read_limit:1;
    bool supports_alloc_op_flush_rcid:1;

    bool supports_mon_op_config_event:1;
    bool supports_mon_op_read_counter:1;

    bool supports_mon_evt_id_none:1;
    bool supports_mon_evt_id_occupancy:1;
} RiscvCbqriCapacityCaps;

/* Bandwidth Controller hardware capabilities */
typedef struct RiscvCbqriBandwidthCaps {
    uint16_t nb_mcids;
    uint16_t nb_rcids;

    uint16_t nbwblks;
    uint16_t mrbwb;

    bool supports_at_code:1;

    bool supports_alloc_op_config_limit:1;
    bool supports_alloc_op_read_limit:1;

    bool supports_mon_op_config_event:1;
    bool supports_mon_op_read_counter:1;

    bool supports_mon_evt_id_none:1;
    bool supports_mon_evt_id_rdwr_count:1;
    bool supports_mon_evt_id_rdonly_count:1;
    bool supports_mon_evt_id_wronly_count:1;
} RiscvCbqriBandwidthCaps;

/*
 * Set the property defaults in a capability struct. A board changes the
 * fields it needs and passes the struct to the matching create function.
 */
void riscv_cbqri_cc_caps_init(RiscvCbqriCapacityCaps *caps);
void riscv_cbqri_bc_caps_init(RiscvCbqriBandwidthCaps *caps);

DeviceState *riscv_cbqri_cc_create(hwaddr addr,
                                   const RiscvCbqriCapacityCaps *caps);
DeviceState *riscv_cbqri_bc_create(hwaddr addr,
                                   const RiscvCbqriBandwidthCaps *caps);
#endif
