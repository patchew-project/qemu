/*
 * QEMU Cavium Octeon board model internal declarations.
 *
 * Copyright (c) 2026 Kirill A. Korinsky
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MIPS_OCTEON_INTERNAL_H
#define HW_MIPS_OCTEON_INTERNAL_H

#include "qemu/typedefs.h"
#include "system/memory.h"
#include "target/mips/cpu.h"

#define OCTEON_MAX_CPUS             16
typedef struct OcteonState OcteonState;

typedef struct OcteonCPUState {
    OcteonState *board;
    MIPSCPU *cpu;
    bool boot_cpu;
} OcteonCPUState;

struct OcteonState {
    MachineState *machine;
    Clock *cpuclk;
    uint64_t cpu_hz;
    uint64_t ref_hz;
    uint64_t io_hz;
    uint64_t ddr_hz;
    OcteonCPUState cpu[OCTEON_MAX_CPUS];
    unsigned int cpu_count;
    uint64_t firmware_entry;

    MemoryRegion dr0;
    MemoryRegion dr1;
    MemoryRegion *flash;
    MemoryRegion boot_flash;
    MemoryRegion boot_flash_alias;
    MemoryRegion cvmseg;
};

#endif
