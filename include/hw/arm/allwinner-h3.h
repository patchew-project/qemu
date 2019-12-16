/*
 * Allwinner H3 System on Chip emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_ALLWINNER_H3_H
#define HW_ARM_ALLWINNER_H3_H

#include "qemu/error-report.h"
#include "qemu/units.h"
#include "hw/char/serial.h"
#include "hw/arm/boot.h"
#include "hw/timer/allwinner-a10-pit.h"
#include "hw/intc/arm_gic.h"
#include "hw/misc/allwinner-h3-clk.h"
#include "hw/misc/allwinner-h3-cpucfg.h"
#include "hw/misc/allwinner-h3-syscon.h"
#include "hw/misc/allwinner-h3-sid.h"
#include "target/arm/cpu.h"

enum {
    AW_H3_SRAM_A1,
    AW_H3_SRAM_A2,
    AW_H3_SRAM_C,
    AW_H3_SYSCON,
    AW_H3_SID,
    AW_H3_CCU,
    AW_H3_PIT,
    AW_H3_UART0,
    AW_H3_UART1,
    AW_H3_UART2,
    AW_H3_UART3,
    AW_H3_EMAC,
    AW_H3_MMC0,
    AW_H3_EHCI0,
    AW_H3_OHCI0,
    AW_H3_EHCI1,
    AW_H3_OHCI1,
    AW_H3_EHCI2,
    AW_H3_OHCI2,
    AW_H3_EHCI3,
    AW_H3_OHCI3,
    AW_H3_GIC_DIST,
    AW_H3_GIC_CPU,
    AW_H3_GIC_HYP,
    AW_H3_GIC_VCPU,
    AW_H3_CPUCFG,
    AW_H3_SDRAM
};

#define AW_H3_NUM_CPUS      (4)

#define TYPE_AW_H3 "allwinner-h3"
#define AW_H3(obj) OBJECT_CHECK(AwH3State, (obj), TYPE_AW_H3)

typedef struct AwH3State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    ARMCPU cpus[AW_H3_NUM_CPUS];
    const hwaddr *memmap;
    AwA10PITState timer;
    AwH3ClockState ccu;
    AwH3CpuCfgState cpucfg;
    AwH3SysconState syscon;
    AwH3SidState sid;
    GICState gic;
    MemoryRegion sram_a1;
    MemoryRegion sram_a2;
    MemoryRegion sram_c;
} AwH3State;

#endif
