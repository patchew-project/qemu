/*
 * Virtio Block Device
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_VIRTIO_BLK_H
#define QEMU_VIRTIO_BLK_H

#include "standard-headers/linux/virtio_blk.h"
#include "hw/virtio/virtio.h"
#include "hw/block/block.h"
#include "sysemu/iothread.h"
#include "sysemu/block-backend.h"
#include "sysemu/block-ram-registrar.h"
#include "qom/object.h"
#include "qapi/qapi-types-virtio.h"

#define TYPE_VIRTIO_BLK "virtio-blk-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOBlock, VIRTIO_BLK)

/* This is the last element of the write scatter-gather list */
struct virtio_blk_inhdr
{
    unsigned char status;
};

#define VIRTIO_BLK_AUTO_NUM_QUEUES UINT16_MAX

struct VirtIOBlkConf
{
    BlockConf conf;
    IOThread *iothread;
    IOThreadVirtQueueMappingList *iothread_vq_mapping_list;
    char *serial;
    uint32_t request_merging;
    uint16_t num_queues;
    uint16_t queue_size;
    bool seg_max_adjust;
    bool report_discard_granularity;
    uint32_t max_discard_sectors;
    uint32_t max_write_zeroes_sectors;
    bool x_enable_wce_if_config_wce;
};

typedef struct vhost_blk {
    struct vhost_dev dev;
    /* vhost-blk only use ONE virtqueue now */
    struct vhost_virtqueue vqs[1];
    /* fd for chardev /dev/vhost-blk */
    int vhostfd;
} vhost_blk;

/* Attach virtio blk ring to an ocfs2 file with modified dio framework.
 * Pass fd -1 to unbind from the file and the backend. This can be used
 * to stop the ring (e.g. for migration). */
#define VHOST_BLK_SET_BACKEND _IOW(VHOST_VIRTIO, 0x50, struct vhost_vring_file)


struct VirtIOBlockReq;
struct VirtIOBlock {
    VirtIODevice parent_obj;
    BlockBackend *blk;
    QemuMutex rq_lock;
    struct VirtIOBlockReq *rq; /* protected by rq_lock */
    VirtIOBlkConf conf;
    unsigned short sector_mask;
    bool original_wce;
    VMChangeStateEntry *change;
    bool ioeventfd_disabled;
    bool ioeventfd_started;
    bool ioeventfd_starting;
    bool ioeventfd_stopping;

    /*
     * The AioContext for each virtqueue. The BlockDriverState will use the
     * first element as its AioContext.
     */
    AioContext **vq_aio_context;

    int blkfd;
    bool vhost_enabled;
    bool vhost_started;
    bool vhost_acked;
    vhost_blk vhblk[VIRTIO_QUEUE_MAX];

    uint64_t host_features;
    size_t config_size;
    BlockRAMRegistrar blk_ram_registrar;
};

typedef struct VirtIOBlockReq {
    VirtQueueElement elem;
    int64_t sector_num;
    VirtIOBlock *dev;
    VirtQueue *vq;
    IOVDiscardUndo inhdr_undo;
    IOVDiscardUndo outhdr_undo;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr out;
    QEMUIOVector qiov;
    size_t in_len;
    struct VirtIOBlockReq *next;
    struct VirtIOBlockReq *mr_next;
    BlockAcctCookie acct;
} VirtIOBlockReq;

#define VIRTIO_BLK_MAX_MERGE_REQS 32

typedef struct MultiReqBuffer {
    VirtIOBlockReq *reqs[VIRTIO_BLK_MAX_MERGE_REQS];
    unsigned int num_reqs;
    bool is_write;
} MultiReqBuffer;

void virtio_blk_handle_vq(VirtIOBlock *s, VirtQueue *vq);

#endif
