/*
 * RISC-V IOMMU - accelerated (nested) translation support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_RISCV_IOMMU_ACCEL_H
#define HW_RISCV_RISCV_IOMMU_ACCEL_H

#include "hw/riscv/riscv-iommu.h"
#include "system/iommufd.h"
#ifdef CONFIG_LINUX
#include <linux/iommufd.h>
#endif
#include CONFIG_DEVICES

/*
 * Represents an accelerated IOMMU instance backed by an iommufd vIOMMU object.
 * Holds bypass and abort proxy HWPT IDs used for device attachment.
 */
typedef struct RISCVIOMMUAccelState {
    IOMMUFDViommu *viommu;
    uint32_t bypass_hwpt_id;
    uint32_t abort_hwpt_id;
    QLIST_HEAD(, RISCVIOMMUAccelDevice) device_list;
} RISCVIOMMUAccelState;

typedef struct RISCVIOMMUAccelDevice {
    RISCVIOMMUSpace as;
    HostIOMMUDeviceIOMMUFD *idev;
    QLIST_ENTRY(RISCVIOMMUAccelDevice) next;
    RISCVIOMMUAccelState *accel;
} RISCVIOMMUAccelDevice;

#ifdef CONFIG_RISCV_IOMMU_ACCEL
void riscv_iommu_accel_init(RISCVIOMMUState *s);
#else
static inline void riscv_iommu_accel_init(RISCVIOMMUState *s)
{
}
#endif

#endif /* HW_RISCV_RISCV_IOMMU_ACCEL_H */
