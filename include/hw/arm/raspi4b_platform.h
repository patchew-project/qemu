/*
 * Raspberry Pi 4 B platform common definitions.
 *
 * Copyright (C) 2022  Auriga LLC, mrm <cmpl.error@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_RASPI4_PLATFORM_H
#define HW_ARM_RASPI4_PLATFORM_H

#define BCM2838_MPHI_OFFSET     0xb200
#define BCM2838_MPHI_SIZE       0x200

#define CLOCK_ISP_OFFSET        0xc11000
#define CLOCK_ISP_SIZE          0x100

#define PCIE_RC_OFFSET          0x1500000
#define PCIE_MMIO_OFFSET        0xc0000000
#define PCIE_MMIO_ARM_OFFSET    0x600000000
#define PCIE_MMIO_SIZE          0x40000000

/* SPI */
#define RPI4_INTERRUPT_MBOX         33
#define RPI4_INTERRUPT_MPHI         40
#define RPI4_INTERRUPT_DWC2         73
#define RPI4_INTERRUPT_DMA_0        80
#define RPI4_INTERRUPT_DMA_6        86
#define RPI4_INTERRUPT_DMA_7_8      87
#define RPI4_INTERRUPT_DMA_9_10     88
#define RPI4_INTERRUPT_AUX_UART1    93
#define RPI4_INTERRUPT_SDHOST       120
#define RPI4_INTERRUPT_UART0        121
#define RPI4_INTERRUPT_RNG200       125
#define RPI4_INTERRUPT_EMMC_EMMC2   126
#define RPI4_INTERRUPT_PCI_INT_A    143

/* GPU (legacy) DMA interrupts */
#define RPI4_GPU_INTERRUPT_DMA0      16
#define RPI4_GPU_INTERRUPT_DMA1      17
#define RPI4_GPU_INTERRUPT_DMA2      18
#define RPI4_GPU_INTERRUPT_DMA3      19
#define RPI4_GPU_INTERRUPT_DMA4      20
#define RPI4_GPU_INTERRUPT_DMA5      21
#define RPI4_GPU_INTERRUPT_DMA6      22
#define RPI4_GPU_INTERRUPT_DMA7_8    23
#define RPI4_GPU_INTERRUPT_DMA9_10   24
#define RPI4_GPU_INTERRUPT_DMA11     25
#define RPI4_GPU_INTERRUPT_DMA12     26
#define RPI4_GPU_INTERRUPT_DMA13     27
#define RPI4_GPU_INTERRUPT_DMA14     28
#define RPI4_GPU_INTERRUPT_DMA15     31

#endif
