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

#include "hw/virtio/virtio-dmabuf.h"


static GMutex lock;
static GHashTable *resource_uuids;

/*
 * uuid_equal_func: wrapper for UUID is_equal function to
 * satisfy g_hash_table_new expected parameters signatures.
 */
static int uuid_equal_func(const void *lhv, const void *rhv)
{
    return qemu_uuid_is_equal(lhv, rhv);
}

static bool virtio_add_resource(QemuUUID *uuid, gpointer value)
{
    assert(resource_uuids != NULL);
    if (g_hash_table_lookup(resource_uuids, uuid) != NULL) {
        return false;
    }

    return g_hash_table_insert(resource_uuids, uuid, value);
}

static gpointer virtio_lookup_resource(const QemuUUID *uuid)
{
    if (resource_uuids == NULL) {
        return NULL;
    }

    return g_hash_table_lookup(resource_uuids, uuid);
}

bool virtio_add_dmabuf(QemuUUID *uuid, int udmabuf_fd)
{
    bool result;
    if (udmabuf_fd < 0) {
        return false;
    }
    g_mutex_lock(&lock);
    if (resource_uuids == NULL) {
        resource_uuids = g_hash_table_new(qemu_uuid_hash, uuid_equal_func);
    }
    result = virtio_add_resource(uuid, GINT_TO_POINTER(udmabuf_fd));
    g_mutex_unlock(&lock);

    return result;
}

bool virtio_remove_resource(const QemuUUID *uuid)
{
    bool result;
    g_mutex_lock(&lock);
    result = g_hash_table_remove(resource_uuids, uuid);
    g_mutex_unlock(&lock);

    return result;
}

int virtio_lookup_dmabuf(const QemuUUID *uuid)
{
    g_mutex_lock(&lock);
    gpointer lookup_res = virtio_lookup_resource(uuid);
    g_mutex_unlock(&lock);
    if (lookup_res == NULL) {
        return -1;
    }

    return GPOINTER_TO_INT(lookup_res);
}

void virtio_free_resources(void)
{
    g_hash_table_destroy(resource_uuids);
    /* Reference count shall be 0 after the implicit unref on destroy */
    resource_uuids = NULL;
}
