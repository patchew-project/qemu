#ifndef HOST_IOMMU_DEVICE_H
#define HOST_IOMMU_DEVICE_H

#include "qom/object.h"

#define TYPE_HOST_IOMMU_DEVICE "host-iommu-device"
OBJECT_DECLARE_TYPE(HostIOMMUDevice, HostIOMMUDeviceClass, HOST_IOMMU_DEVICE)

struct HostIOMMUDevice {
    Object parent;
};

struct HostIOMMUDeviceClass {
    ObjectClass parent_class;

    int (*get_host_iommu_info)(HostIOMMUDevice *hiod, void *data, uint32_t len,
                               Error **errp);
};

/*
 * Define the format of host IOMMU related info that current VFIO
 * or VDPA can privode to vIOMMU.
 *
 * @aw_bits: Host IOMMU address width. 0xff if no limitation.
 */
typedef struct HIOD_LEGACY_INFO {
    uint8_t aw_bits;
} HIOD_LEGACY_INFO;
#endif
