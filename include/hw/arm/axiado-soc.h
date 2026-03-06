/*
 * Axiado SoC AX3000
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AXIADO_AX3000_H
#define AXIADO_AX3000_H

#include "cpu.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/char/cadence_uart.h"
#include "hw/sd/sdhci.h"
#include "hw/core/sysbus.h"
#include "qemu/units.h"

#define TYPE_AXIADO_SOC "ax3000"
OBJECT_DECLARE_TYPE(AxiadoSoCState, AxiadoSoCClass, AXIADO_SOC)

#define AX3000_RAM_START        0x3C000000
#define AX3000_RAM_SIZE_MAX     (16 * GiB)

#define AX3000_DRAM0_BASE       AX3000_RAM_START
#define AX3000_DRAM0_SIZE       (1088 * MiB)
#define AX3000_DRAM1_BASE       0x400000000
#define AX3000_DRAM1_SIZE       (2 * GiB)

#define AX3000_GIC_DIST_BASE    0x80300000
#define AX3000_GIC_DIST_SIZE    (64 * KiB)
#define AX3000_GIC_REDIST_BASE  0x80380000
#define AX3000_GIC_REDIST_SIZE  (512 * KiB)

#define AX3000_UART0_BASE       0x80520000
#define AX3000_UART0_SIZE       (256)
#define AX3000_UART1_BASE       0x805a0000
#define AX3000_UART1_SIZE       (256)
#define AX3000_UART2_BASE       0x80620000
#define AX3000_UART2_SIZE       (256)
#define AX3000_UART3_BASE       0x80520800
#define AX3000_UART3_SIZE       (256)

#define AX3000_SDHCI0_BASE      0x86000000
#define AX3000_SDHCI0_PHY_BASE  0x80801C00
#define AX3000_SDHCI0_PHY_SIZE  (256)

#define AX3000_GPIO0_BASE       0x80500000
#define AX3000_GPIO0_SIZE       (1024)
#define AX3000_GPIO1_BASE       0x80580000
#define AX3000_GPIO1_SIZE       (1024)
#define AX3000_GPIO2_BASE       0x80600000
#define AX3000_GPIO2_SIZE       (1024)
#define AX3000_GPIO3_BASE       0x80680000
#define AX3000_GPIO3_SIZE       (1024)
#define AX3000_GPIO4_BASE       0x80700000
#define AX3000_GPIO4_SIZE       (1024)
#define AX3000_GPIO5_BASE       0x80780000
#define AX3000_GPIO5_SIZE       (1024)
#define AX3000_GPIO6_BASE       0x80800000
#define AX3000_GPIO6_SIZE       (1024)
#define AX3000_GPIO7_BASE       0x80880000
#define AX3000_GPIO7_SIZE       (1024)

#define AX3000_TIMER_CTRL        0x8A020000
#define AX3000_PLL_BASE          0x80000000
#define CLKRST_CPU_PLL_POSTDIV_OFFSET   0x0C
#define CLKRST_CPU_PLL_STS_OFFSET       0x14


enum Ax3000Configuration {
    AX3000_NUM_CPUS     = 4,
    AX3000_NUM_IRQS     = 224,
    AX3000_NUM_BANKS    = 2,
    AX3000_NUM_UARTS    = 4,
    AX3000_NUM_GPIOS    = 8,
};

typedef struct AxiadoSoCState {
    SysBusDevice        parent;

    ARMCPU              cpu[AX3000_NUM_CPUS];
    GICv3State          gic;
    MemoryRegion        dram[AX3000_NUM_BANKS];
    MemoryRegion        timer_ctrl;
    MemoryRegion        pll_ctrl;
    CadenceUARTState    uart[AX3000_NUM_UARTS];
    SDHCIState          sdhci0;
    MemoryRegion        sdhci_phy;
} AxiadoSoCState;

typedef struct AxiadoSoCClass {
    SysBusDeviceClass   parent;

    uint32_t            num_cpus;
} AxiadoSoCClass;

enum Ax3000MemoryRegions {
    AX3000_GIC_DIST,
    AX3000_GIC_REDIST,
    AX3000_DRAM0,
    AX3000_DRAM1,
    AX3000_UART0,
    AX3000_UART1,
    AX3000_UART2,
    AX3000_UART3,
    AX3000_SDHCI0,
    AX3000_GPIO0,
    AX3000_GPIO1,
    AX3000_GPIO2,
    AX3000_GPIO3,
    AX3000_GPIO4,
    AX3000_GPIO5,
    AX3000_GPIO6,
    AX3000_GPIO7,
};

enum Ax3000Irqs {
    AX3000_UART0_IRQ    = 112,
    AX3000_UART1_IRQ    = 113,
    AX3000_UART2_IRQ    = 114,
    AX3000_UART3_IRQ    = 170,

    AX3000_SDHCI0_IRQ    = 123,

    AX3000_GPIO0_IRQ    = 183,
    AX3000_GPIO1_IRQ    = 184,
    AX3000_GPIO2_IRQ    = 185,
    AX3000_GPIO3_IRQ    = 186,
    AX3000_GPIO4_IRQ    = 187,
    AX3000_GPIO5_IRQ    = 188,
    AX3000_GPIO6_IRQ    = 189,
    AX3000_GPIO7_IRQ    = 190,
};

enum SDHCI_PHY_REG {
    REG_ID        = 0x00,   // ID
    REG_STATUS    = 0x50,   // READY/LOCK/CAL_DONE bits
};

#endif /* AXIADO_AX3000_H */
