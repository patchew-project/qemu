/*
 * VHost-user Shared Memory Object
 *
 * Copyright Red Hat, Inc. 2024
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
 * @fd_offset: Offset within the file descriptor
 * @shm_offset: Offset within the VIRTIO Shared Memory Region
 * @len: Size of the mapping
 * @flags: Mapping flags (read/write permissions)
 * @mapped_addr: Pointer to the mapped memory region
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
    uint64_t fd_offset;
    uint64_t shm_offset;
    uint64_t len;
    uint16_t flags;
    void *mapped_addr;
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
 * The object will automatically map the memory using mmap() and create
 * a MemoryRegion as a child object. When the object is finalized, it will
 * automatically unmap the memory and close the file descriptor.
 * Errors are reported via error_report().
 *
 * Return: A new VhostUserShmemObject on success, NULL on error.
 */
VhostUserShmemObject *vhost_user_shmem_object_new(uint8_t shmid,
                                                   int fd,
                                                   uint64_t fd_offset,
                                                   uint64_t shm_offset,
                                                   uint64_t len,
                                                   uint16_t flags);

/**
 * vhost_user_shmem_object_get_mr() - Get the MemoryRegion
 * @shmem_obj: VhostUserShmemObject instance
 *
 * Return: The MemoryRegion associated with this shared memory object.
 */
MemoryRegion *vhost_user_shmem_object_get_mr(VhostUserShmemObject *shmem_obj);

/**
 * vhost_user_shmem_object_get_fd() - Get the file descriptor
 * @shmem_obj: VhostUserShmemObject instance
 *
 * Return: The file descriptor for the shared memory region.
 */
int vhost_user_shmem_object_get_fd(VhostUserShmemObject *shmem_obj);

/**
 * vhost_user_shmem_object_get_mapped_addr() - Get the mapped memory address
 * @shmem_obj: VhostUserShmemObject instance
 *
 * Return: The mapped memory address.
 */
void *vhost_user_shmem_object_get_mapped_addr(VhostUserShmemObject *shmem_obj);

#endif /* VHOST_USER_SHMEM_H */
