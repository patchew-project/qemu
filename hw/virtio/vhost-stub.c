#include "qemu/osdep.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "hw/virtio/vhost-pci-slave.h"

bool vhost_has_free_slot(void)
{
    return true;
}

void vhost_user_asyn_read(void *opaque, const uint8_t *buf, int size)
{
    return;
}

int vhost_user_can_read(void *opaque)
{
    return 0;
}

int vhost_pci_slave_init(QemuOpts *opt)
{
    return -1;
}

int vhost_pci_slave_cleanup(void)
{
    return -1;
}
