/*
 * QEMU RISC-V Mini Computer machine interface
 *
 * Copyright (c) 2022 Rivos, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_MINIC_H
#define HW_RISCV_MINIC_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "hw/block/flash.h"
#include "qom/object.h"

#define MINIC_CPUS_MAX_BITS             9
#define MINIC_CPUS_MAX                  (1 << MINIC_CPUS_MAX_BITS)
#define MINIC_SOCKETS_MAX_BITS          2
#define MINIC_SOCKETS_MAX               (1 << MINIC_SOCKETS_MAX_BITS)

#define MINIC_IRQCHIP_IPI_MSI 1
#define MINIC_IRQCHIP_NUM_MSIS 255
#define MINIC_IRQCHIP_NUM_PRIO_BITS 3
#define MINIC_IRQCHIP_MAX_GUESTS_BITS 3
#define MINIC_IRQCHIP_MAX_GUESTS ((1U << MINIC_IRQCHIP_MAX_GUESTS_BITS) - 1U)

#define TYPE_RISCV_MINIC_MACHINE MACHINE_TYPE_NAME("minic")

typedef struct RISCVMinicState RISCVMinicState;
DECLARE_INSTANCE_CHECKER(RISCVMinicState, RISCV_MINIC_MACHINE,
                         TYPE_RISCV_MINIC_MACHINE)

struct RISCVMinicState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    RISCVHartArrayState soc[MINIC_SOCKETS_MAX];
    int fdt_size;
    int aia_guests;
};

enum {
    MINIC_MROM = 0,
    MINIC_CLINT,
    MINIC_IMSIC_M,
    MINIC_IMSIC_S,
    MINIC_DRAM,
    MINIC_PCIE_MMIO,
    MINIC_PCIE_PIO,
    MINIC_PCIE_ECAM
};

#endif
