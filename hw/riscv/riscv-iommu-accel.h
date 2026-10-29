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

typedef struct RISCVIOMMUS1Hwpt {
    uint32_t hwpt_id;
} RISCVIOMMUS1Hwpt;

typedef struct RISCVIOMMUAccelDevice {
    RISCVIOMMUSpace as;
    HostIOMMUDeviceIOMMUFD *idev;
    RISCVIOMMUS1Hwpt *s1_hwpt;
    IOMMUFDVdev *vdev;
    QLIST_ENTRY(RISCVIOMMUAccelDevice) next;
    RISCVIOMMUAccelState *accel;
} RISCVIOMMUAccelDevice;

#ifdef CONFIG_RISCV_IOMMU_ACCEL
void riscv_iommu_accel_init(RISCVIOMMUState *s);
bool riscv_iommu_accel_install_dc(RISCVIOMMUState *s, int devid);
bool riscv_iommu_accel_install_dc_all(RISCVIOMMUState *s);
#else
static inline void riscv_iommu_accel_init(RISCVIOMMUState *s)
{
}
static inline bool
riscv_iommu_accel_install_dc(RISCVIOMMUState *s, int devid)
{
    return true;
}
static inline bool
riscv_iommu_accel_install_dc_all(RISCVIOMMUState *s)
{
    return true;
}
#endif

#endif /* HW_RISCV_RISCV_IOMMU_ACCEL_H */
