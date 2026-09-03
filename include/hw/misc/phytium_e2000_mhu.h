/*
 * Phytium E2000 MHU/SCMI doorbell
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_PHYTIUM_E2000_MHU_H
#define HW_MISC_PHYTIUM_E2000_MHU_H

#include "hw/core/cpu.h"
#include "hw/core/sysbus.h"
#include "hw/misc/phytium_e2000_pbr.h"
#include "qom/object.h"

#define TYPE_PHYTIUM_E2000_MHU "phytium-e2000-mhu"
OBJECT_DECLARE_SIMPLE_TYPE(PhytiumE2000MHUState, PHYTIUM_E2000_MHU)

#define PHYTIUM_E2000_MHU_MMIO_SIZE 0x1000
#define PHYTIUM_E2000_MHU_MAX_CPUS  4

void phytium_e2000_mhu_seed_mailbox(void);
void phytium_e2000_mhu_connect_cpu(PhytiumE2000MHUState *s,
                                   unsigned int index, uint64_t mpidr,
                                   CPUState *cpu);
/* Configure the PBR-validated, firmware-owned secondary-vector slot */
void phytium_e2000_mhu_set_secondary_vector_slot(PhytiumE2000MHUState *s,
                                                 hwaddr slot);

#endif
