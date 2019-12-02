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
#include "hw/sd/allwinner-h3-sdhost.h"
#include "hw/net/allwinner-h3-emac.h"
#include "target/arm/cpu.h"

#define AW_H3_SRAM_A1_BASE     (0x00000000)
#define AW_H3_SRAM_A2_BASE     (0x00044000)
#define AW_H3_SRAM_C_BASE      (0x00010000)
#define AW_H3_DE_BASE          (0x01000000)
#define AW_H3_SYSCON_BASE      (0x01c00000)
#define AW_H3_DMA_BASE         (0x01c02000)
#define AW_H3_LCD0_BASE        (0x01c0c000)
#define AW_H3_LCD1_BASE        (0x01c0d000)
#define AW_H3_SID_BASE         (0x01c14000)
#define AW_H3_CCU_BASE         (0x01c20000)
#define AW_H3_PIC_REG_BASE     (0x01c20400)
#define AW_H3_PIT_REG_BASE     (0x01c20c00)
#define AW_H3_AC_BASE          (0x01c22c00)
#define AW_H3_UART0_REG_BASE   (0x01c28000)
#define AW_H3_EMAC_BASE        (0x01c30000)
#define AW_H3_MMC0_BASE        (0x01c0f000)
#define AW_H3_EHCI0_BASE       (0x01c1a000)
#define AW_H3_OHCI0_BASE       (0x01c1a400)
#define AW_H3_EHCI1_BASE       (0x01c1b000)
#define AW_H3_OHCI1_BASE       (0x01c1b400)
#define AW_H3_EHCI2_BASE       (0x01c1c000)
#define AW_H3_OHCI2_BASE       (0x01c1c400)
#define AW_H3_EHCI3_BASE       (0x01c1d000)
#define AW_H3_OHCI3_BASE       (0x01c1d400)
#define AW_H3_GPU_BASE         (0x01c40000)
#define AW_H3_GIC_DIST_BASE    (0x01c81000)
#define AW_H3_GIC_CPU_BASE     (0x01c82000)
#define AW_H3_GIC_HYP_BASE     (0x01c84000)
#define AW_H3_GIC_VCPU_BASE    (0x01c86000)
#define AW_H3_HDMI_BASE        (0x01ee0000)
#define AW_H3_RTC_BASE         (0x01f00000)
#define AW_H3_CPUCFG_BASE      (0x01f01c00)
#define AW_H3_SDRAM_BASE       (0x40000000)

#define AW_H3_SRAM_A1_SIZE     (64 * KiB)
#define AW_H3_SRAM_A2_SIZE     (32 * KiB)
#define AW_H3_SRAM_C_SIZE      (44 * KiB)
#define AW_H3_DE_SIZE          (4 * MiB)
#define AW_H3_DMA_SIZE         (4 * KiB)
#define AW_H3_LCD0_SIZE        (4 * KiB)
#define AW_H3_LCD1_SIZE        (4 * KiB)
#define AW_H3_GPU_SIZE         (64 * KiB)
#define AW_H3_HDMI_SIZE        (128 * KiB)
#define AW_H3_RTC_SIZE         (1 * KiB)
#define AW_H3_AC_SIZE          (2 * KiB)

#define AW_H3_GIC_PPI_MAINT          (9)
#define AW_H3_GIC_PPI_ARM_HYPTIMER  (10)
#define AW_H3_GIC_PPI_ARM_VIRTTIMER (11)
#define AW_H3_GIC_PPI_ARM_SECTIMER  (13)
#define AW_H3_GIC_PPI_ARM_PHYSTIMER (14)

#define AW_H3_GIC_SPI_UART0         (0)
#define AW_H3_GIC_SPI_TIMER0        (18)
#define AW_H3_GIC_SPI_TIMER1        (19)
#define AW_H3_GIC_SPI_MMC0          (60)
#define AW_H3_GIC_SPI_MMC1          (61)
#define AW_H3_GIC_SPI_MMC2          (62)
#define AW_H3_GIC_SPI_EHCI0         (72)
#define AW_H3_GIC_SPI_OHCI0         (73)
#define AW_H3_GIC_SPI_EHCI1         (74)
#define AW_H3_GIC_SPI_OHCI1         (75)
#define AW_H3_GIC_SPI_EHCI2         (76)
#define AW_H3_GIC_SPI_OHCI2         (77)
#define AW_H3_GIC_SPI_EHCI3         (78)
#define AW_H3_GIC_SPI_OHCI3         (79)
#define AW_H3_GIC_SPI_EMAC          (82)

#define AW_H3_GIC_NUM_SPI           (128)
#define AW_H3_NUM_CPUS              (4)

#define TYPE_AW_H3 "allwinner-h3"
#define AW_H3(obj) OBJECT_CHECK(AwH3State, (obj), TYPE_AW_H3)

typedef struct AwH3State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    qemu_irq irq[AW_H3_GIC_NUM_SPI];
    AwA10PITState timer;
    AwH3ClockState ccu;
    AwH3CpuCfgState cpucfg;
    AwH3SysconState syscon;
    AwH3SidState sid;
    AwH3SDHostState mmc0;
    AwH3EmacState emac;
    GICState gic;
    MemoryRegion sram_a1;
    MemoryRegion sram_a2;
    MemoryRegion sram_c;
} AwH3State;

#endif
