/*
 * RISC-V IOMMU - accelerated (nested) translation support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_RISCV_IOMMU_ACCEL_H
#define HW_RISCV_RISCV_IOMMU_ACCEL_H

#include "qemu/notify.h"
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
    RISCVIOMMUState *iommu;
    IOMMUFDViommu *viommu;
    uint32_t bypass_hwpt_id;
    uint32_t abort_hwpt_id;
    QLIST_HEAD(, RISCVIOMMUAccelDevice) device_list;
    /* Machine init done notifier */
    Notifier machine_done;
    /* Set once machine init is done */
    bool finalised;
} RISCVIOMMUAccelState;

typedef struct RISCVIOMMUS1Hwpt {
    uint32_t hwpt_id;
} RISCVIOMMUS1Hwpt;

typedef struct RISCVIOMMUAccelDevice {
    RISCVIOMMUSpace as;
    HostIOMMUDeviceIOMMUFD *idev;
    RISCVIOMMUS1Hwpt *s1_hwpt;
    IOMMUFDVdev *vdev;
    /* Host IOMMU capabilities */
    uint64_t host_caps;
    QLIST_ENTRY(RISCVIOMMUAccelDevice) next;
    RISCVIOMMUAccelState *accel;
} RISCVIOMMUAccelDevice;

#ifdef CONFIG_RISCV_IOMMU_ACCEL
void riscv_iommu_accel_init(RISCVIOMMUState *s);
bool riscv_iommu_accel_install_dc(RISCVIOMMUState *s, int devid);
bool riscv_iommu_accel_install_dc_all(RISCVIOMMUState *s);
bool riscv_iommu_accel_issue_inv_cmd(RISCVIOMMUState *s,
                                     const struct riscv_iommu_command *cmd);
void riscv_iommu_accel_register_machine_done(RISCVIOMMUState *s);
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
static inline bool
riscv_iommu_accel_issue_inv_cmd(RISCVIOMMUState *s,
                                const struct riscv_iommu_command *cmd)
{
    return true;
}
static inline void riscv_iommu_accel_register_machine_done(RISCVIOMMUState *s)
{
}
#endif

#endif /* HW_RISCV_RISCV_IOMMU_ACCEL_H */
