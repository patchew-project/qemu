/*
 * Virtio Support
 *
 * Copyright IBM, Corp. 2007-2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Rusty Russell     <rusty@rustcorp.com.au>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_VIRTIO_BALLOON_H
#define QEMU_VIRTIO_BALLOON_H

#include "standard-headers/linux/virtio_balloon.h"
#include "hw/virtio/virtio.h"
#include "hw/pci/pci.h"

#define TYPE_VIRTIO_BALLOON "virtio-balloon-device"
#define VIRTIO_BALLOON(obj) \
        OBJECT_CHECK(VirtIOBalloon, (obj), TYPE_VIRTIO_BALLOON)

typedef struct virtio_balloon_stat VirtIOBalloonStat;

typedef struct virtio_balloon_stat_modern {
       uint16_t tag;
       uint8_t reserved[6];
       uint64_t val;
} VirtIOBalloonStatModern;

enum virtio_balloon_free_page_report_status {
    FREE_PAGE_REPORT_S_REQUESTED,
    FREE_PAGE_REPORT_S_START,
    FREE_PAGE_REPORT_S_STOP,
};

typedef struct VirtIOBalloon {
    VirtIODevice parent_obj;
    VirtQueue *ivq, *dvq, *svq, *free_page_vq;
    /* Host is requesting the guest to stop free page reporting */
    bool host_stop_free_page;
    uint32_t free_page_report_status;
    uint32_t num_pages;
    uint32_t actual;
    uint32_t free_page_report_cmd_id;
    uint32_t poison_val;
    uint64_t stats[VIRTIO_BALLOON_S_NR];
    VirtQueueElement *stats_vq_elem;
    size_t stats_vq_offset;
    QEMUTimer *stats_timer;
    QEMUTimer *free_page_timer;
    int64_t stats_last_update;
    int64_t stats_poll_interval;
    int64_t free_page_wait_time;
    uint32_t host_features;
} VirtIOBalloon;

#endif
