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

#ifndef VHOST_USER_SHMEM_H
#define VHOST_USER_SHMEM_H

#include "qemu/osdep.h"
#include "qom/object.h"
#include "system/memory.h"
#include "qapi/error.h"

/* vhost-user memory mapping flags */
#define VHOST_USER_FLAG_MAP_RW (1u << 0)

#define TYPE_VHOST_USER_SHMEM_OBJECT "vhost-user-shmem"
OBJECT_DECLARE_SIMPLE_TYPE(VhostUserShmemObject, VHOST_USER_SHMEM_OBJECT)

/**
 * VhostUserShmemObject:
 * @parent: Parent object
 * @shmid: VIRTIO Shared Memory Region ID
 * @fd: File descriptor for the shared memory region
 * @shm_offset: Offset within the VIRTIO Shared Memory Region
 * @len: Size of the mapping
 * @mr: MemoryRegion associated with this shared memory mapping
 *
 * An intermediate QOM object that manages individual shared memory mappings
 * created by VHOST_USER_BACKEND_SHMEM_MAP requests. It acts as a parent for
 * MemoryRegion objects, providing proper lifecycle management with reference
 * counting. When the object is unreferenced and its reference count drops
 * to zero, it automatically cleans up the MemoryRegion and unmaps the memory.
 */
struct VhostUserShmemObject {
    Object parent;

    uint8_t shmid;
    int fd;
    uint64_t shm_offset;
    uint64_t len;
    MemoryRegion *mr;
};

/**
 * vhost_user_shmem_object_new() - Create a new VhostUserShmemObject
 * @shmid: VIRTIO Shared Memory Region ID
 * @fd: File descriptor for the shared memory
 * @fd_offset: Offset within the file descriptor
 * @shm_offset: Offset within the VIRTIO Shared Memory Region
 * @len: Size of the mapping
 * @flags: Mapping flags (VHOST_USER_FLAG_MAP_*)
 *
 * Creates a new VhostUserShmemObject that manages a shared memory mapping.
 * The object will create a MemoryRegion using memory_region_init_ram_from_fd()
 * as a child object. When the object is finalized, it will automatically
 * clean up the MemoryRegion and close the file descriptor.
 *
 * Return: A new VhostUserShmemObject on success, NULL on error.
 */
VhostUserShmemObject *vhost_user_shmem_object_new(uint8_t shmid,
                                                   int fd,
                                                   uint64_t fd_offset,
                                                   uint64_t shm_offset,
                                                   uint64_t len,
                                                   uint16_t flags);

#endif /* VHOST_USER_SHMEM_H */
