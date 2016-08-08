/*
 * QEMU Block driver for Veritas HyperScale (VxHS)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "vxhs.h"
#include <qnio/qnio_api.h>
#include "trace.h"

/* qnio client ioapi_ctx */
static void __attribute__((unused)) *global_qnio_ctx;

/* insure init once */
static pthread_mutex_t __attribute__((unused))  of_global_ctx_lock =
    PTHREAD_MUTEX_INITIALIZER;

/* HyperScale Driver Version */
static const int __attribute__((unused)) vxhs_drv_version = 8895;

/* vdisk prefix to pass to qnio */
static const char vdisk_prefix[] = "/dev/of/vdisk";

void vxhs_inc_acb_segment_count(void *ptr, int count)
{
    VXHSAIOCB *acb = ptr;
    BDRVVXHSState *s = acb->common.bs->opaque;

    VXHS_SPIN_LOCK(s->vdisk_acb_lock);
    acb->segments += count;
    VXHS_SPIN_UNLOCK(s->vdisk_acb_lock);
}

void vxhs_dec_acb_segment_count(void *ptr, int count)
{
    VXHSAIOCB *acb = ptr;
    BDRVVXHSState *s = acb->common.bs->opaque;

    VXHS_SPIN_LOCK(s->vdisk_acb_lock);
    acb->segments -= count;
    VXHS_SPIN_UNLOCK(s->vdisk_acb_lock);
}

int vxhs_dec_and_get_acb_segment_count(void *ptr, int count)
{
    VXHSAIOCB *acb = ptr;
    BDRVVXHSState *s = acb->common.bs->opaque;
    int segcount = 0;


    VXHS_SPIN_LOCK(s->vdisk_acb_lock);
    acb->segments -= count;
    segcount = acb->segments;
    VXHS_SPIN_UNLOCK(s->vdisk_acb_lock);

    return segcount;
}

void vxhs_set_acb_buffer(void *ptr, void *buffer)
{
    VXHSAIOCB *acb = ptr;

    acb->buffer = buffer;
}

void vxhs_inc_vdisk_iocount(void *ptr, uint32_t count)
{
    BDRVVXHSState *s = ptr;

    VXHS_SPIN_LOCK(s->vdisk_lock);
    s->vdisk_aio_count += count;
    VXHS_SPIN_UNLOCK(s->vdisk_lock);
}

void vxhs_dec_vdisk_iocount(void *ptr, uint32_t count)
{
    BDRVVXHSState *s = ptr;

    VXHS_SPIN_LOCK(s->vdisk_lock);
    s->vdisk_aio_count -= count;
    VXHS_SPIN_UNLOCK(s->vdisk_lock);
}

uint32_t vxhs_get_vdisk_iocount(void *ptr)
{
    BDRVVXHSState *s = ptr;
    uint32_t count = 0;

    VXHS_SPIN_LOCK(s->vdisk_lock);
    count = s->vdisk_aio_count;
    VXHS_SPIN_UNLOCK(s->vdisk_lock);

    return count;
}

void vxhs_iio_callback(uint32_t rfd, uint32_t reason, void *ctx, void *m)
{
    VXHSAIOCB *acb = NULL;
    BDRVVXHSState *s = NULL;
    int rv = 0;
    int segcount = 0;
    uint32_t error = 0;
    uint32_t opcode = 0;

    assert(m);
    if (m) {
        /* TODO: need common get message attrs, not two separate lib calls */
        error = qemu_iio_extract_msg_error(m);
        opcode = qemu_iio_extract_msg_opcode(m);
    }
    switch (opcode) {
    case IRP_READ_REQUEST:
    case IRP_WRITE_REQUEST:

    /*
     * ctx is VXHSAIOCB*
     * ctx is NULL if error is VXERROR_CHANNEL_HUP or reason is IIO_REASON_HUP
     */
    if (ctx) {
        acb = ctx;
        s = acb->common.bs->opaque;
    } else {
        trace_vxhs_iio_callback(error, reason);
        goto out;
    }

    if (error) {
        trace_vxhs_iio_callback_iofail(error, reason, acb, acb->segments);

        if (reason == IIO_REASON_DONE || reason == IIO_REASON_EVENT) {
            /*
             * Storage agent failed while I/O was in progress
             * Fail over only if the qnio channel dropped, indicating
             * storage agent failure. Don't fail over in response to other
             * I/O errors such as disk failure.
             */
            if (error == VXERROR_RETRY_ON_SOURCE || error == VXERROR_HUP ||
                error == VXERROR_CHANNEL_HUP || error == -1) {
                /*
                 * Start vDisk IO failover once callback is
                 * called against all the pending IOs.
                 * If vDisk has no redundency enabled
                 * then IO failover routine will mark
                 * the vDisk failed and fail all the
                 * AIOs without retry (stateless vDisk)
                 */
                VXHS_SPIN_LOCK(s->vdisk_lock);
                if (!OF_VDISK_IOFAILOVER_IN_PROGRESS(s)) {
                    OF_VDISK_SET_IOFAILOVER_IN_PROGRESS(s);
                }
                /*
                 * Check if this acb is already queued before.
                 * It is possible in case if I/Os are submitted
                 * in multiple segments (QNIO_MAX_IO_SIZE).
                 */
                VXHS_SPIN_LOCK(s->vdisk_acb_lock);
                if (!OF_AIOCB_FLAGS_QUEUED(acb)) {
                    QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq,
                                         acb, retry_entry);
                    OF_AIOCB_FLAGS_SET_QUEUED(acb);
                    s->vdisk_aio_retry_qd++;
                    trace_vxhs_iio_callback_retry(s->vdisk_guid, acb);
                }
                segcount = --acb->segments;
                VXHS_SPIN_UNLOCK(s->vdisk_acb_lock);
                /*
                 * Decrement AIO count only when callback is called
                 * against all the segments of aiocb.
                 */
                if (segcount == 0 && --s->vdisk_aio_count == 0) {
                    /*
                     * Start vDisk I/O failover
                     */
                    VXHS_SPIN_UNLOCK(s->vdisk_lock);
                    /*
                     * TODO:
                     * Need to explore further if it is possible to optimize
                     * the failover operation on Virtual-Machine (global)
                     * specific rather vDisk specific.
                     */
                    vxhs_failover_io(s);
                    goto out;
                }
                VXHS_SPIN_UNLOCK(s->vdisk_lock);
                goto out;
            }
        } else if (reason == IIO_REASON_HUP) {
            /*
             * Channel failed, spontaneous notification,
             * not in response to I/O
             */
            trace_vxhs_iio_callback_chnlfail(error);
            /*
             * TODO: Start channel failover when no I/O is outstanding
             */
            goto out;
        } else {
            trace_vxhs_iio_callback_fail(reason, acb, acb->segments, acb->size, error);
        }
    }
    /*
     * Set error into acb if not set. In case if acb is being
     * submitted in multiple segments then need to set the error
     * only once.
     *
     * Once acb done callback is called for the last segment
     * then acb->ret return status will be sent back to the
     * caller.
     */
    VXHS_SPIN_LOCK(s->vdisk_acb_lock);
    if (error && !acb->ret) {
        acb->ret = error;
    }
    --acb->segments;
    segcount = acb->segments;
    assert(segcount >= 0);
    VXHS_SPIN_UNLOCK(s->vdisk_acb_lock);
    /*
     * Check if all the outstanding I/Os are done against acb.
     * If yes then send signal for AIO completion.
     */
    if (segcount == 0) {
        rv = qemu_write_full(s->fds[VDISK_FD_WRITE], &acb, sizeof(acb));
        if (rv != sizeof(acb)) {
            error_report("VXHS AIO completion failed: %s", strerror(errno));
            abort();
        }
    }
    break;

    case IRP_VDISK_CHECK_IO_FAILOVER_READY:
        /* ctx is BDRVVXHSState* */
        assert(ctx);
        trace_vxhs_iio_callback_ready(((BDRVVXHSState *)ctx)->vdisk_guid, error);
        vxhs_failover_ioctl_cb(error, ctx);
        break;

    default:
        if (reason == IIO_REASON_HUP) {
            /*
             * Channel failed, spontaneous notification,
             * not in response to I/O
             */
            trace_vxhs_iio_callback_chnfail(error, errno);
            /*
             * TODO: Start channel failover when no I/O is outstanding
             */
        } else {
            trace_vxhs_iio_callback_unknwn(opcode, error);
        }
        break;
    }
out:
    return;
}

void vxhs_complete_aio(VXHSAIOCB *acb, BDRVVXHSState *s)
{
    BlockCompletionFunc *cb = acb->common.cb;
    void *opaque = acb->common.opaque;
    int ret = 0;

    if (acb->ret != 0) {
        trace_vxhs_complete_aio(acb, acb->ret);
    /*
     * We mask all the IO errors generically as EIO for upper layers
     * Right now our IO Manager uses non standard error codes. Instead
     * of confusing upper layers with incorrect interpretation we are
     * doing this workaround.
     */
        ret = (-EIO);
    }
    /*
     * Copy back contents from stablization buffer into original iovector
     * before returning the IO
     */
    if (acb->buffer != NULL) {
        qemu_iovec_from_buf(acb->qiov, 0, acb->buffer, acb->qiov->size);
        free(acb->buffer);
        acb->buffer = NULL;
    }
    vxhs_dec_vdisk_iocount(s, 1);
    acb->aio_done = VXHS_IO_COMPLETED;
    qemu_aio_unref(acb);
    cb(opaque, ret);
}

/*
 * This is the HyperScale event handler registered to QEMU.
 * It is invoked when any IO gets completed and written on pipe
 * by callback called from QNIO thread context. Then it marks
 * the AIO as completed, and releases HyperScale AIO callbacks.
 */
void vxhs_aio_event_reader(void *opaque)
{
    BDRVVXHSState *s = opaque;
    ssize_t ret;

    do {
        char *p = (char *)&s->qnio_event_acb;

        ret = read(s->fds[VDISK_FD_READ], p + s->event_reader_pos,
                   sizeof(s->qnio_event_acb) - s->event_reader_pos);
        if (ret > 0) {
            s->event_reader_pos += ret;
            if (s->event_reader_pos == sizeof(s->qnio_event_acb)) {
                s->event_reader_pos = 0;
                vxhs_complete_aio(s->qnio_event_acb, s);
            }
        }
    } while (ret < 0 && errno == EINTR);
}

/*
 * QEMU calls this to check if there are any pending IO on vDisk.
 * It will wait in a loop until all the AIOs are completed.
 */
int vxhs_aio_flush_cb(void *opaque)
{
    BDRVVXHSState *s = opaque;

    return vxhs_get_vdisk_iocount(s);
}

/*
 * This will be called by QEMU while booting for each vDisk.
 * bs->opaque will be allocated by QEMU upper block layer before
 * calling open. It will load all the QNIO operations from
 * qemuqnio library and call QNIO operation to create channel to
 * do IO on vDisk. It parses the URI, gets the hostname, vDisk
 * path and then sets HyperScale event handler to QEMU.
 */
void *vxhs_setup_qnio(void)
{
    void *qnio_ctx = NULL;

    qnio_ctx = qemu_iio_init(vxhs_iio_callback);

    if (qnio_ctx != NULL) {
        trace_vxhs_setup_qnio(qnio_ctx);
    } else {
        trace_vxhs_setup_qnio_nwerror('.');
    }

    return qnio_ctx;
}

int vxhs_open_device(const char *vxhs_uri, int *cfd, int *rfd,
                       BDRVVXHSState *s)
{
    char *file_name;
    char *of_vsa_addr;
    int ret = 0;
    gchar **target_list;

    pthread_mutex_lock(&of_global_ctx_lock);
    if (global_qnio_ctx == NULL) {
        global_qnio_ctx = vxhs_setup_qnio();
        if (global_qnio_ctx == NULL) {
            pthread_mutex_unlock(&of_global_ctx_lock);
            return -1;
        }
    }
    pthread_mutex_unlock(&of_global_ctx_lock);

    *cfd = -1;

    of_vsa_addr = g_new0(char, OF_MAX_SERVER_ADDR);
    file_name = g_new0(char, OF_MAX_FILE_LEN);

    /*
     * The steps below need to done by all the block drivers in QEMU which
     * support AIO. Need to create pipe for communicating b/w two threads
     * in different context. And set handler for read event when IO completion
     * is reported by non-QEMU context.
     */
    trace_vxhs_open_device_cmdline(vxhs_uri);
    target_list = g_strsplit(vxhs_uri, "%7D", 0);
    assert(target_list != NULL && target_list[0] != NULL);
    vxhs_build_io_target_list(s, target_list);

    snprintf(file_name, OF_MAX_FILE_LEN, "%s%s", vdisk_prefix, s->vdisk_guid);
    snprintf(of_vsa_addr, OF_MAX_SERVER_ADDR, "of://%s:%d",
             s->vdisk_hostinfo[s->vdisk_cur_host_idx].hostip,
             s->vdisk_hostinfo[s->vdisk_cur_host_idx].port);

    *cfd = qemu_open_iio_conn(global_qnio_ctx, of_vsa_addr, 0);
    if (*cfd < 0) {
        trace_vxhs_open_device_qnio(of_vsa_addr);
        ret = -EIO;
        goto out;
    }
    *rfd = qemu_iio_devopen(global_qnio_ctx, *cfd, file_name, 0);
    s->aio_context = qemu_get_aio_context();

out:
    /* uri is still in use, cleaned up in close */
    if (file_name != NULL) {
        g_free(file_name);
    }
    if (of_vsa_addr != NULL) {
        g_free(of_vsa_addr);
    }
    g_strfreev(target_list);
    return ret;
}

int vxhs_create(const char *filename,
        QemuOpts *options, Error **errp)
{
        int ret = 0;
        int qemu_cfd = 0;
        int qemu_rfd = 0;
        BDRVVXHSState s;

        trace_vxhs_create(filename);
        ret = vxhs_open_device(filename, &qemu_cfd, &qemu_rfd, &s);

        return ret;
}

static QemuOptsList runtime_opts = {
    .name = "vxhs",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "URI to the Veritas HyperScale image",
        },
        { /* end of list */ }
    },
};

int vxhs_open(BlockDriverState *bs, QDict *options,
              int bdrv_flags, Error **errp)
{
    BDRVVXHSState *s = bs->opaque;
    int ret = 0;
    int qemu_qnio_cfd = 0;
    int qemu_rfd = 0;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *vxhs_uri;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    vxhs_uri = qemu_opt_get(opts, "filename");

    memset(s, 0, sizeof(*s));
    trace_vxhs_open(vxhs_uri);
    ret = vxhs_open_device(vxhs_uri, &qemu_qnio_cfd, &qemu_rfd, s);
    if (ret != 0) {
        trace_vxhs_open_fail(ret);
        return ret;
    }
    s->qnio_ctx = global_qnio_ctx;
    s->vdisk_hostinfo[0].qnio_cfd = qemu_qnio_cfd;
    s->vdisk_hostinfo[0].vdisk_rfd = qemu_rfd;
    s->vdisk_size = 0;
    QSIMPLEQ_INIT(&s->vdisk_aio_retryq);

    ret = qemu_pipe(s->fds);
    if (ret < 0) {
        trace_vxhs_open_epipe('.');
        ret = -errno;
        goto out;
    }
    fcntl(s->fds[VDISK_FD_READ], F_SETFL, O_NONBLOCK);

    aio_set_fd_handler(s->aio_context, s->fds[VDISK_FD_READ],
                       false, vxhs_aio_event_reader, NULL, s);

    /*
     * Allocate/Initialize the spin-locks.
     *
     * NOTE:
     *      Since spin lock is being allocated
     *      dynamically hence moving acb struct
     *      specific lock to BDRVVXHSState
     *      struct. The reason being,
     *      we don't want the overhead of spin
     *      lock being dynamically allocated and
     *      freed for every AIO.
     */
    s->vdisk_lock = VXHS_SPIN_LOCK_ALLOC;
    s->vdisk_acb_lock = VXHS_SPIN_LOCK_ALLOC;

    return 0;

out:
    if (s->vdisk_hostinfo[0].vdisk_rfd >= 0) {
        qemu_iio_devclose(s->qnio_ctx, 0,
                                 s->vdisk_hostinfo[0].vdisk_rfd);
    }
    /* never close qnio_cfd */
    trace_vxhs_open_fail(ret);
    return ret;
}

static const AIOCBInfo vxhs_aiocb_info = {
    .aiocb_size = sizeof(VXHSAIOCB)
};

/*
 * This is called in QNIO thread context when IO done
 * on IO Manager and QNIO client received the data or
 * ACK. It notify another event handler thread running in QEMU context
 * by writing on the pipe
 */
void vxhs_finish_aiocb(ssize_t ret, void *arg)
{
    VXHSAIOCB *acb = arg;
    BlockDriverState *bs = acb->common.bs;
    BDRVVXHSState *s = bs->opaque;
    int rv;

    acb->ret = ret;
    rv = qemu_write_full(s->fds[VDISK_FD_WRITE], &acb, sizeof(acb));
    if (rv != sizeof(acb)) {
        error_report("VXHS AIO completion failed: %s",
                     strerror(errno));
        abort();
    }
}

/*
 * This allocates QEMU-VXHS callback for each IO
 * and is passed to QNIO. When QNIO completes the work,
 * it will be passed back through the callback.
 */
BlockAIOCB *vxhs_aio_rw(BlockDriverState *bs,
                                int64_t sector_num, QEMUIOVector *qiov,
                                int nb_sectors,
                                BlockCompletionFunc *cb,
                                void *opaque, int iodir)
{
    VXHSAIOCB         *acb = NULL;
    BDRVVXHSState     *s = bs->opaque;
    size_t              size;
    uint64_t            offset;
    int                 iio_flags = 0;
    int                 ret = 0;

    offset = sector_num * BDRV_SECTOR_SIZE;
    size = nb_sectors * BDRV_SECTOR_SIZE;

    acb = qemu_aio_get(&vxhs_aiocb_info, bs, cb, opaque);
    /*
     * Setup or initialize VXHSAIOCB.
     * Every single field should be initialized since
     * acb will be picked up from the slab without
     * initializing with zero.
     */
    acb->io_offset = offset;
    acb->size = size;
    acb->ret = 0;
    acb->flags = 0;
    acb->aio_done = VXHS_IO_INPROGRESS;
    acb->segments = 0;
    acb->buffer = 0;
    acb->qiov = qiov;
    acb->direction = iodir;

    VXHS_SPIN_LOCK(s->vdisk_lock);
    if (OF_VDISK_FAILED(s)) {
        trace_vxhs_aio_rw(s->vdisk_guid, iodir, size, offset);
        VXHS_SPIN_UNLOCK(s->vdisk_lock);
        goto errout;
    }
    if (OF_VDISK_IOFAILOVER_IN_PROGRESS(s)) {
        QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq, acb, retry_entry);
        s->vdisk_aio_retry_qd++;
        OF_AIOCB_FLAGS_SET_QUEUED(acb);
        VXHS_SPIN_UNLOCK(s->vdisk_lock);
        trace_vxhs_aio_rw_retry(s->vdisk_guid, acb, 1);
        goto out;
    }
    s->vdisk_aio_count++;
    VXHS_SPIN_UNLOCK(s->vdisk_lock);

    iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);

    switch (iodir) {
    case VDISK_AIO_WRITE:
            vxhs_inc_acb_segment_count(acb, 1);
            ret = qemu_iio_writev(s->qnio_ctx,
                    s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                    qiov->iov, qiov->niov, offset, (void *)acb, iio_flags);
            break;
    case VDISK_AIO_READ:
            vxhs_inc_acb_segment_count(acb, 1);
            ret = qemu_iio_readv(s->qnio_ctx,
                    s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                    qiov->iov, qiov->niov, offset, (void *)acb, iio_flags);
            break;
    default:
            trace_vxhs_aio_rw_invalid(iodir);
            goto errout;
    }

    if (ret != 0) {
        trace_vxhs_aio_rw_ioerr(
                  s->vdisk_guid, iodir, size, offset,
                  acb, acb->segments, ret, errno);
        /*
         * Don't retry I/Os against vDisk having no
         * redundency or statefull storage on compute
         *
         * TODO: Revisit this code path to see if any
         *       particular error needs to be handled.
         *       At this moment failing the I/O.
         */
        VXHS_SPIN_LOCK(s->vdisk_lock);
        if (s->vdisk_nhosts == 1) {
            trace_vxhs_aio_rw_iofail(s->vdisk_guid);
            s->vdisk_aio_count--;
            vxhs_dec_acb_segment_count(acb, 1);
            VXHS_SPIN_UNLOCK(s->vdisk_lock);
            goto errout;
        }
        if (OF_VDISK_FAILED(s)) {
            trace_vxhs_aio_rw_devfail(
                      s->vdisk_guid, iodir, size, offset);
            s->vdisk_aio_count--;
            vxhs_dec_acb_segment_count(acb, 1);
            VXHS_SPIN_UNLOCK(s->vdisk_lock);
            goto errout;
        }
        if (OF_VDISK_IOFAILOVER_IN_PROGRESS(s)) {
            /*
             * Queue all incoming io requests after failover starts.
             * Number of requests that can arrive is limited by io queue depth
             * so an app blasting independent ios will not exhaust memory.
             */
            QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq, acb, retry_entry);
            s->vdisk_aio_retry_qd++;
            OF_AIOCB_FLAGS_SET_QUEUED(acb);
            s->vdisk_aio_count--;
            vxhs_dec_acb_segment_count(acb, 1);
            VXHS_SPIN_UNLOCK(s->vdisk_lock);
            trace_vxhs_aio_rw_retry(s->vdisk_guid, acb, 2);
            goto out;
        }
        OF_VDISK_SET_IOFAILOVER_IN_PROGRESS(s);
        QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq, acb, retry_entry);
        s->vdisk_aio_retry_qd++;
        OF_AIOCB_FLAGS_SET_QUEUED(acb);
        vxhs_dec_acb_segment_count(acb, 1);
        trace_vxhs_aio_rw_retry(s->vdisk_guid, acb, 3);
        /*
         * Start I/O failover if there is no active
         * AIO within vxhs block driver.
         */
        if (--s->vdisk_aio_count == 0) {
            VXHS_SPIN_UNLOCK(s->vdisk_lock);
            /*
             * Start IO failover
             */
            vxhs_failover_io(s);
            goto out;
        }
        VXHS_SPIN_UNLOCK(s->vdisk_lock);
    }

out:
    return &acb->common;

errout:
    qemu_aio_unref(acb);
    return NULL;
}

BlockAIOCB *vxhs_aio_readv(BlockDriverState *bs,
                                   int64_t sector_num, QEMUIOVector *qiov,
                                   int nb_sectors,
                                   BlockCompletionFunc *cb, void *opaque)
{
    return vxhs_aio_rw(bs, sector_num, qiov, nb_sectors,
                         cb, opaque, VDISK_AIO_READ);
}

BlockAIOCB *vxhs_aio_writev(BlockDriverState *bs,
                                    int64_t sector_num, QEMUIOVector *qiov,
                                    int nb_sectors,
                                    BlockCompletionFunc *cb, void *opaque)
{
    return vxhs_aio_rw(bs, sector_num, qiov, nb_sectors,
                         cb, opaque, VDISK_AIO_WRITE);
}

/*
 * This is called by QEMU when a flush gets triggered from within
 * a guest at the block layer, either for IDE or SCSI disks.
 */
int vxhs_co_flush(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;
    uint64_t size = 0;
    int ret = 0;
    uint32_t iocount = 0;

    ret = qemu_iio_ioctl(s->qnio_ctx,
            s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
            VDISK_AIO_FLUSH, &size, NULL, IIO_FLAG_SYNC);

    if (ret < 0) {
        /*
         * Currently not handling the flush ioctl
         * failure because of network connection
         * disconnect. Since all the writes are
         * commited into persistent storage hence
         * this flush call is noop and we can safely
         * return success status to the caller.
         *
         * If any write failure occurs for inflight
         * write AIO because of network disconnect
         * then anyway IO failover will be triggered.
         */
        trace_vxhs_co_flush(s->vdisk_guid, ret, errno);
        ret = 0;
    }

    iocount = vxhs_get_vdisk_iocount(s);
    if (iocount > 0) {
        trace_vxhs_co_flush_iocnt(iocount);
    }

    return ret;
}

/*
 * This is called by guest or QEMU to free blocks.
 * When block freed when files deleted in the guest, fstrim utility
 * can be used to pass the hints to the block layer if the disk supports
 * TRIM. It send WRITE_SAME SCSI command to QEMU virtio-scsi layer, which
 * call bdrv_aio_discard interface.
 */
coroutine_fn int vxhs_co_pdiscard(BlockDriverState *bs,
                                   int64_t sector_num, int nb_sectors)
{
    int64_t off, size;

    off = sector_num * BDRV_SECTOR_SIZE;
    size = nb_sectors * BDRV_SECTOR_SIZE;

    vxhsErr("We are faking the discard for range off = %lu "
              "for %lu bytes\n", off, size);
    vxhsErr("returning from discard\n");

    return 0;
}

unsigned long vxhs_get_vdisk_stat(BDRVVXHSState *s)
{
    void *ctx = NULL;
    int flags = 0;
    unsigned long vdisk_size = 0;
    int ret = 0;

    ret = qemu_iio_ioctl(s->qnio_ctx,
            s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
            VDISK_STAT, &vdisk_size, ctx, flags);

    if (ret < 0) {
        trace_vxhs_get_vdisk_stat(s->vdisk_guid, ret, errno);
    }

    return vdisk_size;
}

/*
 * Returns the size of vDisk in bytes. This is required
 * by QEMU block upper block layer so that it is visible
 * to guest.
 */
int64_t vxhs_getlength(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;
    unsigned long vdisk_size = 0;

    if (s->vdisk_size > 0) {
        vdisk_size = s->vdisk_size;
    } else {
        /*
         * Fetch the vDisk size using stat ioctl
         */
        vdisk_size = vxhs_get_vdisk_stat(s);
        if (vdisk_size > 0) {
            s->vdisk_size = vdisk_size;
        }
    }

    if (vdisk_size > 0) {
        return (int64_t)vdisk_size; /* return size in bytes */
    } else {
        return -EIO;
    }
}

/*
 * Returns actual blocks allocated for the vDisk.
 * This is required by qemu-img utility.
 */
int64_t vxhs_get_allocated_blocks(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;
    unsigned long vdisk_size = 0;

    if (s->vdisk_size > 0) {
        vdisk_size = s->vdisk_size;
    } else {
        /*
         * TODO:
         * Once HyperScale storage-virtualizer provides
         * actual physical allocation of blocks then
         * fetch that information and return back to the
         * caller but for now just get the full size.
         */
        vdisk_size = vxhs_get_vdisk_stat(s);
        if (vdisk_size > 0) {
            s->vdisk_size = vdisk_size;
        }
    }

    if (vdisk_size > 0) {
        return (int64_t)vdisk_size; /* return size in bytes */
    } else {
        return -EIO;
    }
}

void vxhs_close(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;

    close(s->fds[VDISK_FD_READ]);
    close(s->fds[VDISK_FD_WRITE]);

    /*
     * never close channel - not ref counted, will
     * close for all vdisks
     */
    if (s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd >= 0) {
        qemu_iio_devclose(s->qnio_ctx, 0,
            s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd);
    }
    if (s->vdisk_lock) {
        VXHS_SPIN_LOCK_DESTROY(s->vdisk_lock);
        s->vdisk_lock = NULL;
    }
    if (s->vdisk_acb_lock) {
        VXHS_SPIN_LOCK_DESTROY(s->vdisk_acb_lock);
        s->vdisk_acb_lock = NULL;
    }

    /*
     * TODO: Verify that all the resources were relinguished.
     */
}

/*
 * If errors are consistent with storage agent failure:
 *  - Try to reconnect in case error is transient or storage agent restarted.
 *  - Currently failover is being triggered on per vDisk basis. There is
 *    a scope of further optimization where failover can be global (per VM).
 *  - In case of network (storage agent) failure, for all the vDisks, having
 *    no redundency, I/Os will be failed without attempting for I/O failover
 *    because of stateless nature of vDisk.
 *  - If local or source storage agent is down then send an ioctl to remote
 *    storage agent to check if remote storage agent in a state to accept
 *    application I/Os.
 *  - Once remote storage agent is ready to accept I/O, start I/O shipping.
 *  - If I/Os cannot be serviced then vDisk will be marked failed so that
 *    new incoming I/Os are returned with failure immediately.
 *  - If vDisk I/O failover is in progress then all new/inflight I/Os will
 *    queued and will be restarted or failed based on failover operation
 *    is successful or not.
 *  - I/O failover can be started either in I/O forward or I/O backward
 *    path.
 *  - I/O failover will be started as soon as all the pending acb(s)
 *    are queued and there is no pending I/O count.
 *  - If I/O failover couldn't be completed within QNIO_CONNECT_TIMOUT_SECS
 *    then vDisk will be marked failed and all I/Os will be completed with
 *    error.
 */

int vxhs_switch_storage_agent(BDRVVXHSState *s)
{
    int res = 0;
    int flags = (IIO_FLAG_ASYNC | IIO_FLAG_DONE);

    trace_vxhs_switch_storage_agent(
              s->vdisk_hostinfo[s->vdisk_ask_failover_idx].hostip,
              s->vdisk_guid);

    res = vxhs_reopen_vdisk(s, s->vdisk_ask_failover_idx);
    if (res == 0) {
        res = qemu_iio_ioctl(s->qnio_ctx,
                   s->vdisk_hostinfo[s->vdisk_ask_failover_idx].vdisk_rfd,
                   VDISK_CHECK_IO_FAILOVER_READY, NULL, s, flags);
    }
    if (res != 0) {
    trace_vxhs_switch_storage_agent_failed(
                  s->vdisk_hostinfo[s->vdisk_ask_failover_idx].hostip,
                  s->vdisk_guid, res, errno);
        /*
         * TODO: calling vxhs_failover_ioctl_cb from here ties up the qnio epoll
         * loop if qemu_iio_ioctl fails synchronously (-1) for all hosts in io
         * target list.
         */

        /* try next host */
        vxhs_failover_ioctl_cb(res, s);
    }
    return res;
}

void vxhs_failover_ioctl_cb(int res, void *ctx)
{
    BDRVVXHSState *s = ctx;

    if (res == 0) {
        /* found failover target */
        s->vdisk_cur_host_idx = s->vdisk_ask_failover_idx;
        s->vdisk_ask_failover_idx = 0;
        trace_vxhs_failover_ioctl_cb(
                   s->vdisk_hostinfo[s->vdisk_cur_host_idx].hostip,
                   s->vdisk_guid);
        VXHS_SPIN_LOCK(s->vdisk_lock);
        OF_VDISK_RESET_IOFAILOVER_IN_PROGRESS(s);
        VXHS_SPIN_UNLOCK(s->vdisk_lock);
        vxhs_handle_queued_ios(s);
    } else {
        /* keep looking */
        trace_vxhs_failover_ioctl_cb_retry(s->vdisk_guid);
        s->vdisk_ask_failover_idx++;
        if (s->vdisk_ask_failover_idx == s->vdisk_nhosts) {
            /* pause and cycle through list again */
            sleep(QNIO_CONNECT_RETRY_SECS);
            s->vdisk_ask_failover_idx = 0;
        }
        res = vxhs_switch_storage_agent(s);
    }
}

int vxhs_failover_io(BDRVVXHSState *s)
{
    int res = 0;

    trace_vxhs_failover_io(s->vdisk_guid);

    s->vdisk_ask_failover_idx = 0;
    res = vxhs_switch_storage_agent(s);

    return res;
}

/*
 * Try to reopen the vDisk on one of the available hosts
 * If vDisk reopen is successful on any of the host then
 * check if that node is ready to accept I/O.
 */
int vxhs_reopen_vdisk(BDRVVXHSState *s, int index)
{
    char *of_vsa_addr = NULL;
    char *file_name = NULL;
    int  res = 0;

    /*
     * Don't close the channel if it was opened
     * before successfully. It will be handled
     * within iio* api if the same channel open
     * fd is reused.
     *
     * close stale vdisk device remote fd since
     * it is invalid after channel disconnect.
     */
    if (s->vdisk_hostinfo[index].vdisk_rfd >= 0) {
        qemu_iio_devclose(s->qnio_ctx, 0,
                                 s->vdisk_hostinfo[index].vdisk_rfd);
        s->vdisk_hostinfo[index].vdisk_rfd = -1;
    }
    /*
     * build storage agent address and vdisk device name strings
     */
    of_vsa_addr = g_new0(char, OF_MAX_SERVER_ADDR);
    file_name = g_new0(char, OF_MAX_FILE_LEN);
    snprintf(file_name, OF_MAX_FILE_LEN, "%s%s", vdisk_prefix, s->vdisk_guid);
    snprintf(of_vsa_addr, OF_MAX_SERVER_ADDR, "of://%s:%d",
             s->vdisk_hostinfo[index].hostip, s->vdisk_hostinfo[index].port);
    /*
     * open qnio channel to storage agent if not opened before.
     */
    if (s->vdisk_hostinfo[index].qnio_cfd < 0) {
        s->vdisk_hostinfo[index].qnio_cfd =
                qemu_open_iio_conn(global_qnio_ctx, of_vsa_addr, 0);
        if (s->vdisk_hostinfo[index].qnio_cfd < 0) {
            trace_vxhs_reopen_vdisk(s->vdisk_hostinfo[index].hostip);
            res = ENODEV;
            goto out;
        }
    }
    /*
     * open vdisk device
     */
    s->vdisk_hostinfo[index].vdisk_rfd =
            qemu_iio_devopen(global_qnio_ctx,
             s->vdisk_hostinfo[index].qnio_cfd, file_name, 0);
    if (s->vdisk_hostinfo[index].vdisk_rfd < 0) {
        trace_vxhs_reopen_vdisk_openfail(file_name);
        res = EIO;
        goto out;
    }
out:
    if (of_vsa_addr) {
        g_free(of_vsa_addr);
    }
    if (file_name) {
        g_free(file_name);
    }
    return res;
}

/*
 * vxhs_build_io_target_list: Initialize io target list with ip addresses of
 * local storage agent and reflection target storage agents. The local storage
 * agent ip is the efficient internal address in the uri, e.g. 192.168.0.2.
 * The local storage agent address is stored at index 0. The reflection target
 * ips, are the E-W data network addresses of the reflection node agents, also
 * extracted from the uri.
 */
int vxhs_build_io_target_list(BDRVVXHSState *s, char **filenames)
{
    URI *uri = NULL;
    int i = 0;

    for (i = 0; filenames[i] != NULL && *filenames[i]; i++) {
        trace_vxhs_build_io_target_list(i+1, filenames[i]);
        uri = uri_parse(filenames[i]);
        assert(uri != NULL && uri->server != NULL);
        s->vdisk_hostinfo[i].hostip = g_new0(char, strlen(uri->server));
        strncpy((s->vdisk_hostinfo[i].hostip), uri->server, IP_ADDR_LEN);
        s->vdisk_hostinfo[i].port = uri->port;
        s->vdisk_hostinfo[i].qnio_cfd = -1;
        s->vdisk_hostinfo[i].vdisk_rfd = -1;
        if (i==0 && (strstr(uri->path, "vxhs") == NULL)) {
            s->vdisk_guid = g_new0(char, strlen(uri->path)+3);
            strcpy((s->vdisk_guid), uri->path);
            strcat((s->vdisk_guid), "}");
        }
        uri_free(uri);
    }
    s->vdisk_nhosts = i;
    s->vdisk_cur_host_idx = 0;

   return 0;
}

int vxhs_handle_queued_ios(BDRVVXHSState *s)
{
    VXHSAIOCB *acb = NULL;
    int res = 0;

    VXHS_SPIN_LOCK(s->vdisk_lock);
    while ((acb = QSIMPLEQ_FIRST(&s->vdisk_aio_retryq)) != NULL) {
        /*
         * Before we process the acb, check whether I/O failover
         * started again due to failback or cascading failure.
         */
        if (OF_VDISK_IOFAILOVER_IN_PROGRESS(s)) {
            VXHS_SPIN_UNLOCK(s->vdisk_lock);
            goto out;
        }
        QSIMPLEQ_REMOVE_HEAD(&s->vdisk_aio_retryq, retry_entry);
        s->vdisk_aio_retry_qd--;
        OF_AIOCB_FLAGS_RESET_QUEUED(acb);
        if (OF_VDISK_FAILED(s)) {
            VXHS_SPIN_UNLOCK(s->vdisk_lock);
            vxhs_fail_aio(acb, EIO);
            VXHS_SPIN_LOCK(s->vdisk_lock);
        } else {
            VXHS_SPIN_UNLOCK(s->vdisk_lock);
            res = vxhs_restart_aio(acb);
            trace_vxhs_handle_queued_ios(acb, res);
            VXHS_SPIN_LOCK(s->vdisk_lock);
            if (res) {
                QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq,
                                     acb, retry_entry);
                OF_AIOCB_FLAGS_SET_QUEUED(acb);
                VXHS_SPIN_UNLOCK(s->vdisk_lock);
                goto out;
            }
        }
    }
    VXHS_SPIN_UNLOCK(s->vdisk_lock);
out:
    return res;
}

int vxhs_restart_aio(VXHSAIOCB *acb)
{
    BDRVVXHSState *s = NULL;
    int iio_flags = 0;
    int res = 0;

    s = acb->common.bs->opaque;

    if (acb->direction == VDISK_AIO_WRITE) {
        vxhs_inc_vdisk_iocount(s, 1);
        vxhs_inc_acb_segment_count(acb, 1);
        iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);
        res = qemu_iio_writev(s->qnio_ctx,
                s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                acb->qiov->iov, acb->qiov->niov,
                acb->io_offset, (void *)acb, iio_flags);
    }

    if (acb->direction == VDISK_AIO_READ) {
        vxhs_inc_vdisk_iocount(s, 1);
        vxhs_inc_acb_segment_count(acb, 1);
        iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);
        res = qemu_iio_readv(s->qnio_ctx,
                s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                acb->qiov->iov, acb->qiov->niov,
                acb->io_offset, (void *)acb, iio_flags);
    }

    if (res != 0) {
        vxhs_dec_vdisk_iocount(s, 1);
        vxhs_dec_acb_segment_count(acb, 1);
        trace_vxhs_restart_aio(acb->direction, res, errno);
    }

    return res;
}

void vxhs_fail_aio(VXHSAIOCB *acb, int err)
{
    BDRVVXHSState *s = NULL;
    int segcount = 0;
    int rv = 0;

    s = acb->common.bs->opaque;

    trace_vxhs_fail_aio(s->vdisk_guid, acb);
    if (!acb->ret) {
        acb->ret = err;
    }
    VXHS_SPIN_LOCK(s->vdisk_acb_lock);
    segcount = acb->segments;
    VXHS_SPIN_UNLOCK(s->vdisk_acb_lock);
    if (segcount == 0) {
        /*
         * Complete the io request
         */
        rv = qemu_write_full(s->fds[VDISK_FD_WRITE], &acb, sizeof(acb));
        if (rv != sizeof(acb)) {
            error_report("VXHS AIO completion failed: %s",
                         strerror(errno));
            abort();
        }
    }
}

static BlockDriver bdrv_vxhs = {
    .format_name                  = "vxhs",
    .protocol_name                = "vxhs",
    .instance_size                = sizeof(BDRVVXHSState),
    .bdrv_file_open               = vxhs_open,
    .bdrv_create                  = vxhs_create,
    .bdrv_close                   = vxhs_close,
    .bdrv_getlength               = vxhs_getlength,
    .bdrv_get_allocated_file_size = vxhs_get_allocated_blocks,
    .bdrv_aio_readv               = vxhs_aio_readv,
    .bdrv_aio_writev              = vxhs_aio_writev,
    .bdrv_co_flush_to_disk        = vxhs_co_flush,
    .bdrv_co_pdiscard             = vxhs_co_pdiscard,
};

void bdrv_vxhs_init(void)
{
    trace_vxhs_bdrv_init(vxhs_drv_version);
    bdrv_register(&bdrv_vxhs);
}

/*
 * The line below is how our drivier is initialized.
 * DO NOT TOUCH IT
 */
block_init(bdrv_vxhs_init);
