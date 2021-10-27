#include "qemu/osdep.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"

unsigned int vhost_get_free_memslots(void)
{
    return true;
}

bool vhost_user_init(VhostUserState *user, CharBackend *chr, Error **errp)
{
    return false;
}

void vhost_user_cleanup(VhostUserState *user)
{
}
