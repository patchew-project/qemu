/*
 * Axiado SoC AX3005
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AXIADO_AX3005_H
#define AXIADO_AX3005_H

#include "cpu.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/char/cadence_uart.h"
#include "hw/gpio/cadence_gpio.h"
#include "hw/sd/axiado_sdhci.h"
#include "hw/core/sysbus.h"
#include "qemu/units.h"

#define TYPE_AX3005_SOC "ax3005"
OBJECT_DECLARE_TYPE(Ax3005SoCState, Ax3005SoCClass, AX3005_SOC)

#define AX3005_DRAM_BASE        0x80000000
#define AX3005_DRAM_SIZE        (2 * GiB)

#define AX3005_GIC_DIST_BASE    0x40400000
#define AX3005_GIC_DIST_SIZE    (64 * KiB)
#define AX3005_GIC_REDIST_BASE  0x40500000
#define AX3005_GIC_REDIST_SIZE  (768 * KiB)

#define AX3005_UART0_BASE       0x33020000
#define AX3005_UART1_BASE       0x330A0000
#define AX3005_UART2_BASE       0x33120000
#define AX3005_UART3_BASE       0x33020800
#define AX3005_UART4_BASE       0x331A0400
#define AX3005_UART5_BASE       0x33381D00
#define AX3005_UART6_BASE       0x33381E00
#define AX3005_UART7_BASE       0x33381F00
#define AX3005_UART8_BASE       0x330C0000

#define AX3005_SDHCI0_BASE      0x46000000
#define AX3005_EMMC_PHY_BASE    0x33301C00

#define AX3005_GPIO0_BASE       0x33000000
#define AX3005_GPIO1_BASE       0x33080000
#define AX3005_GPIO2_BASE       0x33100000
#define AX3005_GPIO3_BASE       0x33180000
#define AX3005_GPIO4_BASE       0x33200000
#define AX3005_GPIO5_BASE       0x33280000
#define AX3005_GPIO6_BASE       0x33300000
#define AX3005_GPIO7_BASE       0x33380000

#define AX3005_TIMER_CTRL       0x48016000

enum Ax3005Configuration {
    AX3005_NUM_CPUS     = 4,
    AX3005_NUM_IRQS     = 224,
    AX3005_NUM_UARTS    = 9,
    AX3005_NUM_GPIOS    = 8,
};

typedef struct Ax3005SoCState {
    SysBusDevice        parent;

    ARMCPU              cpu[AX3005_NUM_CPUS];
    GICv3State          gic;
    MemoryRegion        dram;
    CadenceUARTState    uart[AX3005_NUM_UARTS];
    CadenceGPIOState    gpio[AX3005_NUM_GPIOS];
    AxiadoSDHCIState    sdhci0;
} Ax3005SoCState;

typedef struct Ax3005SoCClass {
    SysBusDeviceClass   parent;

    uint32_t            num_cpus;
} Ax3005SoCClass;

enum Ax3005Irqs {
    AX3005_UART0_IRQ    = 112,
    AX3005_UART1_IRQ    = 113,
    AX3005_UART2_IRQ    = 114,
    AX3005_UART3_IRQ    = 170,
    AX3005_UART4_IRQ    = 208,
    AX3005_UART5_IRQ    = 209,
    AX3005_UART6_IRQ    = 210,
    AX3005_UART7_IRQ    = 211,
    AX3005_UART8_IRQ    = 212,

    AX3005_SDHCI0_IRQ   = 123,

    AX3005_GPIO0_IRQ    = 183,
    AX3005_GPIO1_IRQ    = 184,
    AX3005_GPIO2_IRQ    = 185,
    AX3005_GPIO3_IRQ    = 186,
    AX3005_GPIO4_IRQ    = 187,
    AX3005_GPIO5_IRQ    = 188,
    AX3005_GPIO6_IRQ    = 189,
    AX3005_GPIO7_IRQ    = 190,
};

#endif /* AXIADO_AX3005_H */
