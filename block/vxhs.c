/*
 * QEMU Block driver for Veritas HyperScale (VxHS)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include <qnio/qnio_api.h>
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "trace.h"
#include "qemu/uri.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

#define QNIO_CONNECT_RETRY_SECS     5
#define QNIO_CONNECT_TIMOUT_SECS    120

/*
 * IO specific flags
 */
#define IIO_FLAG_ASYNC              0x00000001
#define IIO_FLAG_DONE               0x00000010
#define IIO_FLAG_SYNC               0

#define VDISK_FD_READ               0
#define VDISK_FD_WRITE              1
#define VXHS_MAX_HOSTS              4

#define VXHS_OPT_FILENAME           "filename"
#define VXHS_OPT_VDISK_ID           "vdisk_id"
#define VXHS_OPT_SERVER             "server."
#define VXHS_OPT_HOST               "host"
#define VXHS_OPT_PORT               "port"

/* qnio client ioapi_ctx */
static void *global_qnio_ctx;

/* vdisk prefix to pass to qnio */
static const char vdisk_prefix[] = "/dev/of/vdisk";

typedef enum {
    VXHS_IO_INPROGRESS,
    VXHS_IO_COMPLETED,
    VXHS_IO_ERROR
} VXHSIOState;

typedef enum {
    VDISK_AIO_READ,
    VDISK_AIO_WRITE,
    VDISK_STAT,
    VDISK_TRUNC,
    VDISK_AIO_FLUSH,
    VDISK_AIO_RECLAIM,
    VDISK_GET_GEOMETRY,
    VDISK_CHECK_IO_FAILOVER_READY,
    VDISK_AIO_LAST_CMD
} VDISKAIOCmd;

typedef void (*qnio_callback_t)(ssize_t retval, void *arg);

/*
 * BDRVVXHSState specific flags
 */
#define OF_VDISK_FLAGS_STATE_ACTIVE             0x0000000000000001
#define OF_VDISK_FLAGS_STATE_FAILED             0x0000000000000002
#define OF_VDISK_FLAGS_IOFAILOVER_IN_PROGRESS   0x0000000000000004

#define OF_VDISK_ACTIVE(s)                                              \
        ((s)->vdisk_flags & OF_VDISK_FLAGS_STATE_ACTIVE)
#define OF_VDISK_SET_ACTIVE(s)                                          \
        ((s)->vdisk_flags |= OF_VDISK_FLAGS_STATE_ACTIVE)
#define OF_VDISK_RESET_ACTIVE(s)                                        \
        ((s)->vdisk_flags &= ~OF_VDISK_FLAGS_STATE_ACTIVE)

#define OF_VDISK_FAILED(s)                                              \
        ((s)->vdisk_flags & OF_VDISK_FLAGS_STATE_FAILED)
#define OF_VDISK_SET_FAILED(s)                                          \
        ((s)->vdisk_flags |= OF_VDISK_FLAGS_STATE_FAILED)
#define OF_VDISK_RESET_FAILED(s)                                        \
        ((s)->vdisk_flags &= ~OF_VDISK_FLAGS_STATE_FAILED)

#define OF_VDISK_IOFAILOVER_IN_PROGRESS(s)                              \
        ((s)->vdisk_flags & OF_VDISK_FLAGS_IOFAILOVER_IN_PROGRESS)
#define OF_VDISK_SET_IOFAILOVER_IN_PROGRESS(s)                          \
        ((s)->vdisk_flags |= OF_VDISK_FLAGS_IOFAILOVER_IN_PROGRESS)
#define OF_VDISK_RESET_IOFAILOVER_IN_PROGRESS(s)                        \
        ((s)->vdisk_flags &= ~OF_VDISK_FLAGS_IOFAILOVER_IN_PROGRESS)

/*
 * VXHSAIOCB specific flags
 */
#define OF_ACB_QUEUED               0x00000001

#define OF_AIOCB_FLAGS_QUEUED(a)            \
        ((a)->flags & OF_ACB_QUEUED)
#define OF_AIOCB_FLAGS_SET_QUEUED(a)        \
        ((a)->flags |= OF_ACB_QUEUED)
#define OF_AIOCB_FLAGS_RESET_QUEUED(a)      \
        ((a)->flags &= ~OF_ACB_QUEUED)

typedef struct qemu2qnio_ctx {
    uint32_t            qnio_flag;
    uint64_t            qnio_size;
    char                *qnio_channel;
    char                *target;
    qnio_callback_t     qnio_cb;
} qemu2qnio_ctx_t;

typedef qemu2qnio_ctx_t qnio2qemu_ctx_t;

typedef struct LibQNIOSymbol {
        const char *name;
        gpointer *addr;
} LibQNIOSymbol;

/*
 * HyperScale AIO callbacks structure
 */
typedef struct VXHSAIOCB {
    BlockAIOCB          common;
    size_t              ret;
    size_t              size;
    QEMUBH              *bh;
    int                 aio_done;
    int                 segments;
    int                 flags;
    size_t              io_offset;
    QEMUIOVector        *qiov;
    void                *buffer;
    int                 direction;  /* IO direction (r/w) */
    QSIMPLEQ_ENTRY(VXHSAIOCB) retry_entry;
} VXHSAIOCB;

typedef struct VXHSvDiskHostsInfo {
    int                 qnio_cfd;   /* Channel FD */
    int                 vdisk_rfd;  /* vDisk remote FD */
    char                *hostip;    /* Host's IP addresses */
    int                 port;       /* Host's port number */
} VXHSvDiskHostsInfo;

/*
 * Structure per vDisk maintained for state
 */
typedef struct BDRVVXHSState {
    int                     fds[2];
    int64_t                 vdisk_size;
    int64_t                 vdisk_blocks;
    int64_t                 vdisk_flags;
    int                     vdisk_aio_count;
    int                     event_reader_pos;
    VXHSAIOCB               *qnio_event_acb;
    void                    *qnio_ctx;
    QemuSpin                vdisk_lock; /* Lock to protect BDRVVXHSState */
    QemuSpin                vdisk_acb_lock;  /* Protects ACB */
    VXHSvDiskHostsInfo      vdisk_hostinfo[VXHS_MAX_HOSTS]; /* Per host info */
    int                     vdisk_nhosts;   /* Total number of hosts */
    int                     vdisk_cur_host_idx; /* IOs are being shipped to */
    int                     vdisk_ask_failover_idx; /*asking permsn to ship io*/
    QSIMPLEQ_HEAD(aio_retryq, VXHSAIOCB) vdisk_aio_retryq;
    int                     vdisk_aio_retry_qd; /* Currently for debugging */
    char                    *vdisk_guid;
} BDRVVXHSState;

static int vxhs_restart_aio(VXHSAIOCB *acb);
static void vxhs_check_failover_status(int res, void *ctx);

static void vxhs_inc_acb_segment_count(void *ptr, int count)
{
    VXHSAIOCB *acb = ptr;
    BDRVVXHSState *s = acb->common.bs->opaque;

    qemu_spin_lock(&s->vdisk_acb_lock);
    acb->segments += count;
    qemu_spin_unlock(&s->vdisk_acb_lock);
}

static void vxhs_dec_acb_segment_count(void *ptr, int count)
{
    VXHSAIOCB *acb = ptr;
    BDRVVXHSState *s = acb->common.bs->opaque;

    qemu_spin_lock(&s->vdisk_acb_lock);
    acb->segments -= count;
    qemu_spin_unlock(&s->vdisk_acb_lock);
}

static void vxhs_set_acb_buffer(void *ptr, void *buffer)
{
    VXHSAIOCB *acb = ptr;

    acb->buffer = buffer;
}

static void vxhs_inc_vdisk_iocount(void *ptr, uint32_t count)
{
    BDRVVXHSState *s = ptr;

    qemu_spin_lock(&s->vdisk_lock);
    s->vdisk_aio_count += count;
    qemu_spin_unlock(&s->vdisk_lock);
}

static void vxhs_dec_vdisk_iocount(void *ptr, uint32_t count)
{
    BDRVVXHSState *s = ptr;

    qemu_spin_lock(&s->vdisk_lock);
    s->vdisk_aio_count -= count;
    qemu_spin_unlock(&s->vdisk_lock);
}

static int32_t
vxhs_qnio_iio_ioctl(void *apictx, uint32_t rfd, uint32_t opcode, int64_t *in,
                    void *ctx, uint32_t flags)
{
    int   ret = 0;

    switch (opcode) {
    case VDISK_STAT:
        ret = iio_ioctl(apictx, rfd, IOR_VDISK_STAT,
                                     in, ctx, flags);
        break;

    case VDISK_AIO_FLUSH:
        ret = iio_ioctl(apictx, rfd, IOR_VDISK_FLUSH,
                                     in, ctx, flags);
        break;

    case VDISK_CHECK_IO_FAILOVER_READY:
        ret = iio_ioctl(apictx, rfd, IOR_VDISK_CHECK_IO_FAILOVER_READY,
                                     in, ctx, flags);
        break;

    default:
        ret = -ENOTSUP;
        break;
    }

    if (ret) {
        *in = 0;
        trace_vxhs_qnio_iio_ioctl(opcode);
    }

    return ret;
}

static void vxhs_qnio_iio_close(BDRVVXHSState *s, int idx)
{
    /*
     * Close vDisk device
     */
    if (s->vdisk_hostinfo[idx].vdisk_rfd >= 0) {
        iio_devclose(s->qnio_ctx, 0, s->vdisk_hostinfo[idx].vdisk_rfd);
        s->vdisk_hostinfo[idx].vdisk_rfd = -1;
    }

    /*
     * Close QNIO channel against cached channel-fd
     */
    if (s->vdisk_hostinfo[idx].qnio_cfd >= 0) {
        iio_close(s->qnio_ctx, s->vdisk_hostinfo[idx].qnio_cfd);
        s->vdisk_hostinfo[idx].qnio_cfd = -1;
    }
}

static int vxhs_qnio_iio_open(int *cfd, const char *of_vsa_addr,
                              int *rfd, const char *file_name)
{
    /*
     * Open qnio channel to storage agent if not opened before.
     */
    if (*cfd < 0) {
        *cfd = iio_open(global_qnio_ctx, of_vsa_addr, 0);
        if (*cfd < 0) {
            trace_vxhs_qnio_iio_open(of_vsa_addr);
            return -ENODEV;
        }
    }

    /*
     * Open vdisk device
     */
    *rfd = iio_devopen(global_qnio_ctx, *cfd, file_name, 0);

    if (*rfd < 0) {
        if (*cfd >= 0) {
            iio_close(global_qnio_ctx, *cfd);
            *cfd = -1;
            *rfd = -1;
        }

        trace_vxhs_qnio_iio_devopen(file_name);
        return -ENODEV;
    }

    return 0;
}

/*
 * Try to reopen the vDisk on one of the available hosts
 * If vDisk reopen is successful on any of the host then
 * check if that node is ready to accept I/O.
 */
static int vxhs_reopen_vdisk(BDRVVXHSState *s, int index)
{
    VXHSvDiskHostsInfo hostinfo = s->vdisk_hostinfo[index];
    char *of_vsa_addr = NULL;
    char *file_name = NULL;
    int  res = 0;

    /*
     * Close stale vdisk device remote-fd and channel-fd
     * since it could be invalid after a channel disconnect.
     * We will reopen the vdisk later to get the new fd.
     */
    vxhs_qnio_iio_close(s, index);

    /*
     * Build storage agent address and vdisk device name strings
     */
    file_name = g_strdup_printf("%s%s", vdisk_prefix, s->vdisk_guid);
    of_vsa_addr = g_strdup_printf("of://%s:%d",
                                  hostinfo.hostip, hostinfo.port);

    res = vxhs_qnio_iio_open(&hostinfo.qnio_cfd, of_vsa_addr,
                             &hostinfo.vdisk_rfd, file_name);

    g_free(of_vsa_addr);
    g_free(file_name);
    return res;
}

static void vxhs_fail_aio(VXHSAIOCB *acb, int err)
{
    BDRVVXHSState *s = NULL;
    int segcount = 0;
    int rv = 0;

    s = acb->common.bs->opaque;

    trace_vxhs_fail_aio(s->vdisk_guid, acb);
    if (!acb->ret) {
        acb->ret = err;
    }
    qemu_spin_lock(&s->vdisk_acb_lock);
    segcount = acb->segments;
    qemu_spin_unlock(&s->vdisk_acb_lock);
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

static int vxhs_handle_queued_ios(BDRVVXHSState *s)
{
    VXHSAIOCB *acb = NULL;
    int res = 0;

    qemu_spin_lock(&s->vdisk_lock);
    while ((acb = QSIMPLEQ_FIRST(&s->vdisk_aio_retryq)) != NULL) {
        /*
         * Before we process the acb, check whether I/O failover
         * started again due to failback or cascading failure.
         */
        if (OF_VDISK_IOFAILOVER_IN_PROGRESS(s)) {
            qemu_spin_unlock(&s->vdisk_lock);
            goto out;
        }
        QSIMPLEQ_REMOVE_HEAD(&s->vdisk_aio_retryq, retry_entry);
        s->vdisk_aio_retry_qd--;
        OF_AIOCB_FLAGS_RESET_QUEUED(acb);
        if (OF_VDISK_FAILED(s)) {
            qemu_spin_unlock(&s->vdisk_lock);
            vxhs_fail_aio(acb, EIO);
            qemu_spin_lock(&s->vdisk_lock);
        } else {
            qemu_spin_unlock(&s->vdisk_lock);
            res = vxhs_restart_aio(acb);
            trace_vxhs_handle_queued_ios(acb, res);
            qemu_spin_lock(&s->vdisk_lock);
            if (res) {
                QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq,
                                     acb, retry_entry);
                OF_AIOCB_FLAGS_SET_QUEUED(acb);
                qemu_spin_unlock(&s->vdisk_lock);
                goto out;
            }
        }
    }
    qemu_spin_unlock(&s->vdisk_lock);
out:
    return res;
}

/*
 * If errors are consistent with storage agent failure:
 *  - Try to reconnect in case error is transient or storage agent restarted.
 *  - Currently failover is being triggered on per vDisk basis. There is
 *    a scope of further optimization where failover can be global (per VM).
 *  - In case of network (storage agent) failure, for all the vDisks, having
 *    no redundancy, I/Os will be failed without attempting for I/O failover
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

static int vxhs_switch_storage_agent(BDRVVXHSState *s)
{
    int res = 0;
    int flags = (IIO_FLAG_ASYNC | IIO_FLAG_DONE);

    trace_vxhs_switch_storage_agent(
              s->vdisk_hostinfo[s->vdisk_ask_failover_idx].hostip,
              s->vdisk_guid);

    res = vxhs_reopen_vdisk(s, s->vdisk_ask_failover_idx);
    if (res == 0) {
        res = vxhs_qnio_iio_ioctl(s->qnio_ctx,
                  s->vdisk_hostinfo[s->vdisk_ask_failover_idx].vdisk_rfd,
                  VDISK_CHECK_IO_FAILOVER_READY, NULL, s, flags);
    } else {
        trace_vxhs_switch_storage_agent_failed(
                  s->vdisk_hostinfo[s->vdisk_ask_failover_idx].hostip,
                  s->vdisk_guid, res, errno);
        /*
         * Try the next host.
         * Calling vxhs_check_failover_status from here ties up the qnio
         * epoll loop if vxhs_qnio_iio_ioctl fails synchronously (-1)
         * for all the hosts in the IO target list.
         */

        vxhs_check_failover_status(res, s);
    }
    return res;
}

static void vxhs_check_failover_status(int res, void *ctx)
{
    BDRVVXHSState *s = ctx;

    if (res == 0) {
        /* found failover target */
        s->vdisk_cur_host_idx = s->vdisk_ask_failover_idx;
        s->vdisk_ask_failover_idx = 0;
        trace_vxhs_check_failover_status(
                   s->vdisk_hostinfo[s->vdisk_cur_host_idx].hostip,
                   s->vdisk_guid);
        qemu_spin_lock(&s->vdisk_lock);
        OF_VDISK_RESET_IOFAILOVER_IN_PROGRESS(s);
        qemu_spin_unlock(&s->vdisk_lock);
        vxhs_handle_queued_ios(s);
    } else {
        /* keep looking */
        trace_vxhs_check_failover_status_retry(s->vdisk_guid);
        s->vdisk_ask_failover_idx++;
        if (s->vdisk_ask_failover_idx == s->vdisk_nhosts) {
            /* pause and cycle through list again */
            sleep(QNIO_CONNECT_RETRY_SECS);
            s->vdisk_ask_failover_idx = 0;
        }
        res = vxhs_switch_storage_agent(s);
    }
}

static int vxhs_failover_io(BDRVVXHSState *s)
{
    int res = 0;

    trace_vxhs_failover_io(s->vdisk_guid);

    s->vdisk_ask_failover_idx = 0;
    res = vxhs_switch_storage_agent(s);

    return res;
}

static void vxhs_iio_callback(int32_t rfd, uint32_t reason, void *ctx,
                       uint32_t error, uint32_t opcode)
{
    VXHSAIOCB *acb = NULL;
    BDRVVXHSState *s = NULL;
    int rv = 0;
    int segcount = 0;

    switch (opcode) {
    case IRP_READ_REQUEST:
    case IRP_WRITE_REQUEST:

    /*
     * ctx is VXHSAIOCB*
     * ctx is NULL if error is QNIOERROR_CHANNEL_HUP or reason is IIO_REASON_HUP
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
            if (error == QNIOERROR_RETRY_ON_SOURCE || error == QNIOERROR_HUP ||
                error == QNIOERROR_CHANNEL_HUP || error == -1) {
                /*
                 * Start vDisk IO failover once callback is
                 * called against all the pending IOs.
                 * If vDisk has no redundancy enabled
                 * then IO failover routine will mark
                 * the vDisk failed and fail all the
                 * AIOs without retry (stateless vDisk)
                 */
                qemu_spin_lock(&s->vdisk_lock);
                if (!OF_VDISK_IOFAILOVER_IN_PROGRESS(s)) {
                    OF_VDISK_SET_IOFAILOVER_IN_PROGRESS(s);
                }
                /*
                 * Check if this acb is already queued before.
                 * It is possible in case if I/Os are submitted
                 * in multiple segments (QNIO_MAX_IO_SIZE).
                 */
                qemu_spin_lock(&s->vdisk_acb_lock);
                if (!OF_AIOCB_FLAGS_QUEUED(acb)) {
                    QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq,
                                         acb, retry_entry);
                    OF_AIOCB_FLAGS_SET_QUEUED(acb);
                    s->vdisk_aio_retry_qd++;
                    trace_vxhs_iio_callback_retry(s->vdisk_guid, acb);
                }
                segcount = --acb->segments;
                qemu_spin_unlock(&s->vdisk_acb_lock);
                /*
                 * Decrement AIO count only when callback is called
                 * against all the segments of aiocb.
                 */
                if (segcount == 0 && --s->vdisk_aio_count == 0) {
                    /*
                     * Start vDisk I/O failover
                     */
                    qemu_spin_unlock(&s->vdisk_lock);
                    /*
                     * TODO:
                     * Need to explore further if it is possible to optimize
                     * the failover operation on Virtual-Machine (global)
                     * specific rather vDisk specific.
                     */
                    vxhs_failover_io(s);
                    goto out;
                }
                qemu_spin_unlock(&s->vdisk_lock);
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
            trace_vxhs_iio_callback_fail(reason, acb, acb->segments,
                                         acb->size, error);
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
    qemu_spin_lock(&s->vdisk_acb_lock);
    if (error && !acb->ret) {
        acb->ret = error;
    }
    --acb->segments;
    segcount = acb->segments;
    assert(segcount >= 0);
    qemu_spin_unlock(&s->vdisk_acb_lock);
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
        trace_vxhs_iio_callback_ready(((BDRVVXHSState *)ctx)->vdisk_guid,
                                      error);
        vxhs_check_failover_status(error, ctx);
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

static void vxhs_complete_aio(VXHSAIOCB *acb, BDRVVXHSState *s)
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
        qemu_vfree(acb->buffer);
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
static void vxhs_aio_event_reader(void *opaque)
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
 * Call QNIO operation to create channels to do IO on vDisk.
 */

static void *vxhs_setup_qnio(void)
{
    void *qnio_ctx = NULL;

    qnio_ctx = iio_init(vxhs_iio_callback);

    if (qnio_ctx != NULL) {
        trace_vxhs_setup_qnio(qnio_ctx);
    } else {
        trace_vxhs_setup_qnio_nwerror('.');
    }

    return qnio_ctx;
}

/*
 * This helper function converts an array of iovectors into a flat buffer.
 */

static void *vxhs_convert_iovector_to_buffer(QEMUIOVector *qiov)
{
    void *buf = NULL;
    size_t size = 0;

    if (qiov->niov == 0) {
        return buf;
    }

    size = qiov->size;
    buf = qemu_try_memalign(BDRV_SECTOR_SIZE, size);
    if (!buf) {
        trace_vxhs_convert_iovector_to_buffer(size);
        errno = -ENOMEM;
        return NULL;
    }
    return buf;
}

/*
 * This helper function iterates over the iovector and checks
 * if the length of every element is an integral multiple
 * of the sector size.
 * Return Value:
 *      On Success : true
 *      On Failure : false
 */
static int vxhs_is_iovector_read_aligned(QEMUIOVector *qiov, size_t sector)
{
    struct iovec *iov = qiov->iov;
    int niov = qiov->niov;
    int i;

    for (i = 0; i < niov; i++) {
        if (iov[i].iov_len % sector != 0) {
            return false;
        }
    }
    return true;
}

static int32_t
vxhs_qnio_iio_writev(void *qnio_ctx, uint32_t rfd, QEMUIOVector *qiov,
                     uint64_t offset, void *ctx, uint32_t flags)
{
    struct iovec cur;
    uint64_t cur_offset = 0;
    uint64_t cur_write_len = 0;
    int segcount = 0;
    int ret = 0;
    int i, nsio = 0;
    int iovcnt = qiov->niov;
    struct iovec *iov = qiov->iov;

    errno = 0;
    cur.iov_base = 0;
    cur.iov_len = 0;

    ret = iio_writev(qnio_ctx, rfd, iov, iovcnt, offset, ctx, flags);

    if (ret == -1 && errno == EFBIG) {
        trace_vxhs_qnio_iio_writev(ret);
        /*
         * IO size is larger than IIO_IO_BUF_SIZE hence need to
         * split the I/O at IIO_IO_BUF_SIZE boundary
         * There are two cases here:
         *  1. iovcnt is 1 and IO size is greater than IIO_IO_BUF_SIZE
         *  2. iovcnt is greater than 1 and IO size is greater than
         *     IIO_IO_BUF_SIZE.
         *
         * Need to adjust the segment count, for that we need to compute
         * the segment count and increase the segment count in one shot
         * instead of setting iteratively in for loop. It is required to
         * prevent any race between the splitted IO submission and IO
         * completion.
         */
        cur_offset = offset;
        for (i = 0; i < iovcnt; i++) {
            if (iov[i].iov_len <= IIO_IO_BUF_SIZE && iov[i].iov_len > 0) {
                cur_offset += iov[i].iov_len;
                nsio++;
            } else if (iov[i].iov_len > 0) {
                cur.iov_base = iov[i].iov_base;
                cur.iov_len = IIO_IO_BUF_SIZE;
                cur_write_len = 0;
                while (1) {
                    nsio++;
                    cur_write_len += cur.iov_len;
                    if (cur_write_len == iov[i].iov_len) {
                        break;
                    }
                    cur_offset += cur.iov_len;
                    cur.iov_base += cur.iov_len;
                    if ((iov[i].iov_len - cur_write_len) > IIO_IO_BUF_SIZE) {
                        cur.iov_len = IIO_IO_BUF_SIZE;
                    } else {
                        cur.iov_len = (iov[i].iov_len - cur_write_len);
                    }
                }
            }
        }

        segcount = nsio - 1;
        vxhs_inc_acb_segment_count(ctx, segcount);
        /*
         * Split the IO and submit it to QNIO.
         * Reset the cur_offset before splitting the IO.
         */
        cur_offset = offset;
        nsio = 0;
        for (i = 0; i < iovcnt; i++) {
            if (iov[i].iov_len <= IIO_IO_BUF_SIZE && iov[i].iov_len > 0) {
                errno = 0;
                ret = iio_writev(qnio_ctx, rfd, &iov[i], 1, cur_offset, ctx,
                                 flags);
                if (ret == -1) {
                    trace_vxhs_qnio_iio_writev_err(i, iov[i].iov_len, errno);
                    /*
                     * Need to adjust the AIOCB segment count to prevent
                     * blocking of AIOCB completion within QEMU block driver.
                     */
                    if (segcount > 0 && (segcount - nsio) > 0) {
                        vxhs_dec_acb_segment_count(ctx, segcount - nsio);
                    }
                    return ret;
                }
                cur_offset += iov[i].iov_len;
                nsio++;
            } else if (iov[i].iov_len > 0) {
                /*
                 * This case is where one element of the io vector is > 4MB.
                 */
                cur.iov_base = iov[i].iov_base;
                cur.iov_len = IIO_IO_BUF_SIZE;
                cur_write_len = 0;
                while (1) {
                    nsio++;
                    errno = 0;
                    ret = iio_writev(qnio_ctx, rfd, &cur, 1, cur_offset, ctx,
                                     flags);
                    if (ret == -1) {
                        trace_vxhs_qnio_iio_writev_err(i, cur.iov_len, errno);
                        /*
                         * Need to adjust the AIOCB segment count to prevent
                         * blocking of AIOCB completion within the
                         * QEMU block driver.
                         */
                        if (segcount > 0 && (segcount - nsio) > 0) {
                            vxhs_dec_acb_segment_count(ctx, segcount - nsio);
                        }
                        return ret;
                    }

                    cur_write_len += cur.iov_len;
                    if (cur_write_len == iov[i].iov_len) {
                        break;
                    }
                    cur_offset += cur.iov_len;
                    cur.iov_base += cur.iov_len;
                    if ((iov[i].iov_len - cur_write_len) >
                                                IIO_IO_BUF_SIZE) {
                        cur.iov_len = IIO_IO_BUF_SIZE;
                    } else {
                        cur.iov_len = (iov[i].iov_len - cur_write_len);
                    }
                }
            }
        }
    }
    return ret;
}

/*
 * Iterate over the i/o vector and send read request
 * to QNIO one by one.
 */
static int32_t
vxhs_qnio_iio_readv(void *qnio_ctx, uint32_t rfd, QEMUIOVector *qiov,
               uint64_t offset, void *ctx, uint32_t flags)
{
    uint64_t read_offset = offset;
    void *buffer = NULL;
    size_t size;
    int aligned, segcount;
    int i, ret = 0;
    int iovcnt = qiov->niov;
    struct iovec *iov = qiov->iov;

    aligned = vxhs_is_iovector_read_aligned(qiov, BDRV_SECTOR_SIZE);
    size = qiov->size;

    if (!aligned) {
        buffer = vxhs_convert_iovector_to_buffer(qiov);
        if (buffer == NULL) {
            return -ENOMEM;
        }

        errno = 0;
        ret = iio_read(qnio_ctx, rfd, buffer, size, read_offset, ctx, flags);
        if (ret != 0) {
            trace_vxhs_qnio_iio_readv(ctx, ret, errno);
            qemu_vfree(buffer);
            return ret;
        }
        vxhs_set_acb_buffer(ctx, buffer);
        return ret;
    }

    /*
     * Since read IO request is going to split based on
     * number of IOvectors hence increment the segment
     * count depending on the number of IOVectors before
     * submitting the read request to QNIO.
     * This is needed to protect the QEMU block driver
     * IO completion while read request for the same IO
     * is being submitted to QNIO.
     */
    segcount = iovcnt - 1;
    if (segcount > 0) {
        vxhs_inc_acb_segment_count(ctx, segcount);
    }

    for (i = 0; i < iovcnt; i++) {
        errno = 0;
        ret = iio_read(qnio_ctx, rfd, iov[i].iov_base, iov[i].iov_len,
                       read_offset, ctx, flags);
        if (ret != 0) {
            trace_vxhs_qnio_iio_readv(ctx, ret, errno);
            /*
             * Need to adjust the AIOCB segment count to prevent
             * blocking of AIOCB completion within QEMU block driver.
             */
            if (segcount > 0 && (segcount - i) > 0) {
                vxhs_dec_acb_segment_count(ctx, segcount - i);
            }
            return ret;
        }
        read_offset += iov[i].iov_len;
    }

    return ret;
}

static int vxhs_restart_aio(VXHSAIOCB *acb)
{
    BDRVVXHSState *s = NULL;
    int iio_flags = 0;
    int res = 0;

    s = acb->common.bs->opaque;

    if (acb->direction == VDISK_AIO_WRITE) {
        vxhs_inc_vdisk_iocount(s, 1);
        vxhs_inc_acb_segment_count(acb, 1);
        iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);
        res = vxhs_qnio_iio_writev(s->qnio_ctx,
                s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                acb->qiov, acb->io_offset, (void *)acb, iio_flags);
    }

    if (acb->direction == VDISK_AIO_READ) {
        vxhs_inc_vdisk_iocount(s, 1);
        vxhs_inc_acb_segment_count(acb, 1);
        iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);
        res = vxhs_qnio_iio_readv(s->qnio_ctx,
                s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                acb->qiov, acb->io_offset, (void *)acb, iio_flags);
    }

    if (res != 0) {
        vxhs_dec_vdisk_iocount(s, 1);
        vxhs_dec_acb_segment_count(acb, 1);
        trace_vxhs_restart_aio(acb->direction, res, errno);
    }

    return res;
}

static QemuOptsList runtime_opts = {
    .name = "vxhs",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = VXHS_OPT_FILENAME,
            .type = QEMU_OPT_STRING,
            .help = "URI to the Veritas HyperScale image",
        },
        {
            .name = VXHS_OPT_VDISK_ID,
            .type = QEMU_OPT_STRING,
            .help = "UUID of the VxHS vdisk",
        },
        { /* end of list */ }
    },
};

static QemuOptsList runtime_tcp_opts = {
    .name = "vxhs_tcp",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_tcp_opts.head),
    .desc = {
        {
            .name = VXHS_OPT_HOST,
            .type = QEMU_OPT_STRING,
            .help = "host address (ipv4 addresses)",
        },
        {
            .name = VXHS_OPT_PORT,
            .type = QEMU_OPT_NUMBER,
            .help = "port number on which VxHSD is listening (default 9999)",
            .def_value_str = "9999"
        },
        { /* end of list */ }
    },
};

/*
 * Parse the incoming URI and populate *options with the host information.
 * URI syntax has the limitation of supporting only one host info.
 * To pass multiple host information, use the JSON syntax.
 */
static int vxhs_parse_uri(const char *filename, QDict *options)
{
    URI *uri = NULL;
    char *hoststr, *portstr;
    char *port;
    int ret = 0;

    trace_vxhs_parse_uri_filename(filename);

    uri = uri_parse(filename);
    if (!uri || !uri->server || !uri->path) {
        uri_free(uri);
        return -EINVAL;
    }

    hoststr = g_strdup(VXHS_OPT_SERVER"0.host");
    qdict_put(options, hoststr, qstring_from_str(uri->server));
    g_free(hoststr);

    portstr = g_strdup(VXHS_OPT_SERVER"0.port");
    if (uri->port) {
        port = g_strdup_printf("%d", uri->port);
        qdict_put(options, portstr, qstring_from_str(port));
        g_free(port);
    }
    g_free(portstr);

    if (strstr(uri->path, "vxhs") == NULL) {
        qdict_put(options, "vdisk_id", qstring_from_str(uri->path));
    }

    trace_vxhs_parse_uri_hostinfo(1, uri->server, uri->port);
    uri_free(uri);

    return ret;
}

static void vxhs_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    if (qdict_haskey(options, "vdisk_id")
        || qdict_haskey(options, "server"))
    {
        error_setg(errp, "vdisk_id/server and a file name may not be specified "
                         "at the same time");
        return;
    }

    if (strstr(filename, "://")) {
        int ret = vxhs_parse_uri(filename, options);
        if (ret < 0) {
            error_setg(errp, "Invalid URI. URI should be of the form "
                       "  vxhs://<host_ip>:<port>/{<vdisk_id>}");
        }
    }
}

static int vxhs_qemu_init(QDict *options, BDRVVXHSState *s,
                              int *cfd, int *rfd, Error **errp)
{
    QDict *backing_options = NULL;
    QemuOpts *opts, *tcp_opts;
    const char *vxhs_filename;
    char *of_vsa_addr = NULL;
    Error *local_err = NULL;
    const char *vdisk_id_opt;
    char *file_name = NULL;
    size_t num_servers = 0;
    char *str = NULL;
    int ret = 0;
    int i;

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto out;
    }

    vxhs_filename = qemu_opt_get(opts, VXHS_OPT_FILENAME);
    if (vxhs_filename) {
        trace_vxhs_qemu_init_filename(vxhs_filename);
    }

    vdisk_id_opt = qemu_opt_get(opts, VXHS_OPT_VDISK_ID);
    if (!vdisk_id_opt) {
        error_setg(&local_err, QERR_MISSING_PARAMETER, VXHS_OPT_VDISK_ID);
        ret = -EINVAL;
        goto out;
    }
    s->vdisk_guid = g_strdup(vdisk_id_opt);
    trace_vxhs_qemu_init_vdisk(vdisk_id_opt);

    num_servers = qdict_array_entries(options, VXHS_OPT_SERVER);
    if (num_servers < 1) {
        error_setg(&local_err, QERR_MISSING_PARAMETER, "server");
        ret = -EINVAL;
        goto out;
    } else if (num_servers > VXHS_MAX_HOSTS) {
        error_setg(&local_err, QERR_INVALID_PARAMETER, "server");
        error_append_hint(errp, "Maximum %d servers allowed.\n",
                          VXHS_MAX_HOSTS);
        ret = -EINVAL;
        goto out;
    }
    trace_vxhs_qemu_init_numservers(num_servers);

    for (i = 0; i < num_servers; i++) {
        str = g_strdup_printf(VXHS_OPT_SERVER"%d.", i);
        qdict_extract_subqdict(options, &backing_options, str);

        /* Create opts info from runtime_tcp_opts list */
        tcp_opts = qemu_opts_create(&runtime_tcp_opts, NULL, 0, &error_abort);
        qemu_opts_absorb_qdict(tcp_opts, backing_options, &local_err);
        if (local_err) {
            qdict_del(backing_options, str);
            qemu_opts_del(tcp_opts);
            g_free(str);
            ret = -EINVAL;
            goto out;
        }

        s->vdisk_hostinfo[i].hostip = g_strdup(qemu_opt_get(tcp_opts,
                                                            VXHS_OPT_HOST));
        s->vdisk_hostinfo[i].port = g_ascii_strtoll(qemu_opt_get(tcp_opts,
                                                                 VXHS_OPT_PORT),
                                                    NULL, 0);

        s->vdisk_hostinfo[i].qnio_cfd = -1;
        s->vdisk_hostinfo[i].vdisk_rfd = -1;
        trace_vxhs_qemu_init(s->vdisk_hostinfo[i].hostip,
                             s->vdisk_hostinfo[i].port);

        qdict_del(backing_options, str);
        qemu_opts_del(tcp_opts);
        g_free(str);
    }

    s->vdisk_nhosts = i;
    s->vdisk_cur_host_idx = 0;
    file_name = g_strdup_printf("%s%s", vdisk_prefix, s->vdisk_guid);
    of_vsa_addr = g_strdup_printf("of://%s:%d",
                                s->vdisk_hostinfo[s->vdisk_cur_host_idx].hostip,
                                s->vdisk_hostinfo[s->vdisk_cur_host_idx].port);

    /*
     * .bdrv_open() and .bdrv_create() run under the QEMU global mutex.
     */
    if (global_qnio_ctx == NULL) {
        global_qnio_ctx = vxhs_setup_qnio();
        if (global_qnio_ctx == NULL) {
            error_setg(&local_err, "Failed vxhs_setup_qnio");
            ret = -EINVAL;
            goto out;
        }
    }

    ret = vxhs_qnio_iio_open(cfd, of_vsa_addr, rfd, file_name);
    if (!ret) {
        error_setg(&local_err, "Failed qnio_iio_open");
        ret = -EIO;
    }

out:
    g_free(file_name);
    g_free(of_vsa_addr);
    qemu_opts_del(opts);

    if (ret < 0) {
        for (i = 0; i < num_servers; i++) {
            g_free(s->vdisk_hostinfo[i].hostip);
        }
        g_free(s->vdisk_guid);
        s->vdisk_guid = NULL;
        errno = -ret;
    }
    error_propagate(errp, local_err);

    return ret;
}

static int vxhs_open(BlockDriverState *bs, QDict *options,
              int bdrv_flags, Error **errp)
{
    BDRVVXHSState *s = bs->opaque;
    AioContext *aio_context;
    int qemu_qnio_cfd = -1;
    int device_opened = 0;
    int qemu_rfd = -1;
    int ret = 0;
    int i;

    ret = vxhs_qemu_init(options, s, &qemu_qnio_cfd, &qemu_rfd, errp);
    if (ret < 0) {
        trace_vxhs_open_fail(ret);
        return ret;
    }

    device_opened = 1;
    s->qnio_ctx = global_qnio_ctx;
    s->vdisk_hostinfo[0].qnio_cfd = qemu_qnio_cfd;
    s->vdisk_hostinfo[0].vdisk_rfd = qemu_rfd;
    s->vdisk_size = 0;
    QSIMPLEQ_INIT(&s->vdisk_aio_retryq);

    /*
     * Create a pipe for communicating between two threads in different
     * context. Set handler for read event, which gets triggered when
     * IO completion is done by non-QEMU context.
     */
    ret = qemu_pipe(s->fds);
    if (ret < 0) {
        trace_vxhs_open_epipe('.');
        ret = -errno;
        goto errout;
    }
    fcntl(s->fds[VDISK_FD_READ], F_SETFL, O_NONBLOCK);

    aio_context = bdrv_get_aio_context(bs);
    aio_set_fd_handler(aio_context, s->fds[VDISK_FD_READ],
                       false, vxhs_aio_event_reader, NULL, s);

    /*
     * Initialize the spin-locks.
     */
    qemu_spin_init(&s->vdisk_lock);
    qemu_spin_init(&s->vdisk_acb_lock);

    return 0;

errout:
    /*
     * Close remote vDisk device if it was opened earlier
     */
    if (device_opened) {
        for (i = 0; i < s->vdisk_nhosts; i++) {
            vxhs_qnio_iio_close(s, i);
        }
    }
    trace_vxhs_open_fail(ret);
    return ret;
}

static const AIOCBInfo vxhs_aiocb_info = {
    .aiocb_size = sizeof(VXHSAIOCB)
};

/*
 * This allocates QEMU-VXHS callback for each IO
 * and is passed to QNIO. When QNIO completes the work,
 * it will be passed back through the callback.
 */
static BlockAIOCB *vxhs_aio_rw(BlockDriverState *bs,
                                int64_t sector_num, QEMUIOVector *qiov,
                                int nb_sectors,
                                BlockCompletionFunc *cb,
                                void *opaque, int iodir)
{
    VXHSAIOCB *acb = NULL;
    BDRVVXHSState *s = bs->opaque;
    size_t size;
    uint64_t offset;
    int iio_flags = 0;
    int ret = 0;
    void *qnio_ctx = s->qnio_ctx;
    uint32_t rfd = s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd;

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

    qemu_spin_lock(&s->vdisk_lock);
    if (OF_VDISK_FAILED(s)) {
        trace_vxhs_aio_rw(s->vdisk_guid, iodir, size, offset);
        qemu_spin_unlock(&s->vdisk_lock);
        goto errout;
    }
    if (OF_VDISK_IOFAILOVER_IN_PROGRESS(s)) {
        QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq, acb, retry_entry);
        s->vdisk_aio_retry_qd++;
        OF_AIOCB_FLAGS_SET_QUEUED(acb);
        qemu_spin_unlock(&s->vdisk_lock);
        trace_vxhs_aio_rw_retry(s->vdisk_guid, acb, 1);
        goto out;
    }
    s->vdisk_aio_count++;
    qemu_spin_unlock(&s->vdisk_lock);

    iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);

    switch (iodir) {
    case VDISK_AIO_WRITE:
            vxhs_inc_acb_segment_count(acb, 1);
            ret = vxhs_qnio_iio_writev(qnio_ctx, rfd, qiov,
                                       offset, (void *)acb, iio_flags);
            break;
    case VDISK_AIO_READ:
            vxhs_inc_acb_segment_count(acb, 1);
            ret = vxhs_qnio_iio_readv(qnio_ctx, rfd, qiov,
                                       offset, (void *)acb, iio_flags);
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
         * redundancy or stateful storage on compute
         *
         * TODO: Revisit this code path to see if any
         *       particular error needs to be handled.
         *       At this moment failing the I/O.
         */
        qemu_spin_lock(&s->vdisk_lock);
        if (s->vdisk_nhosts == 1) {
            trace_vxhs_aio_rw_iofail(s->vdisk_guid);
            s->vdisk_aio_count--;
            vxhs_dec_acb_segment_count(acb, 1);
            qemu_spin_unlock(&s->vdisk_lock);
            goto errout;
        }
        if (OF_VDISK_FAILED(s)) {
            trace_vxhs_aio_rw_devfail(
                      s->vdisk_guid, iodir, size, offset);
            s->vdisk_aio_count--;
            vxhs_dec_acb_segment_count(acb, 1);
            qemu_spin_unlock(&s->vdisk_lock);
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
            qemu_spin_unlock(&s->vdisk_lock);
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
            qemu_spin_unlock(&s->vdisk_lock);
            /*
             * Start IO failover
             */
            vxhs_failover_io(s);
            goto out;
        }
        qemu_spin_unlock(&s->vdisk_lock);
    }

out:
    return &acb->common;

errout:
    qemu_aio_unref(acb);
    return NULL;
}

static BlockAIOCB *vxhs_aio_readv(BlockDriverState *bs,
                                   int64_t sector_num, QEMUIOVector *qiov,
                                   int nb_sectors,
                                   BlockCompletionFunc *cb, void *opaque)
{
    return vxhs_aio_rw(bs, sector_num, qiov, nb_sectors,
                         cb, opaque, VDISK_AIO_READ);
}

static BlockAIOCB *vxhs_aio_writev(BlockDriverState *bs,
                                    int64_t sector_num, QEMUIOVector *qiov,
                                    int nb_sectors,
                                    BlockCompletionFunc *cb, void *opaque)
{
    return vxhs_aio_rw(bs, sector_num, qiov, nb_sectors,
                         cb, opaque, VDISK_AIO_WRITE);
}

static void vxhs_close(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;
    int i;

    trace_vxhs_close(s->vdisk_guid);
    close(s->fds[VDISK_FD_READ]);
    close(s->fds[VDISK_FD_WRITE]);

    /*
     * Clearing all the event handlers for oflame registered to QEMU
     */
    aio_set_fd_handler(bdrv_get_aio_context(bs), s->fds[VDISK_FD_READ],
                       false, NULL, NULL, NULL);
    g_free(s->vdisk_guid);
    s->vdisk_guid = NULL;

    for (i = 0; i < VXHS_MAX_HOSTS; i++) {
        vxhs_qnio_iio_close(s, i);
        /*
         * Free the dynamically allocated hostip string
         */
        g_free(s->vdisk_hostinfo[i].hostip);
        s->vdisk_hostinfo[i].hostip = NULL;
        s->vdisk_hostinfo[i].port = 0;
    }
}

/*
 * This is called by QEMU when a flush gets triggered from within
 * a guest at the block layer, either for IDE or SCSI disks.
 */
static int vxhs_co_flush(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;
    int64_t size = 0;
    int ret = 0;

    /*
     * VDISK_AIO_FLUSH ioctl is a no-op at present and will
     * always return success. This could change in the future.
     */
    ret = vxhs_qnio_iio_ioctl(s->qnio_ctx,
            s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
            VDISK_AIO_FLUSH, &size, NULL, IIO_FLAG_SYNC);

    if (ret < 0) {
        trace_vxhs_co_flush(s->vdisk_guid, ret, errno);
        vxhs_close(bs);
    }

    return ret;
}

static unsigned long vxhs_get_vdisk_stat(BDRVVXHSState *s)
{
    int64_t vdisk_size = 0;
    int ret = 0;

    ret = vxhs_qnio_iio_ioctl(s->qnio_ctx,
            s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
            VDISK_STAT, &vdisk_size, NULL, 0);

    if (ret < 0) {
        trace_vxhs_get_vdisk_stat_err(s->vdisk_guid, ret, errno);
        return 0;
    }

    trace_vxhs_get_vdisk_stat(s->vdisk_guid, vdisk_size);
    return vdisk_size;
}

/*
 * Returns the size of vDisk in bytes. This is required
 * by QEMU block upper block layer so that it is visible
 * to guest.
 */
static int64_t vxhs_getlength(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;
    int64_t vdisk_size = 0;

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
        return vdisk_size; /* return size in bytes */
    }

    return -EIO;
}

/*
 * Returns actual blocks allocated for the vDisk.
 * This is required by qemu-img utility.
 */
static int64_t vxhs_get_allocated_blocks(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;
    int64_t vdisk_size = 0;

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
        return vdisk_size; /* return size in bytes */
    }

    return -EIO;
}

static void vxhs_detach_aio_context(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;

    aio_set_fd_handler(bdrv_get_aio_context(bs), s->fds[VDISK_FD_READ],
                       false, NULL, NULL, NULL);

}

static void vxhs_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context)
{
    BDRVVXHSState *s = bs->opaque;

    aio_set_fd_handler(new_context, s->fds[VDISK_FD_READ],
                       false, vxhs_aio_event_reader, NULL, s);
}

static BlockDriver bdrv_vxhs = {
    .format_name                  = "vxhs",
    .protocol_name                = "vxhs",
    .instance_size                = sizeof(BDRVVXHSState),
    .bdrv_file_open               = vxhs_open,
    .bdrv_parse_filename          = vxhs_parse_filename,
    .bdrv_close                   = vxhs_close,
    .bdrv_getlength               = vxhs_getlength,
    .bdrv_get_allocated_file_size = vxhs_get_allocated_blocks,
    .bdrv_aio_readv               = vxhs_aio_readv,
    .bdrv_aio_writev              = vxhs_aio_writev,
    .bdrv_co_flush_to_disk        = vxhs_co_flush,
    .bdrv_detach_aio_context      = vxhs_detach_aio_context,
    .bdrv_attach_aio_context      = vxhs_attach_aio_context,
};

static void bdrv_vxhs_init(void)
{
    trace_vxhs_bdrv_init('.');
    bdrv_register(&bdrv_vxhs);
}

block_init(bdrv_vxhs_init);
