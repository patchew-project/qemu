#include "qemu/osdep.h"
#include "sysemu/host_iommu_device.h"

OBJECT_DEFINE_ABSTRACT_TYPE(HostIOMMUDevice,
                            host_iommu_device,
                            HOST_IOMMU_DEVICE,
                            OBJECT)

static void host_iommu_device_class_init(ObjectClass *oc, void *data)
{
}

static void host_iommu_device_init(Object *obj)
{
}

static void host_iommu_device_finalize(Object *obj)
{
}
