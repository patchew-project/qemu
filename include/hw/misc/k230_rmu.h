/*
 * K230 Reset Management Unit (RMU / SYSCTL_RST)
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * Register semantics cross-checked against the Linux mainline driver
 * drivers/reset/reset-k230.c (compatible "canaan,k230-rst").
 *
 * Copyright (c) 2026 Jack Wang <163wangjack@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_K230_RMU_H
#define HW_MISC_K230_RMU_H

#include "qemu/bitops.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_K230_RMU "riscv.k230.rmu"
OBJECT_DECLARE_SIMPLE_TYPE(K230RmuState, K230_RMU)

/* 1 KiB MMIO window, see K230_DEV_RMU in hw/riscv/k230.c. */
#define K230_RMU_MMIO_SIZE   0x1000
#define K230_RMU_NUM_REGS    (K230_RMU_MMIO_SIZE / 4)

/* Control register offsets used by drivers/reset/reset-k230.c. */
#define K230_RMU_CPU0_CTRL     0x04
#define K230_RMU_CPU1_CTRL     0x0C
#define K230_RMU_AI_CTRL       0x14
#define K230_RMU_VPU_CTRL      0x1C
#define K230_RMU_PERI0_CTRL    0x20   /* SW_DONE bank: TIMERx, WDTx, MAILBOX   */
#define K230_RMU_PERI1_CTRL    0x24   /* SW_DONE bank: UARTx, I2Cx, GPIO       */
#define K230_RMU_HISYS_CTRL    0x2C
#define K230_RMU_SDIO_CTRL     0x34
#define K230_RMU_USB_CTRL      0x3C
#define K230_RMU_SPI_CTRL      0x44
#define K230_RMU_SEC_CTRL      0x4C
#define K230_RMU_DMA_CTRL      0x54
#define K230_RMU_DECOMP_CTRL   0x5C
#define K230_RMU_SRAM_CTRL     0x64   /* mixed: HW_DONE bits + SW_DONE bit1     */
#define K230_RMU_NONAI2D_CTRL  0x6C
#define K230_RMU_MCTL_CTRL     0x74
#define K230_RMU_ISP_CTRL      0x80   /* mixed: HW_DONE bits + SW_DONE bits     */
#define K230_RMU_DPU_CTRL      0x88
#define K230_RMU_DISP_CTRL     0x90
#define K230_RMU_GPU_CTRL      0x98
#define K230_RMU_AUDIO_CTRL    0xA4
#define K230_RMU_SPI2AXI_CTRL  0xA8   /* SW_DONE (active high)                  */

/* Bit layout shared by the CPU0/CPU1 control registers. */
#define K230_RMU_CPU_RESET     BIT(0)    /* reset request, auto/soft cleared    */
#define K230_RMU_CPU_FLUSH     BIT(4)    /* L2 flush request, hw auto-clears     */
#define K230_RMU_CPU_DONE      BIT(12)   /* done bit, write-1-to-clear           */

/* The high 16 bits are per-bit write-enable strobes (CPU0/CPU1 registers). */
#define K230_RMU_WE_SHIFT      16

struct K230RmuState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /*
     * One 32-bit word per MMIO offset. Indexing by word offset keeps the
     * VMState description trivial. Reset requests auto-clear, so what
     * actually persists here is done bits and SW_DONE storage bits.
     */
    uint32_t regs[K230_RMU_NUM_REGS];
};

#endif /* HW_MISC_K230_RMU_H */
