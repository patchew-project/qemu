/*
 * VHost-user Shared Memory Object
 *
 * Copyright Red Hat, Inc. 2025
 *
 * Authors:
 *     Albert Esteve <aesteve@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/virtio/vhost-user-shmem.h"
#include "system/memory.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "trace.h"

/**
 * VhostUserShmemObject
 *
 * An intermediate QOM object that manages individual shared memory mappings
 * created by VHOST_USER_BACKEND_SHMEM_MAP requests. It acts as a parent for
 * MemoryRegion objects, providing proper lifecycle management with reference
 * counting. When the object is unreferenced and its reference count drops
 * to zero, it automatically cleans up the MemoryRegion and unmaps the memory.
 */

static void vhost_user_shmem_object_finalize(Object *obj);
static void vhost_user_shmem_object_instance_init(Object *obj);

static const TypeInfo vhost_user_shmem_object_info = {
    .name = TYPE_VHOST_USER_SHMEM_OBJECT,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(VhostUserShmemObject),
    .instance_init = vhost_user_shmem_object_instance_init,
    .instance_finalize = vhost_user_shmem_object_finalize,
};

static void vhost_user_shmem_object_instance_init(Object *obj)
{
    VhostUserShmemObject *shmem_obj = VHOST_USER_SHMEM_OBJECT(obj);

    shmem_obj->shmid = 0;
    shmem_obj->fd = -1;
    shmem_obj->shm_offset = 0;
    shmem_obj->len = 0;
    shmem_obj->mr = NULL;
}

static void vhost_user_shmem_object_finalize(Object *obj)
{
    VhostUserShmemObject *shmem_obj = VHOST_USER_SHMEM_OBJECT(obj);

    /* Clean up MemoryRegion if it exists */
    if (shmem_obj->mr) {
        /* Unparent the MemoryRegion to trigger cleanup */
        object_unparent(OBJECT(shmem_obj->mr));
        shmem_obj->mr = NULL;
    }

    /* Close file descriptor */
    if (shmem_obj->fd >= 0) {
        close(shmem_obj->fd);
        shmem_obj->fd = -1;
    }
}

VhostUserShmemObject *vhost_user_shmem_object_new(uint8_t shmid,
                                                   int fd,
                                                   uint64_t fd_offset,
                                                   uint64_t shm_offset,
                                                   uint64_t len,
                                                   uint16_t flags)
{
    VhostUserShmemObject *shmem_obj;
    MemoryRegion *mr;
    g_autoptr(GString) mr_name = g_string_new(NULL);
    uint32_t ram_flags;
    Error *local_err = NULL;

    if (len == 0) {
        error_report("Shared memory mapping size cannot be zero");
        return NULL;
    }

    fd = dup(fd);
    if (fd < 0) {
        error_report("Failed to duplicate fd: %s", strerror(errno));
        return NULL;
    }

    /* Determine RAM flags */
    ram_flags = RAM_SHARED;
    if (!(flags & VHOST_USER_FLAG_MAP_RW)) {
        ram_flags |= RAM_READONLY_FD;
    }

    /* Create the VhostUserShmemObject */
    shmem_obj = VHOST_USER_SHMEM_OBJECT(
        object_new(TYPE_VHOST_USER_SHMEM_OBJECT));

    /* Set up object properties */
    shmem_obj->shmid = shmid;
    shmem_obj->fd = fd;
    shmem_obj->shm_offset = shm_offset;
    shmem_obj->len = len;

    /* Create MemoryRegion as a child of this object */
    mr = g_new0(MemoryRegion, 1);
    g_string_printf(mr_name, "vhost-user-shmem-%d-%" PRIx64, shmid, shm_offset);

    /* Initialize MemoryRegion with file descriptor */
    if (!memory_region_init_ram_from_fd(mr, OBJECT(shmem_obj), mr_name->str,
                                        len, ram_flags, fd, fd_offset,
                                        &local_err)) {
        error_report_err(local_err);
        g_free(mr);
        close(fd);
        object_unref(OBJECT(shmem_obj));
        return NULL;
    }

    shmem_obj->mr = mr;
    return shmem_obj;
}

static void vhost_user_shmem_register_types(void)
{
    type_register_static(&vhost_user_shmem_object_info);
}

type_init(vhost_user_shmem_register_types)
