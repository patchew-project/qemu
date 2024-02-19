/*
 * Virtio Shared dma-buf
 *
 * Copyright Red Hat, Inc. 2023
 *
 * Authors:
 *     Albert Esteve <aesteve@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "include/qemu/lockable.h"

#include "hw/virtio/virtio-dmabuf.h"


static QemuMutex lock;
static GHashTable *resource_uuids;

/*
 * uuid_equal_func: wrapper for UUID is_equal function to
 * satisfy g_hash_table_new expected parameters signatures.
 */
static int uuid_equal_func(const void *lhv, const void *rhv)
{
    return qemu_uuid_is_equal(lhv, rhv);
}

void virtio_dmabuf_init(void) {
    qemu_mutex_init(&lock);
}

static bool virtio_add_resource(QemuUUID *uuid, VirtioSharedObject *value)
{
    bool result = true;

    WITH_QEMU_LOCK_GUARD(&lock) {
        if (resource_uuids == NULL) {
            resource_uuids = g_hash_table_new_full(qemu_uuid_hash,
                                                uuid_equal_func,
                                                NULL,
                                                g_free);
        }
        if (g_hash_table_lookup(resource_uuids, uuid) == NULL) {
            g_hash_table_insert(resource_uuids, uuid, value);
        } else {
            result = false;
        }
    }

    return result;
}

bool virtio_dmabuf_add(QemuUUID *uuid, int udmabuf_fd)
{
    bool result;
    VirtioSharedObject *vso;
    if (udmabuf_fd < 0) {
        return false;
    }
    vso = g_new(VirtioSharedObject, 1);
    vso->type = TYPE_DMABUF;
    vso->value.udma_buf = udmabuf_fd;
    result = virtio_add_resource(uuid, vso);
    if (!result) {
        g_free(vso);
    }

    return result;
}

bool virtio_dmabuf_add_vhost_device(QemuUUID *uuid, struct vhost_dev *dev)
{
    bool result;
    VirtioSharedObject *vso;
    if (dev == NULL) {
        return false;
    }
    vso = g_new(VirtioSharedObject, 1);
    vso->type = TYPE_VHOST_DEV;
    vso->value.dev = dev;
    result = virtio_add_resource(uuid, vso);
    if (!result) {
        g_free(vso);
    }

    return result;
}

bool virtio_dmabuf_remove_resource(const QemuUUID *uuid)
{
    bool result;
    WITH_QEMU_LOCK_GUARD(&lock) {
        result = g_hash_table_remove(resource_uuids, uuid);
    }

    return result;
}

static VirtioSharedObject *get_shared_object(const QemuUUID *uuid)
{
    gpointer lookup_res = NULL;

    WITH_QEMU_LOCK_GUARD(&lock) {
        if (resource_uuids != NULL) {
            lookup_res = g_hash_table_lookup(resource_uuids, uuid);
        }
    }

    return (VirtioSharedObject *) lookup_res;
}

int virtio_dmabuf_lookup(const QemuUUID *uuid)
{
    VirtioSharedObject *vso = get_shared_object(uuid);
    if (vso == NULL) {
        return -1;
    }
    assert(vso->type == TYPE_DMABUF);
    return vso->value.udma_buf;
}

struct vhost_dev *virtio_dmabuf_lookup_vhost_device(const QemuUUID *uuid)
{
    VirtioSharedObject *vso = get_shared_object(uuid);
    if (vso == NULL) {
        return NULL;
    }
    assert(vso->type == TYPE_VHOST_DEV);
    return (struct vhost_dev *) vso->value.dev;
}

SharedObjectType virtio_dmabuf_object_type(const QemuUUID *uuid)
{
    VirtioSharedObject *vso = get_shared_object(uuid);
    if (vso == NULL) {
        return TYPE_INVALID;
    }
    return vso->type;
}

static bool virtio_dmabuf_resource_is_owned(gpointer key,
                                            gpointer value,
                                            gpointer dev)
{
    VirtioSharedObject *vso;

    vso = (VirtioSharedObject *) value;
    return vso->type == TYPE_VHOST_DEV && vso->value.dev == dev;
}

int virtio_dmabuf_vhost_cleanup(struct vhost_dev *dev)
{
    int num_removed;

    WITH_QEMU_LOCK_GUARD(&lock) {
        num_removed = g_hash_table_foreach_remove(
            resource_uuids, (GHRFunc) virtio_dmabuf_resource_is_owned, dev);
    }
    return num_removed;
}

void virtio_dmabuf_free_resources(void)
{
    WITH_QEMU_LOCK_GUARD(&lock) {
        g_hash_table_destroy(resource_uuids);
        /* Reference count shall be 0 after the implicit unref on destroy */
        resource_uuids = NULL;
    }
}
