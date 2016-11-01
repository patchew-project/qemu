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

#define VDISK_FD_READ               0
#define VDISK_FD_WRITE              1

#define VXHS_OPT_FILENAME           "filename"
#define VXHS_OPT_VDISK_ID           "vdisk-id"
#define VXHS_OPT_SERVER             "server"
#define VXHS_OPT_HOST               "host"
#define VXHS_OPT_PORT               "port"

typedef struct QNIOLibState {
    int refcnt;
    void *context;
} QNIOLibState;

typedef enum {
    VDISK_AIO_READ,
    VDISK_AIO_WRITE,
    VDISK_STAT
} VDISKAIOCmd;

/*
 * HyperScale AIO callbacks structure
 */
typedef struct VXHSAIOCB {
    BlockAIOCB common;
    int err;
    int direction; /* IO direction (r/w) */
    size_t io_offset;
    size_t size;
    QEMUIOVector *qiov;
} VXHSAIOCB;

typedef struct VXHSvDiskHostsInfo {
    int qnio_cfd; /* Channel FD */
    int vdisk_rfd; /* vDisk remote FD */
    char *hostip; /* Host's IP addresses */
    int port; /* Host's port number */
} VXHSvDiskHostsInfo;

/*
 * Structure per vDisk maintained for state
 */
typedef struct BDRVVXHSState {
    int fds[2];
    int event_reader_pos;
    VXHSAIOCB *qnio_event_acb;
    VXHSvDiskHostsInfo vdisk_hostinfo; /* Per host info */
    char *vdisk_guid;
} BDRVVXHSState;

/* QNIO Library State */
static QNIOLibState qniolib;

/* vdisk prefix to pass to qnio */
static const char vdisk_prefix[] = "/dev/of/vdisk";

static void vxhs_iio_callback(int32_t rfd, uint32_t reason, void *ctx,
                              uint32_t error, uint32_t opcode)
{
    VXHSAIOCB *acb = NULL;
    BDRVVXHSState *s = NULL;
    ssize_t ret;

    switch (opcode) {
    case IRP_READ_REQUEST:
    case IRP_WRITE_REQUEST:

        /*
         * ctx is VXHSAIOCB*
         * ctx is NULL if error is QNIOERROR_CHANNEL_HUP or
         * reason is IIO_REASON_HUP
         */
        if (ctx) {
            acb = ctx;
            s = acb->common.bs->opaque;
        } else {
            trace_vxhs_iio_callback(error, reason);
            goto out;
        }

        if (error) {
            if (!acb->err) {
                acb->err = error;
            }
            trace_vxhs_iio_callback(error, reason);
        }

        ret = qemu_write_full(s->fds[VDISK_FD_WRITE], &acb, sizeof(acb));
        g_assert(ret == sizeof(acb));
        break;

    default:
        if (error == QNIOERROR_CHANNEL_HUP) {
            /*
             * Channel failed, spontaneous notification,
             * not in response to I/O
             */
            trace_vxhs_iio_callback_chnfail(error, errno);
        } else {
            trace_vxhs_iio_callback_unknwn(opcode, error);
        }
        break;
    }
out:
    return;
}

/*
 * Initialize QNIO library on first open.
 */
static int vxhs_qnio_open(void)
{
    int ret = 0;

    if (qniolib.refcnt != 0) {
        g_assert(qniolib.context != NULL);
        qniolib.refcnt++;
        return 0;
    }
    qniolib.context = iio_init(QNIO_VERSION, vxhs_iio_callback);
    if (qniolib.context == NULL) {
        ret = -ENODEV;
    } else {
        qniolib.refcnt = 1;
    }
    return ret;
}

/*
 * Cleanup QNIO library on last close.
 */
static void vxhs_qnio_close(void)
{
    qniolib.refcnt--;
    if (qniolib.refcnt == 0) {
        iio_fini(qniolib.context);
        qniolib.context = NULL;
    }
}

static int vxhs_qnio_iio_open(int *cfd, const char *of_vsa_addr,
                              int *rfd, const char *file_name)
{
    int ret = 0;
    bool qnio_open = false;

    ret = vxhs_qnio_open();
    if (ret) {
        return ret;
    }
    qnio_open = true;

    /*
     * Open qnio channel to storage agent if not opened before.
     */
    *cfd = iio_open(qniolib.context, of_vsa_addr, 0);
    if (*cfd < 0) {
        trace_vxhs_qnio_iio_open(of_vsa_addr);
        ret = -ENODEV;
        goto err_out;
    }

    /*
     * Open vdisk device
     */
    *rfd = iio_devopen(qniolib.context, *cfd, file_name, 0);
    if (*rfd < 0) {
        trace_vxhs_qnio_iio_devopen(file_name);
        ret = -ENODEV;
        goto err_out;
    }
    return 0;

err_out:
    if (*cfd >= 0) {
        iio_close(qniolib.context, *cfd);
    }
    if (qnio_open) {
        vxhs_qnio_close();
    }
    *cfd = -1;
    *rfd = -1;
    return ret;
}

static void vxhs_qnio_iio_close(BDRVVXHSState *s)
{
    /*
     * Close vDisk device
     */
    if (s->vdisk_hostinfo.vdisk_rfd >= 0) {
        iio_devclose(qniolib.context, 0, s->vdisk_hostinfo.vdisk_rfd);
        s->vdisk_hostinfo.vdisk_rfd = -1;
    }

    /*
     * Close QNIO channel against cached channel-fd
     */
    if (s->vdisk_hostinfo.qnio_cfd >= 0) {
        iio_close(qniolib.context, s->vdisk_hostinfo.qnio_cfd);
        s->vdisk_hostinfo.qnio_cfd = -1;
    }

    vxhs_qnio_close();
}

static void vxhs_complete_aio(VXHSAIOCB *acb, BDRVVXHSState *s)
{
    BlockCompletionFunc *cb = acb->common.cb;
    void *opaque = acb->common.opaque;
    int ret = 0;

    if (acb->err != 0) {
        trace_vxhs_complete_aio(acb, acb->err);
        /*
         * We mask all the IO errors generically as EIO for upper layers
         * Right now our IO Manager uses non standard error codes. Instead
         * of confusing upper layers with incorrect interpretation we are
         * doing this workaround.
         */
        ret = (-EIO);
    }

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
    char *p;
    ssize_t ret;

    do {
        p = (char *)&s->qnio_event_acb;
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

    hoststr = g_strdup(VXHS_OPT_SERVER".host");
    qdict_put(options, hoststr, qstring_from_str(uri->server));
    g_free(hoststr);

    portstr = g_strdup(VXHS_OPT_SERVER".port");
    if (uri->port) {
        port = g_strdup_printf("%d", uri->port);
        qdict_put(options, portstr, qstring_from_str(port));
        g_free(port);
    }
    g_free(portstr);

    if (strstr(uri->path, "vxhs") == NULL) {
        qdict_put(options, "vdisk-id", qstring_from_str(uri->path));
    }

    trace_vxhs_parse_uri_hostinfo(1, uri->server, uri->port);
    uri_free(uri);

    return ret;
}

static void vxhs_parse_filename(const char *filename, QDict *options,
                                Error **errp)
{
    if (qdict_haskey(options, "vdisk-id") || qdict_haskey(options, "server")) {
        error_setg(errp, "vdisk-id/server and a file name may not be specified "
                         "at the same time");
        return;
    }

    if (strstr(filename, "://")) {
        int ret = vxhs_parse_uri(filename, options);
        if (ret < 0) {
            error_setg(errp, "Invalid URI. URI should be of the form "
                       "  vxhs://<host_ip>:<port>/{<vdisk-id>}");
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
    const char *server_host_opt;
    char *file_name = NULL;
    char *str = NULL;
    int ret = 0;

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

    str = g_strdup_printf(VXHS_OPT_SERVER".");
    qdict_extract_subqdict(options, &backing_options, str);

    /* Create opts info from runtime_tcp_opts list */
    tcp_opts = qemu_opts_create(&runtime_tcp_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(tcp_opts, backing_options, &local_err);
    if (local_err) {
        qdict_del(backing_options, str);
        qemu_opts_del(tcp_opts);
        ret = -EINVAL;
        goto out;
    }

    server_host_opt = qemu_opt_get(tcp_opts, VXHS_OPT_HOST);
    if (!server_host_opt) {
        error_setg(&local_err, QERR_MISSING_PARAMETER,
                   VXHS_OPT_SERVER"."VXHS_OPT_HOST);
        ret = -EINVAL;
        goto out;
    }

    s->vdisk_hostinfo.hostip = g_strdup(server_host_opt);

    s->vdisk_hostinfo.port = g_ascii_strtoll(qemu_opt_get(tcp_opts,
                                                          VXHS_OPT_PORT),
                                                          NULL, 0);

    s->vdisk_hostinfo.qnio_cfd = -1;
    s->vdisk_hostinfo.vdisk_rfd = -1;
    trace_vxhs_qemu_init(s->vdisk_hostinfo.hostip,
                         s->vdisk_hostinfo.port);

    qdict_del(backing_options, str);
    qemu_opts_del(tcp_opts);

    file_name = g_strdup_printf("%s%s", vdisk_prefix, s->vdisk_guid);
    of_vsa_addr = g_strdup_printf("of://%s:%d",
                                s->vdisk_hostinfo.hostip,
                                s->vdisk_hostinfo.port);

    ret = vxhs_qnio_iio_open(cfd, of_vsa_addr, rfd, file_name);
    if (ret) {
        error_setg(&local_err, "Failed qnio_iio_open");
        ret = -EIO;
    }

out:
    g_free(str);
    g_free(file_name);
    g_free(of_vsa_addr);
    qemu_opts_del(opts);

    if (ret < 0) {
        error_propagate(errp, local_err);
        g_free(s->vdisk_hostinfo.hostip);
        g_free(s->vdisk_guid);
        s->vdisk_guid = NULL;
        errno = -ret;
    }

    return ret;
}

static int vxhs_open(BlockDriverState *bs, QDict *options,
                     int bdrv_flags, Error **errp)
{
    BDRVVXHSState *s = bs->opaque;
    AioContext *aio_context;
    int qemu_qnio_cfd = -1;
    int qemu_rfd = -1;
    int ret = 0;

    ret = vxhs_qemu_init(options, s, &qemu_qnio_cfd, &qemu_rfd, errp);
    if (ret < 0) {
        trace_vxhs_open_fail(ret);
        return ret;
    }

    s->vdisk_hostinfo.qnio_cfd = qemu_qnio_cfd;
    s->vdisk_hostinfo.vdisk_rfd = qemu_rfd;

    /*
     * Create a pipe for communicating between two threads in different
     * context. Set handler for read event, which gets triggered when
     * IO completion is done by non-QEMU context.
     */
    ret = qemu_pipe(s->fds);
    if (ret < 0) {
        trace_vxhs_open_epipe(ret);
        ret = -errno;
        goto errout;
    }
    fcntl(s->fds[VDISK_FD_READ], F_SETFL, O_NONBLOCK);

    aio_context = bdrv_get_aio_context(bs);
    aio_set_fd_handler(aio_context, s->fds[VDISK_FD_READ],
                       false, vxhs_aio_event_reader, NULL, s);
    return 0;

errout:
    /*
     * Close remote vDisk device if it was opened earlier
     */
    vxhs_qnio_iio_close(s);
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
static BlockAIOCB *vxhs_aio_rw(BlockDriverState *bs, int64_t sector_num,
                               QEMUIOVector *qiov, int nb_sectors,
                               BlockCompletionFunc *cb, void *opaque, int iodir)
{
    VXHSAIOCB *acb = NULL;
    BDRVVXHSState *s = bs->opaque;
    size_t size;
    uint64_t offset;
    int iio_flags = 0;
    int ret = 0;
    uint32_t rfd = s->vdisk_hostinfo.vdisk_rfd;

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
    acb->err = 0;
    acb->qiov = qiov;
    acb->direction = iodir;

    iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);

    switch (iodir) {
    case VDISK_AIO_WRITE:
            ret = iio_writev(qniolib.context, rfd, qiov->iov, qiov->niov,
                             offset, (uint64_t)size, (void *)acb, iio_flags);
            break;
    case VDISK_AIO_READ:
            ret = iio_readv(qniolib.context, rfd, qiov->iov, qiov->niov,
                            offset, (uint64_t)size, (void *)acb, iio_flags);
            break;
    default:
            trace_vxhs_aio_rw_invalid(iodir);
            goto errout;
    }

    if (ret != 0) {
        trace_vxhs_aio_rw_ioerr(s->vdisk_guid, iodir, size, offset,
                                acb, ret, errno);
        goto errout;
    }
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
    return vxhs_aio_rw(bs, sector_num, qiov, nb_sectors, cb,
                       opaque, VDISK_AIO_READ);
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
    vxhs_qnio_iio_close(s);

    /*
     * Free the dynamically allocated hostip string
     */
    g_free(s->vdisk_hostinfo.hostip);
    s->vdisk_hostinfo.hostip = NULL;
    s->vdisk_hostinfo.port = 0;
}

static int64_t vxhs_get_vdisk_stat(BDRVVXHSState *s)
{
    int64_t vdisk_size = -1;
    int ret = 0;
    uint32_t rfd = s->vdisk_hostinfo.vdisk_rfd;

    ret = iio_ioctl(qniolib.context, rfd, IOR_VDISK_STAT, &vdisk_size, NULL, 0);
    if (ret < 0) {
        trace_vxhs_get_vdisk_stat_err(s->vdisk_guid, ret, errno);
        return -EIO;
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
    int64_t vdisk_size;

    vdisk_size = vxhs_get_vdisk_stat(s);
    if (vdisk_size < 0) {
        return -EIO;
    }

    return vdisk_size;
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
    .bdrv_aio_readv               = vxhs_aio_readv,
    .bdrv_aio_writev              = vxhs_aio_writev,
    .bdrv_detach_aio_context      = vxhs_detach_aio_context,
    .bdrv_attach_aio_context      = vxhs_attach_aio_context,
};

static void bdrv_vxhs_init(void)
{
    bdrv_register(&bdrv_vxhs);
}

block_init(bdrv_vxhs_init);
