/*
 * RISC-V IOMMU - accelerated (nested) translation support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_RISCV_IOMMU_ACCEL_H
#define HW_RISCV_RISCV_IOMMU_ACCEL_H

#include "hw/riscv/riscv-iommu.h"
#include CONFIG_DEVICES

typedef struct RISCVIOMMUAccelDevice {
    RISCVIOMMUSpace as;
} RISCVIOMMUAccelDevice;

#ifdef CONFIG_RISCV_IOMMU_ACCEL
void riscv_iommu_accel_init(RISCVIOMMUState *s);
#else
static inline void riscv_iommu_accel_init(RISCVIOMMUState *s)
{
}
#endif

#endif /* HW_RISCV_RISCV_IOMMU_ACCEL_H */
