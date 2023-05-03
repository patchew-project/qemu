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


#define UUID_TO_POINTER(i) \
    ((gpointer) (g_intern_static_string(qemu_uuid_unparse_strdup((&i)))))


static GMutex lock;
static GHashTable *resource_uuids;


static bool virtio_add_resource(QemuUUID uuid, gpointer value)
{
    gpointer struuid = UUID_TO_POINTER(uuid);
    if (resource_uuids == NULL) {
        resource_uuids = g_hash_table_new_full(NULL, NULL, NULL, g_free);
    } else if (g_hash_table_lookup(resource_uuids, struuid) != NULL) {
        return false;
    }

    return g_hash_table_insert(resource_uuids, struuid, value);
}

static gpointer virtio_lookup_resource(QemuUUID uuid)
{
    if (resource_uuids == NULL) {
        return NULL;
    }

    return g_hash_table_lookup(resource_uuids, UUID_TO_POINTER(uuid));
}

bool virtio_add_dmabuf(QemuUUID uuid, int udmabuf_fd)
{
    bool result;
    if (udmabuf_fd < 0) {
        return false;
    }
    g_mutex_lock(&lock);
    if (resource_uuids == NULL) {
        resource_uuids = g_hash_table_new(NULL, NULL);
    }
    result = virtio_add_resource(uuid, GINT_TO_POINTER(udmabuf_fd));
    g_mutex_unlock(&lock);

    return result;
}

bool virtio_remove_resource(QemuUUID uuid)
{
    bool result;
    g_mutex_lock(&lock);
    result = g_hash_table_remove(resource_uuids, UUID_TO_POINTER(uuid));
    g_mutex_unlock(&lock);

    return result;
}

int virtio_lookup_dmabuf(QemuUUID uuid)
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
