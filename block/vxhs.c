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
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"

#define VXHS_OPT_FILENAME        "filename"
#define VXHS_OPT_VDISK_ID        "vdisk_id"
#define VXHS_OPT_SERVER          "server."
#define VXHS_OPT_HOST            "host"
#define VXHS_OPT_PORT            "port"

/* qnio client ioapi_ctx */
static void *global_qnio_ctx;

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
        error = qnio_iio_extract_msg_error(m);
        opcode = qnio_iio_extract_msg_opcode(m);
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
                 * If vDisk has no redundancy enabled
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
        trace_vxhs_iio_callback_ready(((BDRVVXHSState *)ctx)->vdisk_guid,
                                      error);
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
 * Call QNIO operation to create channels to do IO on vDisk.
 */

void *vxhs_setup_qnio(void)
{
    void *qnio_ctx = NULL;

    qnio_ctx = qnio_iio_init(vxhs_iio_callback);

    if (qnio_ctx != NULL) {
        trace_vxhs_setup_qnio(qnio_ctx);
    } else {
        trace_vxhs_setup_qnio_nwerror('.');
    }

    return qnio_ctx;
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
        {
            .name = "to",
            .type = QEMU_OPT_NUMBER,
            .help = "max port number, not supported by VxHS",
        },
        {
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
            .help = "ipv4 bool value, not supported by VxHS",
        },
        {
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
            .help = "ipv6 bool value, not supported by VxHS",
        },
        { /* end of list */ }
    },
};

/*
 * Parse the incoming URI and populate *options with all the host(s)
 * information. Host at index 0 is local storage agent.
 * Remaining are the reflection target storage agents. The local storage agent
 * ip is the efficient internal address in the uri, e.g. 192.168.0.2.
 * The local storage agent address is stored at index 0. The reflection target
 * ips, are the E-W data network addresses of the reflection node agents, also
 * extracted from the uri.
 */
static int vxhs_parse_uri(const char *filename, QDict *options)
{
    gchar **target_list;
    URI *uri = NULL;
    char *hoststr, *portstr;
    char *vdisk_id = NULL;
    char *port;
    int ret = 0;
    int i = 0;

    trace_vxhs_parse_uri_filename(filename);
    target_list = g_strsplit(filename, "%7D", 0);
    assert(target_list != NULL && target_list[0] != NULL);

    for (i = 0; target_list[i] != NULL && *target_list[i]; i++) {
        uri = uri_parse(target_list[i]);
        if (!uri || !uri->server) {
            uri_free(uri);
            ret = -EINVAL;
            break;
        }

        hoststr = g_strdup_printf(VXHS_OPT_SERVER"%d.host", i);
        qdict_put(options, hoststr, qstring_from_str(uri->server));

        portstr = g_strdup_printf(VXHS_OPT_SERVER"%d.port", i);
        if (uri->port) {
            port = g_strdup_printf("%d", uri->port);
            qdict_put(options, portstr, qstring_from_str(port));
            g_free(port);
        }

        if (i == 0 && (strstr(uri->path, "vxhs") == NULL)) {
            vdisk_id = g_strdup_printf("%s%c", uri->path, '}');
            qdict_put(options, "vdisk_id", qstring_from_str(vdisk_id));
        }

        trace_vxhs_parse_uri_hostinfo(i + 1, uri->server, uri->port);
        g_free(hoststr);
        g_free(portstr);
        g_free(vdisk_id);
        uri_free(uri);
    }

    g_strfreev(target_list);
    return ret;
}

static void vxhs_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    if (qdict_haskey(options, "host")
        || qdict_haskey(options, "port")
        || qdict_haskey(options, "path"))
    {
        error_setg(errp, "host/port/path and a file name may not be specified "
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
    } else if (num_servers > 4) {
        error_setg(&local_err, QERR_INVALID_PARAMETER, "server");
        error_append_hint(errp, "Maximum 4 servers allowed.\n");
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

    *cfd = qnio_open_iio_conn(global_qnio_ctx, of_vsa_addr, 0);
    if (*cfd < 0) {
        error_setg(&local_err, "Failed qnio_open_iio_conn");
        ret = -EIO;
        goto out;
    }
    *rfd = qnio_iio_devopen(global_qnio_ctx, *cfd, file_name, 0);
    if (*rfd < 0) {
        qnio_iio_close(global_qnio_ctx, *cfd);
        *cfd = -1;
        error_setg(&local_err, "Failed qnio_iio_devopen");
        ret = -EIO;
        goto out;
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

int vxhs_open(BlockDriverState *bs, QDict *options,
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
    } else {
        device_opened = 1;
    }

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

errout:
    /*
     * Close remote vDisk device if it was opened before
     */
    if (device_opened) {
        for (i = 0; i < s->vdisk_nhosts; i++) {
            if (s->vdisk_hostinfo[i].vdisk_rfd >= 0) {
                qnio_iio_devclose(s->qnio_ctx, 0,
                                         s->vdisk_hostinfo[i].vdisk_rfd);
                s->vdisk_hostinfo[i].vdisk_rfd = -1;
            }
            /*
             * close QNIO channel against cached channel open-fd
             */
            if (s->vdisk_hostinfo[i].qnio_cfd >= 0) {
                qnio_iio_close(s->qnio_ctx,
                                      s->vdisk_hostinfo[i].qnio_cfd);
                s->vdisk_hostinfo[i].qnio_cfd = -1;
            }
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
BlockAIOCB *vxhs_aio_rw(BlockDriverState *bs,
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
            ret = qnio_iio_writev(s->qnio_ctx,
                    s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                    qiov->iov, qiov->niov, offset, (void *)acb, iio_flags);
            break;
    case VDISK_AIO_READ:
            vxhs_inc_acb_segment_count(acb, 1);
            ret = qnio_iio_readv(s->qnio_ctx,
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
         * redundancy or stateful storage on compute
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

    /*
     * VDISK_AIO_FLUSH ioctl is a no-op at present.
     */
    ret = qnio_iio_ioctl(s->qnio_ctx,
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

    return ret;
}

unsigned long vxhs_get_vdisk_stat(BDRVVXHSState *s)
{
    void *ctx = NULL;
    int flags = 0;
    int64_t vdisk_size = 0;
    int ret = 0;

    ret = qnio_iio_ioctl(s->qnio_ctx,
            s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
            VDISK_STAT, &vdisk_size, ctx, flags);

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
int64_t vxhs_getlength(BlockDriverState *bs)
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
    } else {
        return -EIO;
    }
}

void vxhs_close(BlockDriverState *bs)
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

    if (s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd >= 0) {
        qnio_iio_devclose(s->qnio_ctx, 0,
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

    g_free(s->vdisk_guid);
    s->vdisk_guid = NULL;

    for (i = 0; i < VXHS_MAX_HOSTS; i++) {
        /*
         * Close vDisk device
         */
        if (s->vdisk_hostinfo[i].vdisk_rfd >= 0) {
            qnio_iio_devclose(s->qnio_ctx, 0,
                                     s->vdisk_hostinfo[i].vdisk_rfd);
            s->vdisk_hostinfo[i].vdisk_rfd = -1;
        }

        /*
         * Close Iridium channel against cached channel-fd
         */
        if (s->vdisk_hostinfo[i].qnio_cfd >= 0) {
            qnio_iio_close(s->qnio_ctx,
                                  s->vdisk_hostinfo[i].qnio_cfd);
            s->vdisk_hostinfo[i].qnio_cfd = -1;
        }

        /*
         * Free hostip string which is allocated dynamically
         */
        g_free(s->vdisk_hostinfo[i].hostip);
        s->vdisk_hostinfo[i].hostip = NULL;
        s->vdisk_hostinfo[i].port = 0;
    }
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

int vxhs_switch_storage_agent(BDRVVXHSState *s)
{
    int res = 0;
    int flags = (IIO_FLAG_ASYNC | IIO_FLAG_DONE);

    trace_vxhs_switch_storage_agent(
              s->vdisk_hostinfo[s->vdisk_ask_failover_idx].hostip,
              s->vdisk_guid);

    res = vxhs_reopen_vdisk(s, s->vdisk_ask_failover_idx);
    if (res == 0) {
        res = qnio_iio_ioctl(s->qnio_ctx,
                  s->vdisk_hostinfo[s->vdisk_ask_failover_idx].vdisk_rfd,
                  VDISK_CHECK_IO_FAILOVER_READY, NULL, s, flags);
    } else {
        trace_vxhs_switch_storage_agent_failed(
                  s->vdisk_hostinfo[s->vdisk_ask_failover_idx].hostip,
                  s->vdisk_guid, res, errno);
        /*
         * TODO: calling vxhs_failover_ioctl_cb from here ties up the qnio epoll
         * loop if qnio_iio_ioctl fails synchronously (-1) for all hosts in io
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
     * Close stale vdisk device remote fd since
     * it could be invalid fd after channel disconnect.
     * Reopen the vdisk to get the new fd.
     */
    if (s->vdisk_hostinfo[index].vdisk_rfd >= 0) {
        qnio_iio_devclose(s->qnio_ctx, 0,
                                 s->vdisk_hostinfo[index].vdisk_rfd);
        s->vdisk_hostinfo[index].vdisk_rfd = -1;
    }

    /*
     * As part of vDisk reopen, close the QNIO channel
     * against cached channel-fd (fd is being cached into
     * vDisk hostinfo).
     */
    if (s->vdisk_hostinfo[index].qnio_cfd >= 0) {
        qnio_iio_close(s->qnio_ctx,
                              s->vdisk_hostinfo[index].qnio_cfd);
        s->vdisk_hostinfo[index].qnio_cfd = -1;
    }

    /*
     * Build storage agent address and vdisk device name strings
     */
    file_name = g_strdup_printf("%s%s", vdisk_prefix, s->vdisk_guid);
    of_vsa_addr = g_strdup_printf("of://%s:%d",
             s->vdisk_hostinfo[index].hostip, s->vdisk_hostinfo[index].port);
    /*
     * Open qnio channel to storage agent if not opened before.
     */
    if (s->vdisk_hostinfo[index].qnio_cfd < 0) {
        s->vdisk_hostinfo[index].qnio_cfd =
                qnio_open_iio_conn(global_qnio_ctx, of_vsa_addr, 0);
        if (s->vdisk_hostinfo[index].qnio_cfd < 0) {
            trace_vxhs_reopen_vdisk(s->vdisk_hostinfo[index].hostip);
            res = ENODEV;
            goto out;
        }
    }

    /*
     * Open vdisk device
     */
    s->vdisk_hostinfo[index].vdisk_rfd =
            qnio_iio_devopen(global_qnio_ctx,
             s->vdisk_hostinfo[index].qnio_cfd, file_name, 0);

    if (s->vdisk_hostinfo[index].vdisk_rfd < 0) {
        /*
         * Close QNIO channel against cached channel-fd
         */
        if (s->vdisk_hostinfo[index].qnio_cfd >= 0) {
            qnio_iio_close(s->qnio_ctx,
                                  s->vdisk_hostinfo[index].qnio_cfd);
            s->vdisk_hostinfo[index].qnio_cfd = -1;
        }

        trace_vxhs_reopen_vdisk_openfail(file_name);
        res = EIO;
        goto out;
    }

out:
    g_free(of_vsa_addr);
    g_free(file_name);
    return res;
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
        res = qnio_iio_writev(s->qnio_ctx,
                s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                acb->qiov->iov, acb->qiov->niov,
                acb->io_offset, (void *)acb, iio_flags);
    }

    if (acb->direction == VDISK_AIO_READ) {
        vxhs_inc_vdisk_iocount(s, 1);
        vxhs_inc_acb_segment_count(acb, 1);
        iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);
        res = qnio_iio_readv(s->qnio_ctx,
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

void bdrv_vxhs_init(void)
{
    trace_vxhs_bdrv_init('.');
    bdrv_register(&bdrv_vxhs);
}

block_init(bdrv_vxhs_init);
