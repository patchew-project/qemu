/*
 * Kendryte K230 IOMUX
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * Copyright (c) 2026 Kangjie Huang <flamboyant.h.01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_K230_IOMUX_H
#define HW_MISC_K230_IOMUX_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_K230_IOMUX "riscv.k230.iomux"
OBJECT_DECLARE_SIMPLE_TYPE(K230IomuxState, K230_IOMUX)

#define K230_IOMUX_MMIO_SIZE 0x800
#define K230_IOMUX_NUM_REGS 64
#define K230_IOMUX_REGS_SIZE \
    (K230_IOMUX_NUM_REGS * sizeof(uint32_t))
#define K230_IOMUX_WRITABLE_MASK 0x00003fff

struct K230IomuxState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t regs[K230_IOMUX_NUM_REGS];
};

#endif /* HW_MISC_K230_IOMUX_H */
