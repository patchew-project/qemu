/*
 * Phytium E2000 PBR (Phytium Boot ROM) model
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_PHYTIUM_E2000_PBR_H
#define HW_MISC_PHYTIUM_E2000_PBR_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_PHYTIUM_E2000_PBR "phytium-e2000-pbr"
OBJECT_DECLARE_SIMPLE_TYPE(PhytiumE2000PBRState, PHYTIUM_E2000_PBR)

/*
 * These are E2000 physical addresses, not locations chosen by a particular
 * PBF build. Keep them with the PBR device so that the machine memory map and
 * the firmware parser do not grow independent copies of the same constants.
 *
 * The boot SRAM contains the handoff left by the on-chip PBR. IACC is the
 * vendor-named boot-time execution window into which PBR places fip-all.bin.
 * The status window is consumed by PBF as a snapshot of PBR-owned state.
 */
#define PHYTIUM_E2000_PBR_STATUS_BASE       0x32a11804
#define PHYTIUM_E2000_PBR_BOOT_SRAM_BASE    0x30c00000
#define PHYTIUM_E2000_PBR_IACC_BASE         0x38000000

#define PHYTIUM_E2000_PBR_MMIO_SIZE         0x64
#define PHYTIUM_E2000_PBR_BOOT_SRAM_SIZE    0x00100000
#define PHYTIUM_E2000_PBR_IACC_SIZE         0x08000000
#define PHYTIUM_E2000_PBR_MAX_CPUS          4

#define PHYTIUM_E2000_PBR_BOOT_MEDIA_QSPI   0x1
#define PHYTIUM_E2000_PBR_BOOT_MEDIA_SD0    0x4

#define PHYTIUM_E2000_PBR_BOOT_MODE_QSPI    "qspi"
#define PHYTIUM_E2000_PBR_BOOT_MODE_SD0     "sd"

void phytium_e2000_pbr_configure(PhytiumE2000PBRState *s,
                                 BlockBackend *boot_blk,
                                 hwaddr ram_base, uint64_t ram_size,
                                 const uint64_t *cpu_mpidrs,
                                 unsigned int num_cpus);
bool phytium_e2000_pbr_firmware_loaded(PhytiumE2000PBRState *s);
int phytium_e2000_pbr_primary_cpu(PhytiumE2000PBRState *s);
void phytium_e2000_pbr_connect_cpu(PhytiumE2000PBRState *s,
                                   unsigned int index, CPUState *cpu);

#endif
