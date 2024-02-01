#ifndef HOST_IOMMU_DEVICE_H
#define HOST_IOMMU_DEVICE_H

typedef enum HostIOMMUDevice_Type {
    HID_LEGACY,
    HID_IOMMUFD,
    HID_MAX,
} HostIOMMUDevice_Type;

typedef struct HostIOMMUDevice {
    HostIOMMUDevice_Type type;
    size_t size;
} HostIOMMUDevice;

static inline void host_iommu_base_device_init(HostIOMMUDevice *dev,
                                               HostIOMMUDevice_Type type,
                                               size_t size)
{
    dev->type = type;
    dev->size = size;
}
#endif
