#include "qemu/osdep.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"

bool vhost_has_free_slot(void)
{
    return true;
}

VhostUserState *vhost_user_init(void)
{
    return NULL;
}

void vhost_user_cleanup(VhostUserState *user)
{
}

int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, uint32_t busyloop_timeout)
{
    return -ENODEV;
}

void vhost_dev_cleanup(struct vhost_dev *hdev)
{
}

int vhost_dev_start(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    abort();
}

void vhost_dev_stop(struct vhost_dev *hdev, VirtIODevice *vdev)
{
}

int vhost_dev_enable_notifiers(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    abort();
}

void vhost_dev_disable_notifiers(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    abort();
}

int vhost_net_set_backend(struct vhost_dev *hdev,
                          struct vhost_vring_file *file)
{
    abort();
}

uint64_t vhost_get_features(struct vhost_dev *hdev, const int *feature_bits,
                            uint64_t features)
{
    abort();
}

void vhost_ack_features(struct vhost_dev *hdev, const int *feature_bits,
                        uint64_t features)
{
    abort();
}

bool vhost_virtqueue_pending(struct vhost_dev *hdev, int n)
{
    abort();
}

void vhost_virtqueue_mask(struct vhost_dev *hdev, VirtIODevice *vdev, int n,
                         bool mask)
{
    abort();
}
