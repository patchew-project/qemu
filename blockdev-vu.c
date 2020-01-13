#include "qemu/osdep.h"
#include "block/vhost-user.h"
#include "qapi/error.h"
#include "qapi/qapi-types-sockets.h"
#include "qapi/qapi-commands-block.h"

#include "sysemu/block-backend.h"
#include "qemu/main-loop.h"

#include "qemu/units.h"

#include "block/block.h"

#include "qom/object_interfaces.h"

#include <sys/eventfd.h>

#include "hw/qdev-properties.h"
enum {
    VHOST_USER_BLK_MAX_QUEUES = 8,
};

struct virtio_blk_inhdr {
    unsigned char status;
};


static QTAILQ_HEAD(, VubDev) vub_devs = QTAILQ_HEAD_INITIALIZER(vub_devs);


typedef struct VubReq {
    VuVirtqElement *elem;
    int64_t sector_num;
    size_t size;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr out;
    VuClient *client;
    struct VuVirtq *vq;
} VubReq;

static void
remove_watch(VuDev *vu_dev, int fd)
{
    VuClient *client;

    g_assert(vu_dev);
    g_assert(fd >= 0);

    client = container_of(vu_dev, VuClient, parent);
    aio_set_fd_handler(client->blk->ctx, fd, false, NULL, NULL, NULL, NULL);
}

static void close_client(VuClient *client)
{
    vu_deinit(&client->parent);
    /** g_source_destroy(vub_device->parent.src); */
    client->sioc = NULL;
    object_unref(OBJECT(client->ioc));
    client->closed = true;

}

static void vub_panic_cb(VuDev *vu_dev, const char *buf)
{
    if (buf) {
        g_warning("vu_panic: %s", buf);
    }

    VuClient *client = container_of(vu_dev, VuClient, parent);
    if (client->blk->exit_panic) {
        client->blk->close = true;
    }
    if (!client->closed) {
        close_client(client);
    }
}


static void vub_req_complete(VubReq *req)
{
    VuDev *vu_dev = &req->client->parent;

    /* IO size with 1 extra status byte */
    vu_queue_push(vu_dev, req->vq, req->elem,
                  req->size + 1);
    vu_queue_notify(vu_dev, req->vq);

    if (req->elem) {
        free(req->elem);
    }

    g_free(req);
}



static int
vub_discard_write_zeroes(VubReq *req, struct iovec *iov, uint32_t iovcnt,
                         uint32_t type)
{
    struct virtio_blk_discard_write_zeroes *desc;
    ssize_t size;
    void *buf;

    size = iov_size(iov, iovcnt);
    if (size != sizeof(*desc)) {
        fprintf(stderr, "Invalid size %ld, expect %ld\n", size, sizeof(*desc));
        return -1;
    }
    buf = g_new0(char, size);

    iov_to_buf_full(iov, iovcnt, 0, buf, size);


    #if defined(__linux__) && defined(BLKDISCARD) && defined(BLKZEROOUT)
    VubDev *vdev_blk;
    VuClient *client = container_of(dev, VuClient, parent);
    vdev_blk = client->blk;
    desc = (struct virtio_blk_discard_write_zeroes *)buf;
    uint64_t range[2] = { le64toh(desc->sector) << 9,
                          le32toh(desc->num_sectors) << 9 };
    if (type == VIRTIO_BLK_T_DISCARD) {
        if (blk_pdiscard(vdev_blk->blk, range[0], range[1]) == 0) {
            g_free(buf);
            return 0;
        }
    } else if (type == VIRTIO_BLK_T_WRITE_ZEROES) {
        if (blk_pwrite_zeroes(vdev_blk->blk, range[0], range[1]) == 0) {
            g_free(buf);
            return 0;
        }
    }
    #endif

    g_free(buf);
    return -1;
}


static void
vub_flush(VubReq *req)
{
    VuClient *client = req->client;
    blk_co_flush(client->blk->backend);
}


#define NOT_DONE 0x7fffffff /* used while emulated sync operation in progress */
typedef struct BlkRwCo {
    BlockBackend *blk;
    int64_t offset;
    void *iobuf;
    int ret;
    BdrvRequestFlags flags;
} BlkRwCo;

static void blk_read_entry(void *opaque)
{
    BlkRwCo *rwco = opaque;
    QEMUIOVector *qiov = rwco->iobuf;

    rwco->ret = blk_co_preadv(rwco->blk, rwco->offset, qiov->size,
                              qiov, rwco->flags);
    aio_wait_kick();
}


static void blk_write_entry(void *opaque)
{
    BlkRwCo *rwco = opaque;
    QEMUIOVector *qiov = rwco->iobuf;

    rwco->ret = blk_co_pwritev(rwco->blk, rwco->offset, qiov->size,
                              qiov, rwco->flags);
    aio_wait_kick();
}


static int blk_prw(BlockBackend *blk, QEMUIOVector *qiov, int64_t offset,
                   CoroutineEntry co_entry, BdrvRequestFlags flags)
{

    BlkRwCo rwco = {
        .blk    = blk,
        .offset = offset,
        .iobuf  = qiov,
        .flags  = flags,
        .ret    = NOT_DONE,
    };

    if (qemu_in_coroutine()) {
        /* Fast-path if already in coroutine context */
        co_entry(&rwco);
    } else {
        Coroutine *co = qemu_coroutine_create(co_entry, &rwco);
        bdrv_coroutine_enter(blk_bs(blk), co);
        BDRV_POLL_WHILE(blk_bs(blk), rwco.ret == NOT_DONE);
    }

    return rwco.ret;
}


static ssize_t
vub_rwv(VubReq *req, struct iovec *iov,
        uint32_t iovcnt,
        CoroutineEntry co_entry)
{
    VuClient *client = req->client;
    ssize_t rc;

    if (!iovcnt) {
        fprintf(stderr, "Invalid Read/Write IOV count\n");
        return -1;
    }

    int64_t offset = req->sector_num * 512;
    QEMUIOVector *qiov = g_new0(QEMUIOVector, 1);
    qemu_iovec_init_external(qiov, iov, iovcnt);
    rc = blk_prw(client->blk->backend, qiov, offset, co_entry, 0);

    req->size = iov_size(iov, iovcnt);
    if (rc < 0) {
        fprintf(stderr, "%s, Sector %"PRIu64", Size %lu failed with %s\n",
                client->blk->name, req->sector_num, req->size,
                strerror(errno));
        return -1;
    }

    return rc;
}

static int vub_virtio_process_req(VuClient *client,
                                     VuVirtq *vq)
{
    VuDev *vu_dev = &client->parent;
    VuVirtqElement *elem;
    uint32_t type;
    VubReq *req;

    elem = vu_queue_pop(vu_dev, vq, sizeof(VuVirtqElement) + sizeof(VubReq));
    if (!elem) {
        return -1;
    }

    struct iovec *in_iov = elem->in_sg;
    struct iovec *out_iov = elem->out_sg;
    unsigned in_num = elem->in_num;
    unsigned out_num = elem->out_num;
    /* refer to hw/block/virtio_blk.c */
    if (elem->out_num < 1 || elem->in_num < 1) {
        fprintf(stderr, "virtio-blk request missing headers\n");
        free(elem);
        return -1;
    }

    req = g_new0(VubReq, 1);
    req->client = client;
    req->vq = vq;
    req->elem = elem;

    if (unlikely(iov_to_buf(out_iov, out_num, 0, &req->out,
                            sizeof(req->out)) != sizeof(req->out))) {
        fprintf(stderr, "virtio-blk request outhdr too short");
        goto err;
    }

    iov_discard_front(&out_iov, &out_num, sizeof(req->out));

    if (in_iov[in_num - 1].iov_len < sizeof(struct virtio_blk_inhdr)) {
        fprintf(stderr, "virtio-blk request inhdr too short");
        goto err;
    }

    /* We always touch the last byte, so just see how big in_iov is.  */
    req->in = (void *)in_iov[in_num - 1].iov_base
              + in_iov[in_num - 1].iov_len
              - sizeof(struct virtio_blk_inhdr);
    iov_discard_back(in_iov, &in_num, sizeof(struct virtio_blk_inhdr));


    type = le32toh(req->out.type);
    switch (type & ~VIRTIO_BLK_T_BARRIER) {
    case VIRTIO_BLK_T_IN:
    case VIRTIO_BLK_T_OUT: {
        ssize_t ret = 0;
        bool is_write = type & VIRTIO_BLK_T_OUT;
        req->sector_num = le64toh(req->out.sector);
        if (is_write) {
            ret = vub_rwv(req, out_iov, out_num, blk_write_entry);
        } else {
            ret = vub_rwv(req, in_iov, in_num, blk_read_entry);
        }
        if (ret >= 0) {
            req->in->status = VIRTIO_BLK_S_OK;
        } else {
            req->in->status = VIRTIO_BLK_S_IOERR;
        }
        vub_req_complete(req);
        break;
    }
    case VIRTIO_BLK_T_FLUSH:
        vub_flush(req);
        req->in->status = VIRTIO_BLK_S_OK;
        vub_req_complete(req);
        break;
    case VIRTIO_BLK_T_GET_ID: {
        size_t size = MIN(iov_size(&elem->in_sg[0], in_num),
                          VIRTIO_BLK_ID_BYTES);
        snprintf(elem->in_sg[0].iov_base, size, "%s", "vhost_user_blk");
        req->in->status = VIRTIO_BLK_S_OK;
        req->size = elem->in_sg[0].iov_len;
        vub_req_complete(req);
        break;
    }
    case VIRTIO_BLK_T_DISCARD:
    case VIRTIO_BLK_T_WRITE_ZEROES: {
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
        req->in->status = VIRTIO_BLK_S_UNSUPP;
        vub_req_complete(req);
        break;
    }

    return 0;

err:
    free(elem);
    g_free(req);
    return -1;
}


static void vub_process_vq(VuDev *vu_dev, int idx)
{
    VuClient *client;
    VuVirtq *vq;
    int ret;

    client = container_of(vu_dev, VuClient, parent);
    assert(client);

    vq = vu_get_queue(vu_dev, idx);
    assert(vq);

    while (1) {
        ret = vub_virtio_process_req(client, vq);
        if (ret) {
            break;
        }
    }
}


static void vub_queue_set_started(VuDev *vu_dev, int idx, bool started)
{
    VuVirtq *vq;

    assert(vu_dev);

    vq = vu_get_queue(vu_dev, idx);
    vu_set_queue_handler(vu_dev, vq, started ? vub_process_vq : NULL);
}

static uint64_t
vub_get_features(VuDev *dev)
{
    uint64_t features;
    VubDev *vdev_blk;

    VuClient *client = container_of(dev, VuClient, parent);
    vdev_blk = client->blk;

    features = 1ull << VIRTIO_BLK_F_SIZE_MAX |
               1ull << VIRTIO_BLK_F_SEG_MAX |
               1ull << VIRTIO_BLK_F_TOPOLOGY |
               1ull << VIRTIO_BLK_F_BLK_SIZE |
               1ull << VIRTIO_BLK_F_FLUSH |
               #if defined(__linux__) && defined(BLKDISCARD) && defined(BLKZEROOUT)
               1ull << VIRTIO_BLK_F_DISCARD |
               1ull << VIRTIO_BLK_F_WRITE_ZEROES |
               #endif
               1ull << VIRTIO_BLK_F_CONFIG_WCE |
               1ull << VIRTIO_F_VERSION_1 |
               1ull << VIRTIO_RING_F_INDIRECT_DESC |
               1ull << VIRTIO_RING_F_EVENT_IDX |
               1ull << VHOST_USER_F_PROTOCOL_FEATURES;

    if (!vdev_blk->writable) {
        features |= 1ull << VIRTIO_BLK_F_RO;
    }

    return features;
}

static uint64_t
vub_get_protocol_features(VuDev *dev)
{
    return 1ull << VHOST_USER_PROTOCOL_F_CONFIG |
           1ull << VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD;
}

static int
vub_get_config(VuDev *vu_dev, uint8_t *config, uint32_t len)
{
    VubDev *vdev_blk;

    VuClient *client = container_of(vu_dev, VuClient, parent);
    vdev_blk = client->blk;
    memcpy(config, &vdev_blk->blkcfg, len);

    return 0;
}

static int
vub_set_config(VuDev *vu_dev, const uint8_t *data,
               uint32_t offset, uint32_t size, uint32_t flags)
{
    VubDev *vdev_blk;

    VuClient *client = container_of(vu_dev, VuClient, parent);
    vdev_blk = client->blk;
    uint8_t wce;

    /* don't support live migration */
    if (flags != VHOST_SET_CONFIG_TYPE_MASTER) {
        return -1;
    }


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
    blk_set_enable_write_cache(vdev_blk->backend, true);
    return 0;
}


/*
 * When the client disconnects, it send a VHOST_USER_NONE request
 * and vu_process_message will simple call exit which cause the VM
 * to exit abruptly.
 * To avoid this issue,  process VHOST_USER_NONE request ahead
 * of vu_process_message.
 *
 */
static int vub_process_msg(VuDev *dev, VhostUserMsg *vmsg, int *do_reply)
{
    if (vmsg->request == VHOST_USER_NONE) {
        dev->panic(dev, "disconnect");
        return true;
    }
    return false;
}

static void
vmsg_close_fds(VhostUserMsg *vmsg)
{
    int i;
    for (i = 0; i < vmsg->fd_num; i++) {
        close(vmsg->fds[i]);
    }
}

static bool
vu_message_read_co(VuDev *vu_dev, int conn_fd, VhostUserMsg *vmsg)
{
    char control[CMSG_SPACE(VHOST_MEMORY_MAX_NREGIONS * sizeof(int))] = { };
    struct iovec iov = {
        .iov_base = (char *)vmsg,
        .iov_len = VHOST_USER_HDR_SIZE,
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = sizeof(control),
    };
    size_t fd_size;
    struct cmsghdr *cmsg;
    int rc;
    char buffer[100];
    VuClient *client = container_of(vu_dev, VuClient, parent);
    QIOChannel *ioc = client->ioc;
    do {
        rc = recvmsg(conn_fd, &msg, 0);
        if (rc < 0) {
            if (errno == EAGAIN) {
                if (qemu_in_coroutine()) {
                    qio_channel_yield(ioc, G_IO_IN);
                } else {
                    qio_channel_wait(ioc, G_IO_IN);
                }
                continue;
            } else if (errno == EINTR) {
                continue;
            }
        }
        break;
    } while (true);

    if (rc < 0) {
        sprintf(buffer, "Error while recvmsg: %s", strerror(errno));
        vub_panic_cb(vu_dev, buffer);
        return false;
    }

    assert(rc == VHOST_USER_HDR_SIZE || rc == 0);

    vmsg->fd_num = 0;
    for (cmsg = CMSG_FIRSTHDR(&msg);
         cmsg != NULL;
         cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            fd_size = cmsg->cmsg_len - CMSG_LEN(0);
            vmsg->fd_num = fd_size / sizeof(int);
            memcpy(vmsg->fds, CMSG_DATA(cmsg), fd_size);
            break;
        }
    }

    if (vmsg->size > sizeof(vmsg->payload)) {
        sprintf(buffer,
                "Error: too big message request: %d, size: vmsg->size: %u, "
                "while sizeof(vmsg->payload) = %zu\n",
                vmsg->request, vmsg->size, sizeof(vmsg->payload));
        vub_panic_cb(vu_dev, buffer);
        goto fail;
    }

    if (vmsg->size) {
        do {
            rc = read(conn_fd, &vmsg->payload, vmsg->size);
            if (rc < 0) {
                if (errno == EAGAIN) {
                    if (qemu_in_coroutine()) {
                        qio_channel_yield(ioc, G_IO_IN);
                    } else {
                        qio_channel_wait(ioc, G_IO_IN);
                    }
                    continue;
                } else if (errno == EINTR) {
                    continue;
                }
            }
            break;
        } while (true);

        if (rc <= 0) {
            sprintf(buffer, "Error while reading: %s", strerror(errno));
            vub_panic_cb(vu_dev, buffer);
            goto fail;
        }

        assert(rc == vmsg->size);
    }

    return true;

fail:
    vmsg_close_fds(vmsg);

    return false;
}

static void vub_kick_cb(void *opaque)
{
    vu_watch_cb_data *data = (vu_watch_cb_data *) opaque;
    int index = data->index;
    VuDev *dev = data->vu_dev;
    VuVirtq *vq = &dev->vq[index];
    int sock = vq->kick_fd;
    eventfd_t kick_data;
    ssize_t rc;

    rc = eventfd_read(sock, &kick_data);
    if (rc == -1) {
        char buffer[100];
        sprintf(buffer, "kick eventfd_read(): %s", strerror(errno));
        vub_panic_cb(dev, buffer);
        g_free(data);
        dev->remove_watch(dev, dev->vq[index].kick_fd);
    } else {
        if (vq->handler) {
            vq->handler(dev, index);
        }
    }
}

static const VuDevIface vub_iface = {
    .get_features = vub_get_features,
    .queue_set_started = vub_queue_set_started,
    .get_protocol_features = vub_get_protocol_features,
    .get_config = vub_get_config,
    .set_config = vub_set_config,
    .process_msg = vub_process_msg,
    .read_msg = vu_message_read_co,
    .kick_callback = vub_kick_cb,
};


void vub_free(VubDev *vub_dev, bool called_by_QOM)
{
    if (!vub_dev) {
        return;
    }

    blk_unref(vub_dev->backend);
    g_free(vub_dev->name);
    g_free(vub_dev->unix_socket);

    if (vub_dev->next.tqe_circ.tql_prev) {
        /*
         * if vub_dev->next.tqe_circ.tql_prev = null,
         * vub_dev hasn't been inserted into the queue and
         * vub_free is called by obj->instance_finalize.
         */
        QTAILQ_REMOVE(&vub_devs, vub_dev, next);
    }
    /*
     * Needn't to free vub_dev if called by QOM
     * because QOM will do the clean-up work.
     */
    if (!called_by_QOM) {
        g_free(vub_dev);
    }
}

static coroutine_fn void vu_client_trip(void *opaque)
{
    VuClient *client = opaque;

    while (!client->closed) {
        vu_dispatch(&client->parent);
    }

    QTAILQ_REMOVE(&client->blk->clients, client, next);

}

static void vu_client_start(VuClient *client)
{
    Coroutine *co = qemu_coroutine_create(vu_client_trip, client);
    qemu_coroutine_enter(co);
}


G_STATIC_ASSERT((int)G_IO_IN == (int)VU_WATCH_IN);
G_STATIC_ASSERT((int)G_IO_OUT == (int)VU_WATCH_OUT);
G_STATIC_ASSERT((int)G_IO_PRI == (int)VU_WATCH_PRI);
G_STATIC_ASSERT((int)G_IO_ERR == (int)VU_WATCH_ERR);
G_STATIC_ASSERT((int)G_IO_HUP == (int)VU_WATCH_HUP);

static void
set_watch(VuDev *vu_dev, int fd, int vu_evt,
          vu_watch_cb_packed_data cb, void *pvt)
{
    /*
     * since aio_dispatch can only pass one user data pointer to the
     * callback function, pack VuDev, pvt into a struct
     */
    VuClient *client;

    g_assert(vu_dev);
    g_assert(fd >= 0);
    g_assert(cb);
    client = container_of(vu_dev, VuClient, parent);
    vu_watch_cb_data *cb_data = g_new0(vu_watch_cb_data, 1);
    cb_data->index = (intptr_t) pvt;
    cb_data->vu_dev = vu_dev;
    aio_set_fd_handler(client->blk->ctx, fd, false, (void *) cb,
                       NULL, NULL, cb_data);
}


void vub_accept(QIONetListener *listener, QIOChannelSocket *sioc,
                gpointer opaque)
{
    VuClient *client;
    VubDev *vub_device = opaque;
    client = g_new0(VuClient, 1);

    if (!vu_init_packed_data(&client->parent, VHOST_USER_BLK_MAX_QUEUES,
                             sioc->fd, vub_panic_cb, set_watch,
                             remove_watch, &vub_iface)) {
        fprintf(stderr, "Failed to initialized libvhost-user\n");
        g_free(client);
        return;
    }

    client->blk = vub_device;
    client->refcount = 1;
    client->sioc = sioc;
    /*
     * increase the object reference, so cioc will not freed by
     * qio_net_listener_channel_func which will call object_unref(OBJECT(sioc))
     */
    object_ref(OBJECT(client->sioc));
    qio_channel_set_name(QIO_CHANNEL(sioc), "vhost-user client");
    client->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(client->ioc));
    object_ref(OBJECT(sioc));

    qio_channel_set_blocking(QIO_CHANNEL(client->sioc), false, NULL);
    client->closed = false;
    QTAILQ_INSERT_TAIL(&client->blk->clients, client, next);
    vu_client_start(client);
}


void
vub_initialize_config(BlockDriverState *bs, struct virtio_blk_config *config)
{
    config->capacity = bdrv_getlength(bs) >> BDRV_SECTOR_BITS;
    config->blk_size = BDRV_SECTOR_SIZE;
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


static VubDev *vub_new(VubDev *vub_device, const char *name,
                       const char *unix_socket, bool writable, Error **errp)
{

    BlockBackend *blk;

    /*
     * Don't allow resize while the vhost user server is running,
     * otherwise we don't care what happens with the node.
     */
    uint64_t perm = BLK_PERM_CONSISTENT_READ;
    int ret;

    AioContext *ctx;

    BlockDriverState *bs = bdrv_lookup_bs(name,
                                          name,
                                          errp);

    if (!bs) {
        error_setg(errp,
                   "No drive with name '%s'."
                   " Please find the list of names with "
                   "'info block'", name);
        return NULL;
    }

    if (bdrv_is_read_only(bs)) {
        writable = false;
    }

    if (writable) {
        perm |= BLK_PERM_WRITE;
    }

    ctx = bdrv_get_aio_context(bs);
    aio_context_acquire(ctx);
    bdrv_invalidate_cache(bs, NULL);
    aio_context_release(ctx);

    blk = blk_new(bdrv_get_aio_context(bs), perm,
                  BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                  BLK_PERM_WRITE | BLK_PERM_GRAPH_MOD);
    ret = blk_insert_bs(blk, bs, errp);

    if (ret < 0) {
        goto fail;
    }


    blk_set_enable_write_cache(blk, false);

    blk_set_allow_aio_context_change(blk, true);


    vub_device->name = g_strdup(name);
    vub_device->unix_socket = g_strdup(unix_socket);
    vub_device->writable = writable;
    vub_device->blkcfg.wce = 0;
    vub_device->backend = blk;
    vub_device->ctx = ctx;
    vub_initialize_config(bs, &vub_device->blkcfg);
    return vub_device;

fail:
    blk_unref(blk);
    return NULL;
}

void vhost_user_server_free(VubDev *vub_device, bool called_by_QOM)
{
    if (!vub_device) {
        return;
    }

    VuClient *client, *next;
    QTAILQ_FOREACH_SAFE(client, &vub_device->clients, next, next) {
        if (!client->closed) {
            close_client(client);
        }
    }

    if (vub_device->listener) {
        qio_net_listener_disconnect(vub_device->listener);
        object_unref(OBJECT(vub_device->listener));
    }
    vub_free(vub_device, called_by_QOM);

}


VubDev *vub_dev_find(const char *name)
{
    VubDev *vub_device;
    QTAILQ_FOREACH(vub_device, &vub_devs, next) {
        if (strcmp(name, vub_device->name) == 0) {
            return vub_device;
        }
    }

    return NULL;
}


static VubDev *vub_dev_find_by_unix_socket(const char *unix_socket)
{
    VubDev *vub_device;
    QTAILQ_FOREACH(vub_device, &vub_devs, next) {
        if (strcmp(unix_socket, vub_device->unix_socket) == 0) {
            return vub_device;
        }
    }

    return NULL;
}

static void vhost_user_server_start(VubDev *vub_device, const char *unix_socket,
                                    const char *name, bool writable,
                                    Error **errp)
{

    if (vub_dev_find(name) || vub_dev_find_by_unix_socket(unix_socket)) {
        error_setg(errp, "Vhost user server with name '%s' or "
                "with socket_path '%s' has already been started",
                name, unix_socket);
        return;
    }


    if (!vub_new(vub_device, name, unix_socket, writable, errp)) {
        return;
    }


    vub_device->listener = qio_net_listener_new();

    qio_net_listener_set_name(vub_device->listener,
                              "vhost-user-backend-listener");

    SocketAddress *addr = g_new0(SocketAddress, 1);
    addr->u.q_unix.path = (char *) unix_socket;
    addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    if (qio_net_listener_open_sync(vub_device->listener, addr, 1, errp) < 0) {
        goto error;
    }


    QTAILQ_INSERT_TAIL(&vub_devs, vub_device, next);
    QTAILQ_INIT(&vub_device->clients);

    qio_net_listener_set_client_func(vub_device->listener,
                                     vub_accept,
                                     vub_device,
                                     NULL);

    return;

 error:
    vub_free(vub_device, false);
}

static void vu_set_block_name(Object *obj, const char *value,
                                           Error **errp)
{
    VubDev *vus = VHOST_USER_SERVER(obj);;

    if (vus->name) {
        error_setg(errp, "evdev property already set");
        return;
    }

    vus->name = g_strdup(value);
}

static char *vu_get_block_name(Object *obj, Error **errp)
{
    VubDev *vus = VHOST_USER_SERVER(obj);
    return g_strdup(vus->name);
}


static void vu_set_unix_socket(Object *obj, const char *value,
                                            Error **errp)
{
    VubDev *vus = VHOST_USER_SERVER(obj);;

    if (vus->unix_socket) {
        error_setg(errp, "unix_socket property already set");
        return;
    }

    vus->unix_socket = g_strdup(value);
    vhost_user_server_start(vus, value, vus->name,
                            vus->writable, errp);
}

static char *vu_get_unix_socket(Object *obj, Error **errp)
{
    VubDev *vus = VHOST_USER_SERVER(obj);;
    return g_strdup(vus->unix_socket);
}

static bool vu_get_block_writable(Object *obj, Error **errp)
{
    VubDev *vus = VHOST_USER_SERVER(obj);;
    return vus->writable;
}

static void vu_set_block_writable(Object *obj, bool value, Error **errp)
{
    VubDev *vus = VHOST_USER_SERVER(obj);

    vus->writable = value;
}

static void vhost_user_server_instance_init(Object *obj)
{

    object_property_add_bool(obj, "writable",
                            vu_get_block_writable,
                            vu_set_block_writable, NULL);

    object_property_add_str(obj, "name",
                            vu_get_block_name,
                            vu_set_block_name, NULL);

    object_property_add_str(obj, "unix_socket",
                            vu_get_unix_socket,
                            vu_set_unix_socket, NULL);

}

static void vhost_user_server_instance_finalize(Object *obj)
{
    VubDev *vus = VHOST_USER_SERVER(obj);
    vhost_user_server_free(vus, true);
    /* object_del shouldn't free this object struct */
    obj->free = NULL;
}

static const TypeInfo vhost_user_server_info = {
    .name = TYPE_VHOST_USER_SERVER,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(VuDev),
    .instance_init = vhost_user_server_instance_init,
    .instance_finalize = vhost_user_server_instance_finalize,
    .interfaces = (InterfaceInfo[]) {
        {TYPE_USER_CREATABLE},
        {}
    },
};

static void vhost_user_server_register_types(void)
{
    type_register_static(&vhost_user_server_info);
}

type_init(vhost_user_server_register_types)

