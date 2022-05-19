/*
 * Virtio Vhost-user Device
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_VIRTIO_VHOST_USER_H
#define QEMU_VIRTIO_VHOST_USER_H

#include "hw/virtio/virtio.h"
#include "standard-headers/linux/virtio_pci.h"
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"

#define TYPE_VIRTIO_VHOST_USER "virtio-vhost-user-device"
#define VIRTIO_VHOST_USER(obj) \
        OBJECT_CHECK(VirtIOVhostUser, (obj), TYPE_VIRTIO_VHOST_USER)

/* Macros for the offsets in virtio notification structure */
#define NOTIFICATION_SELECT            0
#define NOTIFICATION_MSIX_VECTOR       2

/* Macros for the additional resources configuration types */
#define VIRTIO_PCI_CAP_DOORBELL_CFG 6
#define VIRTIO_PCI_CAP_NOTIFICATION_CFG 7
#define VIRTIO_PCI_CAP_SHARED_MEMORY_CFG 8

/* The virtio configuration space fields */
typedef struct {
    uint32_t status;
#define VIRTIO_VHOST_USER_STATUS_BACKEND_UP (1 << 0)
#define VIRTIO_VHOST_USER_STATUS_FRONTEND_UP (1 << 1)
    uint32_t max_vhost_queues;
    uint8_t uuid[16];
} QEMU_PACKED VirtIOVhostUserConfig;

/* Keep track of the mmap for each memory table region */
typedef struct {
    MemoryRegion mr;
    void *mmap_addr;
    size_t total_size;
} VirtIOVhostUserMemTableRegion;

struct kickfd {
    VirtIODevice *vdev;
    EventNotifier guest_notifier;
    uint16_t msi_vector;
};

/* Additional resources configuration structures */

/* Doorbell structure layout */
struct virtio_pci_doorbell_cap {
    struct virtio_pci_cap cap;
    uint32_t doorbell_off_multiplier;
};

/* Notification structure layout */
struct virtio_pci_notification_cfg {
    uint16_t notification_select;              /* read-write */
    uint16_t notification_msix_vector;         /* read-write */
};

typedef struct VirtIOVhostUser VirtIOVhostUser;
struct VirtIOVhostUser {
    VirtIODevice parent_obj;

    /* The vhost-user socket */
    CharBackend chr;

    /* notification select */
    uint16_t nselect;
    /* Eventfds from VHOST_USER_SET_VRING_KICK along with the MSI-X vectors. */
    struct kickfd kickfds[VIRTIO_QUEUE_MAX];

    /* Eventfds from VHOST_USER_SET_VRING_CALL */
    EventNotifier callfds[VIRTIO_QUEUE_MAX];

    /* Mapped memory regions from VHOST_USER_SET_MEM_TABLE */
    VirtIOVhostUserMemTableRegion mem_table[VHOST_MEMORY_MAX_NREGIONS];

    VirtIOVhostUserConfig config;

    /* Connection establishment state */
    int conn_state;

    /* Device-to-driver message queue */
    VirtQueue *rxq;

    /* Driver-to-device message queue */
    VirtQueue *txq;

    /* Asynchronous read state */
    int read_bytes_needed;
    void *read_ptr;
    void (*read_done)(VirtIOVhostUser *s);
    VhostUserMsg read_msg;
    bool read_waiting_on_rxq; /* need rx buffer? */
    size_t read_msg_size;

    /* Asynchronous write state */
    int write_bytes_avail;
    void *write_ptr;
    void (*write_done)(VirtIOVhostUser *s);
    VhostUserMsg write_msg;
    guint write_watch_tag;
};

void virtio_vhost_user_set_vhost_mem_regions(VirtIOVhostUser *s);
void virtio_vhost_user_delete_vhost_mem_region(VirtIOVhostUser *s,
                                               MemoryRegion *mr);
void virtio_vhost_user_cleanup_additional_resources(VirtIOVhostUser *s);
void virtio_vhost_user_register_doorbell(VirtIOVhostUser *s, EventNotifier *e,
                                         uint8_t vq_idx);
void virtio_vhost_user_unregister_doorbell(VirtIOVhostUser *s, EventNotifier *e,
                                           uint8_t vq_idx);
void virtio_vhost_user_guest_notifier_read(EventNotifier *n);

#endif /* QEMU_VIRTIO_VHOST_USER_H */
