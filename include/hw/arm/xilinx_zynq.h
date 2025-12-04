/*
 * Xilinx Zynq Baseboard System emulation.
 *
 * Copyright (c) 2010 Xilinx.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.croshtwaite@petalogix.com)
 * Copyright (c) 2012 Petalogix Pty Ltd.
 * Written by Haibing Ma
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_ARM_ZYNQ_H
#define QEMU_ARM_ZYNQ_H

#include "target/arm/cpu-qom.h"
#include "hw/qdev-clock.h"

#define TYPE_ZYNQ_MACHINE MACHINE_TYPE_NAME("xilinx-zynq-a9")
OBJECT_DECLARE_SIMPLE_TYPE(ZynqMachineState, ZYNQ_MACHINE)

#define ZYNQ_MAX_CPUS 2

struct ZynqMachineState {
    MachineState parent;
    Clock *ps_clk;
    ARMCPU *cpu[ZYNQ_MAX_CPUS];
    uint8_t boot_mode;
};

#endif /* QEMU_ARM_ZYNQ_H */
