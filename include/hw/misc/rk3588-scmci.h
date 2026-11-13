/*
 * Rockchip RK3588 SCMI server
 *
 * This device implements the SCMI shared memory and a minimal SCMI
 * server for the RK3588 SMC transport.  The guest uses the standard
 * "arm,scmi-smc" binding, with the SMC function ID and shared memory
 * address documented by the upstream RK3588 device tree.
 *
 * Copyright (C) 2026 Process Mission
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_RK3588_SCMCI_H
#define HW_MISC_RK3588_SCMCI_H

#include "qom/object.h"

#define TYPE_RK3588_SCMCI "rk3588-scmci"
OBJECT_DECLARE_SIMPLE_TYPE(RK3588SCMIState, RK3588_SCMCI)

#define RK3588_SCMI_SMC_ID      0x82000010
#define RK3588_SCMI_SHMEM_SIZE  0x100

#endif /* HW_MISC_RK3588_SCMCI_H */
