/*
 * NEORV32 SOC presentation in QEMU
 *
 * Copyright (c) 2025 Michael Levit
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NEORV32_H
#define HW_NEORV32_H

#include "hw/riscv/riscv_hart.h"
#include "hw/boards.h"

#if defined(TARGET_RISCV32)
#define NEORV32_CPU TYPE_RISCV_CPU_NEORV32
#endif

#define TYPE_RISCV_NEORV32_SOC "riscv.neorv32.soc"
#define RISCV_NEORV32_SOC(obj) \
    OBJECT_CHECK(Neorv32SoCState, (obj), TYPE_RISCV_NEORV32_SOC)

typedef struct Neorv32SoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    MemoryRegion imem_region;
    MemoryRegion bootloader_rom;
} Neorv32SoCState;

typedef struct Neorv32State {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    Neorv32SoCState soc;
} Neorv32State;

#define TYPE_NEORV32_MACHINE MACHINE_TYPE_NAME("neorv32")
#define NEORV32_MACHINE(obj) \
    OBJECT_CHECK(Neorv32State, (obj), TYPE_NEORV32_MACHINE)

enum {
    NEORV32_IMEM,
    NEORV32_BOOTLOADER_ROM,
    NEORV32_DMEM,
    NEORV32_SYSINFO,
    NEORV32_UART0,
    NEORV32_SPI0,
};

#endif /* HW_NEORV32_H */
