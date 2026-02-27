/*
 * vhost-user-blk sample application
 *
 * Copyright (c) 2017 Intel Corporation. All rights reserved.
 *
 * Author:
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is based on the "vhost-user-scsi" sample and "virtio-blk" driver
 * implementation by:
 *  Felipe Franciosi <felipe@nutanix.com>
 *  Anthony Liguori <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "standard-headers/linux/virtio_blk.h"
#include "libvhost-user-glib.h"

#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

/* Maximum supported virtqueues for this example server. Adjust if adding
 * multiqueue support (must match guest configuration too). */
enum {
    VHOST_USER_BLK_MAX_QUEUES = 8,
};

/* Small header returned by block device to guest for each request. This
 * corresponds to the in-only status byte used by the virtio-blk protocol. */
struct virtio_blk_inhdr {
    unsigned char status;
};

/* vhost user block device
 *
 * This structure stores state for a single vhost-user-backed block device:
 *  - parent: common glue object (libvhost-user-glib) that holds generic vhost
 *            user device state.
 *  - blk_fd: file descriptor for the backing file / block device.
 *  - blkcfg: virtio_blk_config presented to the guest (capacity, block size).
 *  - enable_ro: whether the device was opened read-only.
 *  - blk_name: path to the backing file (owned/managed by caller).
 *  - loop: mainloop used to integrate with glib event handling.
 *
 * Note: this is a simple single-process example. A production implementation
 * may need more robust error handling, thread-safety, and integration with
 * QEMU's event loop.
 */
typedef struct VubDev {
    VugDev parent;
    int blk_fd;
    struct virtio_blk_config blkcfg;
    bool enable_ro;
    char *blk_name;
    GMainLoop *loop;
} VubDev;

/* Request context used while processing a single virtio-blk request.
 * It bundles vhost-user virtq element information with parsed headers and
 * temporary data used during I/O completion.
 */
typedef struct VubReq {
    VuVirtqElement *elem;              /* raw descriptor table info */
    int64_t sector_num;                /* sector number (512-byte sectors) */
    size_t size;                       /* total I/O size (bytes) */
    struct virtio_blk_inhdr *in;       /* pointer to 'in' header (status) */
    struct virtio_blk_outhdr *out;     /* pointer to 'out' header (type/sector) */
    VubDev *vdev_blk;                  /* backpointer to device state */
    struct VuVirtq *vq;                /* queue this request came from */
} VubReq;

/* ------------------------------------------------------------------------- */
/* Helper utilities (mirrors util/iov.c from QEMU where useful)             */
/* ------------------------------------------------------------------------- */

/* vub_iov_size - compute total bytes in scatter-gather vector
 *
 * This is identical to what QEMU's iov helpers provide: a simple sum of
 * iov_len fields; useful to validate sizes and allocate temporary buffers.
 */
static size_t vub_iov_size(const struct iovec *iov,
                              const unsigned int iov_cnt)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        len += iov[i].iov_len;
    }
    return len;
}

/* vub_iov_to_buf - copy scatter-gather iovector into contiguous buffer
 *
 * Caller must ensure 'buf' is large enough (e.g. use vub_iov_size()).
 * Used for parsing descriptors that contain small control structs.
 */
static size_t vub_iov_to_buf(const struct iovec *iov,
                             const unsigned int iov_cnt, void *buf)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        memcpy(buf + len,  iov[i].iov_base, iov[i].iov_len);
        len += iov[i].iov_len;
    }
    return len;
}

/* vub_panic_cb - library panic callback
 *
 * Invoked by libvhost-user-glib on unrecoverable errors. We log the message
 * and break the main loop so process can exit cleanly.
 */
static void vub_panic_cb(VuDev *vu_dev, const char *buf)
{
    VugDev *gdev;
    VubDev *vdev_blk;

    assert(vu_dev);

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);
    if (buf) {
        g_warning("vu_panic: %s", buf);
    }

    /* Stop the main loop to allow cleanup and process termination. */
    g_main_loop_quit(vdev_blk->loop);
}

/* vub_req_complete - push completed response back to guest
 *
 * - Adds 1 extra status byte to the length as required by virtio-blk protocol
 * - Notifies the guest about queue event
 * - Frees request helper structures
 */
static void vub_req_complete(VubReq *req)
{
    VugDev *gdev = &req->vdev_blk->parent;
    VuDev *vu_dev = &gdev->parent;

    /* IO size with 1 extra status byte */
    vu_queue_push(vu_dev, req->vq, req->elem,
                  req->size + 1);
    vu_queue_notify(vu_dev, req->vq);

    /* caller allocated elem & req with g_new0/g_free; free them */
    g_free(req->elem);
    g_free(req);
}

/* vub_open - open backing file / block device
 *
 * - file_name: path to file or block device
 * - wce: write cache enabled (if true, open without O_DIRECT to allow OS
 *        write caching behavior)
 *
 * Returns file descriptor on success or -1 on error.
 */
static int vub_open(const char *file_name, bool wce)
{
    int fd;
    int flags = O_RDWR;

    /* use O_DIRECT for direct I/O unless write-cache enabled requested */
    if (!wce) {
        flags |= O_DIRECT;
    }

    fd = open(file_name, flags);
    if (fd < 0) {
        fprintf(stderr, "Cannot open file %s, %s\n", file_name,
                strerror(errno));
        return -1;
    }

    return fd;
}

/* vub_readv - perform vectorized read at sector-aligned offset
 *
 * Uses preadv() so reads are performed at the given offset (sector_num * 512).
 * Returns number of bytes read or negative on error.
 */
static ssize_t
vub_readv(VubReq *req, struct iovec *iov, uint32_t iovcnt)
{
    VubDev *vdev_blk = req->vdev_blk;
    ssize_t rc;

    if (!iovcnt) {
        fprintf(stderr, "Invalid Read IOV count\n");
        return -1;
    }

    req->size = vub_iov_size(iov, iovcnt);
    rc = preadv(vdev_blk->blk_fd, iov, iovcnt, req->sector_num * 512);
    if (rc < 0) {
        fprintf(stderr, "%s, Sector %"PRIu64", Size %zu failed with %s\n",
                vdev_blk->blk_name, req->sector_num, req->size,
                strerror(errno));
        return -1;
    }

    return rc;
}

/* vub_writev - perform vectorized write at sector-aligned offset
 *
 * Uses pwritev() to write iovcnt segments at the given offset. Returns
 * number of bytes written or negative on error.
 */
static ssize_t
vub_writev(VubReq *req, struct iovec *iov, uint32_t iovcnt)
{
    VubDev *vdev_blk = req->vdev_blk;
    ssize_t rc;

    if (!iovcnt) {
        fprintf(stderr, "Invalid Write IOV count\n");
        return -1;
    }

    req->size = vub_iov_size(iov, iovcnt);
    rc = pwritev(vdev_blk->blk_fd, iov, iovcnt, req->sector_num * 512);
    if (rc < 0) {
        fprintf(stderr, "%s, Sector %"PRIu64", Size %zu failed with %s\n",
                vdev_blk->blk_name, req->sector_num, req->size,
                strerror(errno));
        return -1;
    }

    return rc;
}

/* vub_discard_write_zeroes - try to handle discard / write-zeroes requests
 *
 * This helper parses the discard/write-zeroes descriptor and attempts to
 * perform the operation via Linux-specific ioctls BLKDISCARD / BLKZEROOUT.
 * If the ioctl succeeds we return 0, otherwise -1 to indicate fallback is
 * necessary (or guest should get I/O error).
 *
 * Only compiled on Linux and when BLKDISCARD/BLKZEROOUT are available.
 */
static int
vub_discard_write_zeroes(VubReq *req, struct iovec *iov, uint32_t iovcnt,
                         uint32_t type)
{
    struct virtio_blk_discard_write_zeroes *desc;
    ssize_t size;
    void *buf;

    size = vub_iov_size(iov, iovcnt);
    if (size != sizeof(*desc)) {
        fprintf(stderr, "Invalid size %zd, expect %zd\n", size, sizeof(*desc));
        return -1;
    }
    buf = g_new0(char, size);
    vub_iov_to_buf(iov, iovcnt, buf);

    #if defined(__linux__) && defined(BLKDISCARD) && defined(BLKZEROOUT)
    VubDev *vdev_blk = req->vdev_blk;
    desc = buf;
    /* Convert sector/num_sectors to an (offset, length) pair (bytes) for ioctl */
    uint64_t range[2] = { le64_to_cpu(desc->sector) << 9,
                          (uint64_t)le32_to_cpu(desc->num_sectors) << 9 };
    if (type == VIRTIO_BLK_T_DISCARD) {
        if (ioctl(vdev_blk->blk_fd, BLKDISCARD, range) == 0) {
            g_free(buf);
            return 0;
        }
    } else if (type == VIRTIO_BLK_T_WRITE_ZEROES) {
        if (ioctl(vdev_blk->blk_fd, BLKZEROOUT, range) == 0) {
            g_free(buf);
            return 0;
        }
    }
    #endif

    g_free(buf);
    return -1;
}

/* vub_flush - flush backing device to stable storage.
 *
 * Uses fdatasync to attempt to flush file data. For block devices on Linux
 * this typically ensures data reaches stable storage.
 */
static void
vub_flush(VubReq *req)
{
    VubDev *vdev_blk = req->vdev_blk;

    fdatasync(vdev_blk->blk_fd);
}

/* vub_virtio_process_req - decode and perform a single virtio-blk request
 *
 * This function:
 *  1. Pops a descriptor element from the vhost queue.
 *  2. Validates headers (out_outhdr, in_inhdr).
 *  3. Handles read/write/flush/get_id/discard/write_zeroes operations.
 *  4. Completes the request and notifies the guest.
 *
 * Limitations & notes:
 *  - Does NOT support VIRTIO_F_ANY_LAYOUT (assumes traditional layout).
 *  - Expects at least one 'out' descriptor for the virtio_blk_outhdr and
 *    at least one 'in' descriptor for the status byte (inhdr).
 *  - Simplified error handling: on many failures we return -1 to stop processing.
 */
static int vub_virtio_process_req(VubDev *vdev_blk,
                                     VuVirtq *vq)
{
    VugDev *gdev = &vdev_blk->parent;
    VuDev *vu_dev = &gdev->parent;
    VuVirtqElement *elem;
    uint32_t type;
    unsigned in_num;
    unsigned out_num;
    VubReq *req;

    /* Pop one descriptor element from queue; caller will provide a sufficiently
     * sized buffer for VuVirtqElement + our VubReq. */
    elem = vu_queue_pop(vu_dev, vq, sizeof(VuVirtqElement) + sizeof(VubReq));
    if (!elem) {
        return -1;
    }

    /* Validate the basic virtio-blk header presence. hw/block/virtio_blk.c
     * ensures that a request contains at least an out header and an in
     * (status) byte. If missing, report and drop request. */
    if (elem->out_num < 1 || elem->in_num < 1) {
        fprintf(stderr, "virtio-blk request missing headers\n");
        g_free(elem);
        return -1;
    }

    req = g_new0(VubReq, 1);
    req->vdev_blk = vdev_blk;
    req->vq = vq;
    req->elem = elem;

    in_num = elem->in_num;
    out_num = elem->out_num;

    /* Don't support improved layout flag (VIRTIO_F_ANY_LAYOUT) in this sample.
     * Verify header sizes for expected positions:
     *  - out_sg[0] contains struct virtio_blk_outhdr
     *  - in_sg[in_num - 1] contains struct virtio_blk_inhdr (status byte)
     */
    if (elem->out_sg[0].iov_len < sizeof(struct virtio_blk_outhdr)) {
        fprintf(stderr, "Invalid outhdr size\n");
        goto err;
    }
    req->out = (struct virtio_blk_outhdr *)elem->out_sg[0].iov_base;
    out_num--;

    if (elem->in_sg[in_num - 1].iov_len < sizeof(struct virtio_blk_inhdr)) {
        fprintf(stderr, "Invalid inhdr size\n");
        goto err;
    }
    req->in = (struct virtio_blk_inhdr *)elem->in_sg[in_num - 1].iov_base;
    in_num--;

    type = le32_to_cpu(req->out->type);

    /* Mask out barrier flag - we don't treat it specially in this example */
    switch (type & ~VIRTIO_BLK_T_BARRIER) {
    case VIRTIO_BLK_T_IN:
    case VIRTIO_BLK_T_OUT: {
        ssize_t ret = 0;
        bool is_write = type & VIRTIO_BLK_T_OUT;
        req->sector_num = le64_to_cpu(req->out->sector);

        if (is_write) {
            /* Write: data is carried in out descriptors starting at out_sg[1] */
            ret  = vub_writev(req, &elem->out_sg[1], out_num);
        } else {
            /* Read: data is carried in in descriptors starting at in_sg[0] */
            ret = vub_readv(req, &elem->in_sg[0], in_num);
        }

        /* Set the status field seen by the guest */
        if (ret >= 0) {
            req->in->status = VIRTIO_BLK_S_OK;
        } else {
            req->in->status = VIRTIO_BLK_S_IOERR;
        }

        vub_req_complete(req);
        break;
    }
    case VIRTIO_BLK_T_FLUSH:
        /* Guest requests that data be flushed to stable storage */
        vub_flush(req);
        req->in->status = VIRTIO_BLK_S_OK;
        vub_req_complete(req);
        break;
    case VIRTIO_BLK_T_GET_ID: {
        /* Return a short identifier string to the guest; limit to buffer size */
        size_t size = MIN(vub_iov_size(&elem->in_sg[0], in_num),
                          VIRTIO_BLK_ID_BYTES);
        snprintf(elem->in_sg[0].iov_base, size, "%s", "vhost_user_blk");
        req->in->status = VIRTIO_BLK_S_OK;
        req->size = elem->in_sg[0].iov_len;
        vub_req_complete(req);
        break;
    }
    case VIRTIO_BLK_T_DISCARD:
    case VIRTIO_BLK_T_WRITE_ZEROES: {
        /* Try to perform efficient discard / write_zeroes via ioctl when
         * available. If unsupported/fails, report IOERR to guest. */
        int rc;
        rc = vub_discard_write_zeroes(req, &elem->out_sg[1], out_num, type);
        if (rc == 0) {
            req->in->status = VIRTIO_BLK_S_OK;
        } else {
            req->in->status = VIRTIO_BLK_S_IOERR;
        }
        vub_req_complete(req);
        break;
    }
    default:
        /* Unsupported operation type */
        req->in->status = VIRTIO_BLK_S_UNSUPP;
        vub_req_complete(req);
        break;
    }

    return 0;

err:
    /* Free on error and return -1 so caller stops processing for now */
    g_free(elem);
    g_free(req);
    return -1;
}

/* vub_process_vq - queue processing loop for a given virtqueue index
 *
 * This function is set as the queue handler. It continuously calls the
 * single-request processor until no more requests are available (or error).
 * For a high-performance implementation this could be run in a dedicated
 * thread and scaled to multiple queues.
 */
static void vub_process_vq(VuDev *vu_dev, int idx)
{
    VugDev *gdev;
    VubDev *vdev_blk;
    VuVirtq *vq;
    int ret;

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);
    assert(vdev_blk);

    vq = vu_get_queue(vu_dev, idx);
    assert(vq);

    while (1) {
        ret = vub_virtio_process_req(vdev_blk, vq);
        if (ret) {
            break;
        }
    }
}

/* vub_queue_set_started - called when a virtqueue transitions started/stopped
 *
 * When started==true we set vub_process_vq as the handler for the queue so
 * requests will be processed. When false, we remove the handler.
 */
static void vub_queue_set_started(VuDev *vu_dev, int idx, bool started)
{
    VuVirtq *vq;

    assert(vu_dev);

    vq = vu_get_queue(vu_dev, idx);
    vu_set_queue_handler(vu_dev, vq, started ? vub_process_vq : NULL);
}

/* vub_get_features - report virtio device features supported by this backend
 *
 * This constructs a feature bitmask that mirrors hw/block/virtio-blk.c
 * capabilities that this sample supports. Feature bits must match guest
 * expectations.
 */
static uint64_t
vub_get_features(VuDev *dev)
{
    uint64_t features;
    VugDev *gdev;
    VubDev *vdev_blk;

    gdev = container_of(dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);

    features = 1ull << VIRTIO_BLK_F_SIZE_MAX |
               1ull << VIRTIO_BLK_F_SEG_MAX |
               1ull << VIRTIO_BLK_F_TOPOLOGY |
               1ull << VIRTIO_BLK_F_BLK_SIZE |
               1ull << VIRTIO_BLK_F_FLUSH |
               #if defined(__linux__) && defined(BLKDISCARD) && defined(BLKZEROOUT)
               1ull << VIRTIO_BLK_F_DISCARD |
               1ull << VIRTIO_BLK_F_WRITE_ZEROES |
               #endif
               1ull << VIRTIO_BLK_F_CONFIG_WCE;

    if (vdev_blk->enable_ro) {
        features |= 1ull << VIRTIO_BLK_F_RO;
    }

    return features;
}

/* vub_get_protocol_features - report vhost-user protocol features used */
static uint64_t
vub_get_protocol_features(VuDev *dev)
{
    return 1ull << VHOST_USER_PROTOCOL_F_CONFIG |
           1ull << VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD;
}

/* vub_get_config - copy the device config (virtio_blk_config) to the buffer
 *
 * This function is invoked by libvhost-user-glib when the master (QEMU) requests
 * the device configuration space. We guard the length and copy only the
 * requested bytes.
 */
static int
vub_get_config(VuDev *vu_dev, uint8_t *config, uint32_t len)
{
    VugDev *gdev;
    VubDev *vdev_blk;

    if (len > sizeof(struct virtio_blk_config)) {
        return -1;
    }

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);
    memcpy(config, &vdev_blk->blkcfg, len);

    return 0;
}

/* vub_set_config - update configuration sent from the frontend (QEMU)
 *
 * The sample only supports updating the write caching enable (wce) byte and
 * only as a frontend configuration change (not live migration). On change,
 * we re-open the backing file with the new O_DIRECT vs non-O_DIRECT policy.
 */
static int
vub_set_config(VuDev *vu_dev, const uint8_t *data,
               uint32_t offset, uint32_t size, uint32_t flags)
{
    VugDev *gdev;
    VubDev *vdev_blk;
    uint8_t wce;
    int fd;

    /* don't support live migration configuration updates in this example */
    if (flags != VHOST_SET_CONFIG_TYPE_FRONTEND) {
        return -1;
    }

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);

    /* Only accept writes to the 'wce' byte in virtio_blk_config */
    if (offset != offsetof(struct virtio_blk_config, wce) ||
        size != 1) {
        return -1;
    }

    wce = *data;
    if (wce == vdev_blk->blkcfg.wce) {
        /* Do nothing as same with old configuration */
        return 0;
    }

    vdev_blk->blkcfg.wce = wce;
    fprintf(stdout, "Write Cache Policy Changed\n");
    if (vdev_blk->blk_fd >= 0) {
        close(vdev_blk->blk_fd);
        vdev_blk->blk_fd = -1;
    }

    fd = vub_open(vdev_blk->blk_name, wce);
    if (fd < 0) {
        fprintf(stderr, "Error to open block device %s\n", vdev_blk->blk_name);
        vdev_blk->blk_fd = -1;
        return -1;
    }
    vdev_blk->blk_fd = fd;

    return 0;
}

/* Interface structure used by libvhost-user-glib to call into this backend */
static const VuDevIface vub_iface = {
    .get_features = vub_get_features,
    .queue_set_started = vub_queue_set_started,
    .get_protocol_features = vub_get_protocol_features,
    .get_config = vub_get_config,
    .set_config = vub_set_config,
};

/* unix_sock_new - create a UNIX-domain listening socket at path unix_fn
 *
 * Returns the listening socket fd or -1 on error. The function unlinks any
 * existing socket at the path before binding.
 */
static int unix_sock_new(char *unix_fn)
{
    int sock;
    struct sockaddr_un un;

    assert(unix_fn);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    un.sun_family = AF_UNIX;
    (void)snprintf(un.sun_path, sizeof(un.sun_path), "%s", unix_fn);

    (void)unlink(unix_fn);
    if (bind(sock, (struct sockaddr *)&un, sizeof(un)) < 0) {
        perror("bind");
        goto fail;
    }

    if (listen(sock, 1) < 0) {
        perror("listen");
        goto fail;
    }

    return sock;

fail:
    (void)close(sock);

    return -1;
}

/* vub_free - release VubDev resources
 *
 * Note: mainloop and blk_fd are cleaned up here. Do not call after freeing
 * any contained objects or double-free will occur.
 */
static void vub_free(struct VubDev *vdev_blk)
{
    if (!vdev_blk) {
        return;
    }

    g_main_loop_unref(vdev_blk->loop);
    if (vdev_blk->blk_fd >= 0) {
        close(vdev_blk->blk_fd);
    }
    g_free(vdev_blk);
}

/* vub_get_blocksize - try to obtain device blocksize via ioctl
 *
 * Returns the blocksize (default 512 bytes if ioctl is not available).
 */
static uint32_t
vub_get_blocksize(int fd)
{
    uint32_t blocksize = 512;

#if defined(__linux__) && defined(BLKSSZGET)
    if (ioctl(fd, BLKSSZGET, &blocksize) == 0) {
        return blocksize;
    }
#endif

    return blocksize;
}

/* vub_initialize_config - populate virtio_blk_config using fd parameters
 *
 * Fills out capacity (in 512-byte sectors), block size and other config
 * fields used to advertise device layout to the guest.
 *
 * Note: this example uses sensible defaults for size_max, seg_max, etc.
 * A production backend might want to derive these from device capabilities
 * or expose them as CLI options.
 */
static void
vub_initialize_config(int fd, struct virtio_blk_config *config)
{
    off_t capacity;

    capacity = lseek(fd, 0, SEEK_END);
    config->capacity = capacity >> 9;
    config->blk_size = vub_get_blocksize(fd);
    config->size_max = 65536;
    config->seg_max = 128 - 2;
    config->min_io_size = 1;
    config->opt_io_size = 1;
    config->num_queues = 1;
    #if defined(__linux__) && defined(BLKDISCARD) && defined(BLKZEROOUT)
    config->max_discard_sectors = 32768;
    config->max_discard_seg = 1;
    config->discard_sector_alignment = config->blk_size >> 9;
    config->max_write_zeroes_sectors = 32768;
    config->max_write_zeroes_seg = 1;
    #endif
}

/* vub_new - create and initialize VubDev for given backing file
 *
 * - blk_file: path to the file/block device to use as backing storage.
 * Returns allocated VubDev on success or NULL on failure.
 */
static VubDev *
vub_new(char *blk_file)
{
    VubDev *vdev_blk;

    vdev_blk = g_new0(VubDev, 1);
    vdev_blk->loop = g_main_loop_new(NULL, FALSE);
    vdev_blk->blk_fd = vub_open(blk_file, 0);
    if (vdev_blk->blk_fd  < 0) {
        fprintf(stderr, "Error to open block device %s\n", blk_file);
        vub_free(vdev_blk);
        return NULL;
    }
    vdev_blk->enable_ro = false;
    vdev_blk->blkcfg.wce = 0;
    vdev_blk->blk_name = blk_file;

    /* fill virtio_blk_config with block parameters */
    vub_initialize_config(vdev_blk->blk_fd, &vdev_blk->blkcfg);

    return vdev_blk;
}

/* ------------------------------------------------------------------------- */
/* CLI/option parsing and main flow                                          */
/* ------------------------------------------------------------------------- */

/* Global CLI option variables (simple example) */
static int opt_fdnum = -1;
static char *opt_socket_path;
static char *opt_blk_file;
static gboolean opt_print_caps;
static gboolean opt_read_only;

/* GOption entries for program options. When contributing to QEMU, try to
 * follow QEMU CLI style and options conventions (this sample uses GLib options
 * for simplicity). */
static GOptionEntry entries[] = {
    { "print-capabilities", 'c', 0, G_OPTION_ARG_NONE, &opt_print_caps,
      "Print capabilities", NULL },
    { "fd", 'f', 0, G_OPTION_ARG_INT, &opt_fdnum,
      "Use inherited fd socket", "FDNUM" },
    { "socket-path", 's', 0, G_OPTION_ARG_FILENAME, &opt_socket_path,
      "Use UNIX socket path", "PATH" },
    {"blk-file", 'b', 0, G_OPTION_ARG_FILENAME, &opt_blk_file,
     "block device or file path", "PATH"},
    { "read-only", 'r', 0, G_OPTION_ARG_NONE, &opt_read_only,
      "Enable read-only", NULL },
    { NULL, },
};

/* main - program entry point
 *
 * Behavior:
 *  - Parse options
 *  - Print capabilities JSON and exit if requested
 *  - Create a listening unix socket or use inherited socket fd
 *  - Accept a single vhost-user connection, create VubDev for blk-file, and
 *    initialize libvhost-user-glib with the vub_iface callbacks.
 *  - Run a GLib mainloop until termination (e.g., peer disconnect or panic)
 *
 * Notes for reviewers:
 *  - This example accepts a single connection via accept(). To make it handle
 *    multiple connections or to integrate tightly with QEMU's mainloop, adapt
 *    accordingly.
 *  - Before merging into QEMU tree, prefer aligning option parsing with QEMU's
 *    existing helpers and coding style (indent, header ordering, etc.).
 */
int main(int argc, char **argv)
{
    int lsock = -1, csock = -1;
    VubDev *vdev_blk = NULL;
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("Option parsing failed: %s\n", error->message);
        exit(EXIT_FAILURE);
    }
    if (opt_print_caps) {
        /* This JSON is useful for higher-level tools to discover what this
         * backend supports. Keep it stable if external tooling depends on it. */
        g_print("{\n");
        g_print("  \"type\": \"block\",\n");
        g_print("  \"features\": [\n");
        g_print("    \"read-only\",\n");
        g_print("    \"blk-file\"\n");
        g_print("  ]\n");
        g_print("}\n");
        exit(EXIT_SUCCESS);
    }

    if (!opt_blk_file) {
        g_print("%s\n", g_option_context_get_help(context, true, NULL));
        exit(EXIT_FAILURE);
    }

    if (opt_socket_path) {
        lsock = unix_sock_new(opt_socket_path);
        if (lsock < 0) {
            exit(EXIT_FAILURE);
        }
    } else if (opt_fdnum < 0) {
        /* require either socket path or inherited fd from parent */
        g_print("%s\n", g_option_context_get_help(context, true, NULL));
        exit(EXIT_FAILURE);
    } else {
        lsock = opt_fdnum;
    }

    /* Accept the single incoming connection from the vhost-user master
     * (typically QEMU). For more robust servers, consider handling EINTR,
     * implementing a timeout, and supporting multiple peers. */
    csock = accept(lsock, NULL, NULL);
    if (csock < 0) {
        g_printerr("Accept error %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    vdev_blk = vub_new(opt_blk_file);
    if (!vdev_blk) {
        exit(EXIT_FAILURE);
    }
    if (opt_read_only) {
        vdev_blk->enable_ro = true;
    }

    /* Initialize libvhost-user-glib to handle the vhost-user protocol using
     * our callbacks (vub_iface). The number of queues is limited by our sample
     * constant above. */
    if (!vug_init(&vdev_blk->parent, VHOST_USER_BLK_MAX_QUEUES, csock,
                  vub_panic_cb, &vub_iface)) {
        g_printerr("Failed to initialize libvhost-user-glib\n");
        exit(EXIT_FAILURE);
    }

    /* Run the GLib main loop until vub_panic_cb quits it or the connection
     * closes. After the loop exits perform cleanup. */
    g_main_loop_run(vdev_blk->loop);
    g_main_loop_unref(vdev_blk->loop);
    g_option_context_free(context);
    vug_deinit(&vdev_blk->parent);
    vub_free(vdev_blk);
    if (csock >= 0) {
        close(csock);
    }
    if (lsock >= 0) {
        close(lsock);
    }
    g_free(opt_socket_path);
    g_free(opt_blk_file);

    return 0;
}
