#include "qemu/osdep.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"

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
