/*
 * QEMU fw_cfg helpers (X86 specific)
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_I386_FW_CFG_H
#define HW_I386_FW_CFG_H

#include "hw/boards.h"
#include "hw/nvram/fw_cfg.h"

#define FW_CFG_ACPI_TABLES      (FW_CFG_ARCH_LOCAL + 0)
#define FW_CFG_SMBIOS_ENTRIES   (FW_CFG_ARCH_LOCAL + 1)
#define FW_CFG_IRQ0_OVERRIDE    (FW_CFG_ARCH_LOCAL + 2)
#define FW_CFG_E820_TABLE       (FW_CFG_ARCH_LOCAL + 3)
#define FW_CFG_HPET             (FW_CFG_ARCH_LOCAL + 4)

/**
 * FWCfgX86Topology: expose the X86 CPU topology to guest firmware over fw-cfg.
 *
 * All fields have little-endian encoding.
 *
 * @dies:     Number of dies per package (aka socket). Set it to 1 unless the
 *            concrete MachineState subclass defines it differently.
 * @cores:    Corresponds to @CpuTopology.@cores.
 * @threads:  Corresponds to @CpuTopology.@threads.
 * @max_cpus: Corresponds to @CpuTopology.@max_cpus.
 *
 * Firmware can derive the package (aka socket) count with the following
 * formula:
 *
 *   DIV_ROUND_UP(@max_cpus, @dies * @cores * @threads)
 *
 * Firmware can derive APIC ID field widths and offsets per the standard
 * calculations in "include/hw/i386/topology.h".
 */
typedef struct FWCfgX86Topology {
  uint32_t dies;
  uint32_t cores;
  uint32_t threads;
  uint32_t max_cpus;
} QEMU_PACKED FWCfgX86Topology;

FWCfgState *fw_cfg_arch_create(MachineState *ms,
                               uint16_t boot_cpus,
                               uint16_t apic_id_limit,
                               unsigned smp_dies,
                               bool expose_topology);
void fw_cfg_build_smbios(MachineState *ms, FWCfgState *fw_cfg);
void fw_cfg_build_feature_control(MachineState *ms, FWCfgState *fw_cfg);

#endif
