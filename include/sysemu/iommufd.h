#ifndef SYSEMU_IOMMUFD_H
#define SYSEMU_IOMMUFD_H

#include "qom/object.h"
#include "qemu/thread.h"
#include "exec/hwaddr.h"
#include "exec/cpu-common.h"

#define TYPE_IOMMUFD_BACKEND "iommufd"
OBJECT_DECLARE_TYPE(IOMMUFDBackend, IOMMUFDBackendClass,
                    IOMMUFD_BACKEND)
#define IOMMUFD_BACKEND(obj) \
    OBJECT_CHECK(IOMMUFDBackend, (obj), TYPE_IOMMUFD_BACKEND)
#define IOMMUFD_BACKEND_GET_CLASS(obj) \
    OBJECT_GET_CLASS(IOMMUFDBackendClass, (obj), TYPE_IOMMUFD_BACKEND)
#define IOMMUFD_BACKEND_CLASS(klass) \
    OBJECT_CLASS_CHECK(IOMMUFDBackendClass, (klass), TYPE_IOMMUFD_BACKEND)
struct IOMMUFDBackendClass {
    ObjectClass parent_class;
};

struct IOMMUFDBackend {
    Object parent;

    /*< protected >*/
    int fd;            /* /dev/iommu file descriptor */
    bool owned;        /* is the /dev/iommu opened internally */
    QemuMutex lock;
    uint32_t users;

    /*< public >*/
};

int iommufd_backend_connect(IOMMUFDBackend *be, Error **errp);
void iommufd_backend_disconnect(IOMMUFDBackend *be);

int iommufd_backend_get_ioas(IOMMUFDBackend *be, uint32_t *ioas_id);
void iommufd_backend_put_ioas(IOMMUFDBackend *be, uint32_t ioas_id);
void iommufd_backend_free_id(int fd, uint32_t id);
int iommufd_backend_unmap_dma(IOMMUFDBackend *be, uint32_t ioas,
                              hwaddr iova, ram_addr_t size);
int iommufd_backend_map_dma(IOMMUFDBackend *be, uint32_t ioas, hwaddr iova,
                            ram_addr_t size, void *vaddr, bool readonly);
int iommufd_backend_copy_dma(IOMMUFDBackend *be, uint32_t src_ioas,
                             uint32_t dst_ioas, hwaddr iova,
                             ram_addr_t size, bool readonly);
int iommufd_backend_alloc_hwpt(int iommufd, uint32_t dev_id,
                               uint32_t pt_id, uint32_t *out_hwpt);
#endif
