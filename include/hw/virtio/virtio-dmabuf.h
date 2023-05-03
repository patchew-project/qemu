/*
 * Virtio Shared dma-buf
 *
 * Copyright Red Hat, Inc. 2023
 *
 * Authors:
 *     Albert Esteve <aesteve@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef VIRTIO_DMABUF_H
#define VIRTIO_DMABUF_H

#include "qemu/osdep.h"

#include <glib.h>
#include "qemu/uuid.h"

/**
 * virtio_add_dmabuf() - Add a new dma-buf resource to the lookup table
 * @uuid: new resource's UUID
 * @dmabuf_fd: the dma-buf descriptor that will be stored and shared with
 *             other virtio devices
 *
 * Note: @dmabuf_fd must be a valid (non-negative) file descriptor.
 *
 * Return: true if the UUID did not exist and the resource has been added,
 * false if another resource with the same UUID already existed.
 * Note that if it finds a repeated UUID, the resource is not inserted in
 * the lookup table.
 */
bool virtio_add_dmabuf(QemuUUID uuid, int dmabuf_fd);

/**
 * virtio_remove_resource() - Removes a resource from the lookup table
 * @uuid: resource's UUID
 *
 * Return: true if the UUID has been found and removed from the lookup table.
 */
bool virtio_remove_resource(QemuUUID uuid);

/**
 * virtio_lookup_dmabuf() - Looks for a dma-buf resource in the lookup table
 * @uuid: resource's UUID
 *
 * Return: the dma-buf file descriptor integer, or -1 if the key is not found.
 */
int virtio_lookup_dmabuf(QemuUUID uuid);

/**
 * virtio_free_resources() - Destroys all keys and values of the shared
 * resources lookup table, and frees them
 */
void virtio_free_resources(void);

#endif /* VIRTIO_DMABUF_H */
