/*
 * QEMU Block driver for Veritas HyperScale (VxHS)
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Please follow QEMU coding guidelines while modifying this file.
 * The QEMU coding guidelines need to be followed because this driver has
 * to be submitted to QEMU community in near futute and we want to prevent any
 * reduce the amount of work at that time.
 * QEMU coding guidelines can be found at :
 * http://git.qemu-project.org/?p=qemu.git;a=blob_plain;f=CODING_STYLE;hb=HEAD
 */

#include "vxhs.h"

/* global variables (per-vm) */
static QNIOOps          qnioops;
static GModule          *lib_qemuqnio_handle;

/* qnio client ioapi_ctx */
static void             *global_qnio_ctx;

/* insure init once */
static pthread_mutex_t  of_global_ctx_lock;

/* HyperScale Driver Version */
int vxhs_drv_version = 8895;

/*
 * Loading QNIO operation from qemuqnio library at run time.
 * It loads only when first vxhs_open called for a vDisk
 */
int vxhs_load_iio_ops(void)
{
    int i = 0;

    LibQNIOSymbol qnio_symbols[] = {
        {"qemu_iio_init",
                (gpointer *) &qnioops.qemu_iio_init},
        {"qemu_open_iio_conn",
                (gpointer *) &qnioops.qemu_open_iio_conn},
        {"qemu_iio_devopen",
                (gpointer *) &qnioops.qemu_iio_devopen},
        {"qemu_iio_devclose",
                (gpointer *) &qnioops.qemu_iio_devclose},
        {"qemu_iio_writev",
                (gpointer *) &qnioops.qemu_iio_writev},
        {"qemu_iio_readv",
                (gpointer *) &qnioops.qemu_iio_readv},
        {"qemu_iio_read",
                (gpointer *) &qnioops.qemu_iio_read},
        {"qemu_iio_ioctl",
                (gpointer *) &qnioops.qemu_iio_ioctl},
        {"qemu_iio_close",
                (gpointer *) &qnioops.qemu_iio_close},
        {"qemu_iio_extract_msg_error",
                (gpointer *) &qnioops.qemu_iio_extract_msg_error},
        {"qemu_iio_extract_msg_size",
                (gpointer *) &qnioops.qemu_iio_extract_msg_size},
        {"qemu_iio_extract_msg_opcode",
                (gpointer *) &qnioops.qemu_iio_extract_msg_opcode},
        {"qemu_initialize_lock",
                (gpointer *) &qnioops.qemu_initialize_lock},
        {"qemu_spin_lock",
                (gpointer *) &qnioops.qemu_spin_lock},
        {"qemu_spin_unlock",
                (gpointer *) &qnioops.qemu_spin_unlock},
        {"qemu_destroy_lock",
                (gpointer *) &qnioops.qemu_destroy_lock},
        {NULL}
    };

    if (!g_module_supported()) {
        error_report("modules are not supported on this platform: %s",
                     g_module_error());
        return -EIO;
    }

    lib_qemuqnio_handle = g_module_open("libqnioshim.so", 0);
    if (!lib_qemuqnio_handle) {
        error_report("error loading libqnioshim.so: %s", g_module_error());
        return -EIO;
    }

    g_module_make_resident(lib_qemuqnio_handle);
    while (qnio_symbols[i].name) {
        const char *name = qnio_symbols[i].name;
        if (!g_module_symbol(lib_qemuqnio_handle, name, qnio_symbols[i].addr)) {
            error_report("%s could not be loaded from qnioops : %s",
                         name, g_module_error());
            return -EIO;
        }
        ++i;
    }
    vxhsDbg("qnio ops loaded\n");

    return 0;
}

inline void vxhs_inc_acb_segment_count(void *ptr, int count)
{
    VXHSAIOCB *acb = (VXHSAIOCB *)ptr;
    BDRVVXHSState *s = acb->common.bs->opaque;

    VXHS_SPIN_LOCK(s->vdisk_acb_lock);
    acb->segments += count;
    VXHS_SPIN_UNLOCK(s->vdisk_acb_lock);
}

inline void vxhs_dec_acb_segment_count(void *ptr, int count)
{
    VXHSAIOCB *acb = (VXHSAIOCB *)ptr;
    BDRVVXHSState *s = acb->common.bs->opaque;

    VXHS_SPIN_LOCK(s->vdisk_acb_lock);
    acb->segments -= count;
    VXHS_SPIN_UNLOCK(s->vdisk_acb_lock);
}

inline int vxhs_dec_and_get_acb_segment_count(void *ptr, int count)
{
    VXHSAIOCB *acb = (VXHSAIOCB *)ptr;
    BDRVVXHSState *s = acb->common.bs->opaque;
    int segcount = 0;


    VXHS_SPIN_LOCK(s->vdisk_acb_lock);
    acb->segments -= count;
    segcount = acb->segments;
    VXHS_SPIN_UNLOCK(s->vdisk_acb_lock);

    return segcount;
}

inline void vxhs_set_acb_buffer(void *ptr, void *buffer)
{
    VXHSAIOCB *acb = (VXHSAIOCB *)ptr;

    acb->buffer = buffer;
}

inline void vxhs_inc_vdisk_iocount(void *ptr, uint32_t count)
{
    BDRVVXHSState *s = (BDRVVXHSState *)ptr;

    VXHS_SPIN_LOCK(s->vdisk_lock);
    s->vdisk_aio_count += count;
    VXHS_SPIN_UNLOCK(s->vdisk_lock);
}

inline void vxhs_dec_vdisk_iocount(void *ptr, uint32_t count)
{
    BDRVVXHSState *s = (BDRVVXHSState *)ptr;

    VXHS_SPIN_LOCK(s->vdisk_lock);
    s->vdisk_aio_count -= count;
    VXHS_SPIN_UNLOCK(s->vdisk_lock);
}

inline uint32_t vxhs_get_vdisk_iocount(void *ptr)
{
    BDRVVXHSState *s = (BDRVVXHSState *)ptr;
    uint32_t count = 0;

    VXHS_SPIN_LOCK(s->vdisk_lock);
    count = s->vdisk_aio_count;
    VXHS_SPIN_UNLOCK(s->vdisk_lock);

    return count;
}


/*
 * All the helper functions required for HyperScale QEMU block driver
 * If these functions become unmanageable in single file then split it.
 * Avoiding file proliferage till then.
 */

char *vxhs_string_iterate(char *p, const char *d, const size_t len)
{
    while (p != NULL && *p && memcmp(p, d, len) == 0) {
        p += len;
    }
    return p;
}

int
vxhs_tokenize(char **result, char *working, const char *src, const char *delim)
{
    int i = 0;
    char *p = NULL;
    size_t len = strlen(delim);

    strcpy(working, src);
    p = working;

    for (result[i] = NULL, p = vxhs_string_iterate(p, delim, len); p != NULL
        && *p; p = vxhs_string_iterate(p, delim, len)) {
        result[i++] = p;
        result[i] = NULL;
        p = strstr(p, delim);
    }
    return i;
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
        error = (*qnioops.qemu_iio_extract_msg_error)(m);
        opcode = (*qnioops.qemu_iio_extract_msg_opcode)(m);
    }
    switch (opcode) {
    case IRP_READ_REQUEST:
    case IRP_WRITE_REQUEST:

    /*
     * ctx is VXHSAIOCB*
     * ctx is NULL if error is VXERROR_CHANNEL_HUP or reason is IIO_REASON_HUP
     */
    if (ctx) {
        acb = (VXHSAIOCB *)ctx;
        s = acb->common.bs->opaque;
    } else {
        vxhsDbg("ctx is NULL: error %d, reason %d\n",
                  error, reason);
        goto out;
    }

    if (error) {
        vxhsDbg("Read/Write failed: error %d, reason %d, acb %p, segment %d\n",
                  error, reason, acb, acb->segments);

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
                    vxhsDbg("vDisk %s, added acb %p to retry queue(5)\n",
                              s->vdisk_guid, acb);
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
            vxhsDbg(" QNIO channel failed, no i/o (%d)\n", error);
            /*
             * TODO: Start channel failover when no I/O is outstanding
             */
            goto out;
        } else {
            vxhsDbg(" ALERT: reason = %d , acb = %p, "
                      "acb->segments = %d, acb->size = %lu Error = %d\n",
                      reason, acb, acb->segments, acb->size, error);
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
    /*
     * vxhsDbg(" acb %p segcount %d\n", acb, segcount);
     */
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
        vxhsDbg("async vxhs_iio_callback: IRP_VDISK_CHECK_IO_FAILOVER_READY "
                  "completed for vdisk %s with error %d\n",
                  ((BDRVVXHSState *)ctx)->vdisk_guid, error);
        failover_ioctl_cb(error, ctx);
        break;

    default:
        if (reason == IIO_REASON_HUP) {
            /*
             * Channel failed, spontaneous notification,
             * not in response to I/O
             */
            vxhsDbg(" QNIO channel failed, no i/o (%d, %d)\n",
                      error, errno);
            /*
             * TODO: Start channel failover when no I/O is outstanding
             */
        } else {
            vxhsDbg("unexpected opcode %d, errno %d\n",
                      opcode, error);
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
        vxhsDbg("aio failed acb %p ret %ld\n", acb, acb->ret);
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
 * It invoked when any IO completed and written on pipe
 * by callback called from QNIO thread context. Then it mark
 * the AIO as completed and release HyperScale AIO callbacks.
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
 * QEMU calls this to check if any pending IO on vDisk
 * It will wait in loop until all the AIO completed.
 */
int vxhs_aio_flush_cb(void *opaque)
{
    BDRVVXHSState *s = opaque;

    return vxhs_get_vdisk_iocount(s);
}

/*
 * This will be called by QEMU while booting for each vDisks.
 * bs->opaque will be allocated by QEMU upper block layer before
 * calling open. It will load all the QNIO operations from
 * qemuqnio library and call QNIO operation to create channel to
 * doing IO on vDisk. It parse the URI, get the hostname, vDisk
 * path and then set HyperScale event handler to QEMU.
 */
void *vxhs_initialize(void)
{
    void *qnio_ctx = NULL;

    if (vxhs_load_iio_ops() < 0) {
        vxhsDbg("Could not load the QNIO IO library.\n");
        return qnio_ctx;
    }

    qnio_ctx = (*qnioops.qemu_iio_init)(vxhs_iio_callback);

    return qnio_ctx;
}

void *vxhs_setup_qnio(void)
{
    void *qnio_ctx = NULL;

    qnio_ctx = vxhs_initialize();

    if (qnio_ctx != NULL) {
        vxhsDbg("Context to HyperScale IO manager = %p\n", qnio_ctx);
    } else {
        vxhsDbg("Could not initialize the network channel. Bailing out\n");
    }

    return qnio_ctx;
}

int vxhs_open_device(const char *vxhs_uri, int *cfd, int *rfd,
                       BDRVVXHSState *s)
{
    char *file_name;
    char *of_vsa_addr;
    char **split_filename = (char **)malloc(1024);
    char *scratch = (char *)malloc(1024);
    int resilency_count = 0;
    int ret = 0;

    pthread_mutex_lock(&of_global_ctx_lock);
    if (global_qnio_ctx == NULL) {
        global_qnio_ctx = vxhs_setup_qnio();
        if (global_qnio_ctx == NULL) {
            vxhsDbg("Error while opening the device. QNIO IO library "
                      "couldn't be initialized.\n");
            pthread_mutex_unlock(&of_global_ctx_lock);
            free(split_filename);
            free(scratch);
            return -1;
        }
    }
    pthread_mutex_unlock(&of_global_ctx_lock);

    *cfd = -1;

    of_vsa_addr = (char *) malloc(sizeof(char) * OF_MAX_SERVER_ADDR);
    if (!of_vsa_addr) {
        vxhsDbg("Could not allocate memory for file parsing. Bailing out\n");
        free(split_filename);
        free(scratch);
        return -ENOMEM;
    }
    memset(of_vsa_addr, 0, OF_MAX_SERVER_ADDR);
    file_name = (char *) malloc(sizeof(char) * OF_MAX_FILE_LEN);
    if (!file_name) {
        vxhsDbg("Could not allocate memory for file parsing. Bailing out\n");
        free(of_vsa_addr);
        free(split_filename);
        free(scratch);
        return -ENOMEM;
    }
    memset(file_name, 0, OF_MAX_FILE_LEN);

    /*
     * Below steps need to done by all the block driver in QEMU which
     * support AIO. Need to create pipe for communicating b/w two threads
     * in different context. And set handler for read event when IO completion
     * done by non-QEMU context.
     */
    vxhsDbg("Original command line : %s\n", vxhs_uri);
    resilency_count = vxhs_tokenize(split_filename, scratch, vxhs_uri, "%7D");
    vxhs_build_io_target_list(s, resilency_count, split_filename);

    snprintf(file_name, OF_MAX_FILE_LEN, "%s%s", vdisk_prefix, s->vdisk_guid);
    snprintf(of_vsa_addr, OF_MAX_SERVER_ADDR, "of://%s:%d",
             s->vdisk_hostinfo[s->vdisk_cur_host_idx].hostip,
             s->vdisk_hostinfo[s->vdisk_cur_host_idx].port);

    vxhsDbg("Driver state = %p\n", s);
    vxhsDbg("Connecting to : %s\n", of_vsa_addr);

    *cfd = (*qnioops.qemu_open_iio_conn)(global_qnio_ctx, of_vsa_addr, 0);
    if (*cfd < 0) {
        vxhsDbg("Could not open an QNIO connection to: %s\n", of_vsa_addr);
        ret = -EIO;
        goto out;
    }
    *rfd = (*qnioops.qemu_iio_devopen)(global_qnio_ctx, *cfd, file_name, 0);
    s->aio_context = qemu_get_aio_context();

out:
    /* uri is still in use, cleaned up in close */
    if (file_name != NULL) {
        free(file_name);
    }
    if (of_vsa_addr != NULL) {
        free(of_vsa_addr);
    }
    free(split_filename);
    free(scratch);
    return ret;
}

int vxhs_create(const char *filename,
        QemuOpts *options, Error **errp)
{
        int ret = 0;
        int qemu_cfd = 0;
        int qemu_rfd = 0;
        BDRVVXHSState s;

        vxhsDbg("vxhs_create: came in to open file = %s\n", filename);
        ret = vxhs_open_device(filename, &qemu_cfd, &qemu_rfd, &s);
        vxhsDbg("vxhs_create: s->qnio_cfd = %d , s->rfd = %d\n", qemu_cfd,
                 qemu_rfd);

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
    vxhsDbg("came in to open file = %s\n", vxhs_uri);
    ret = vxhs_open_device(vxhs_uri, &qemu_qnio_cfd, &qemu_rfd, s);
    if (ret != 0) {
        vxhsDbg("Could not open the device. Error = %d\n", ret);
        return ret;
    }
    s->qnio_ctx = global_qnio_ctx;
    s->vdisk_hostinfo[0].qnio_cfd = qemu_qnio_cfd;
    s->vdisk_hostinfo[0].vdisk_rfd = qemu_rfd;
    s->vdisk_size = 0;
    QSIMPLEQ_INIT(&s->vdisk_aio_retryq);

    vxhsDbg("s->qnio_cfd = %d, s->rfd = %d\n",
              s->vdisk_hostinfo[0].qnio_cfd,
              s->vdisk_hostinfo[0].vdisk_rfd);
    ret = qemu_pipe(s->fds);
    if (ret < 0) {
        vxhsDbg("Could not create a pipe for device. bailing out\n");
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
     *      struct. The reason is very simple,
     *      we don't want an overhead of spin
     *      lock dynamic allocation and free
     *      for every AIO.
     */
    s->vdisk_lock = VXHS_SPIN_LOCK_ALLOC;
    s->vdisk_acb_lock = VXHS_SPIN_LOCK_ALLOC;

    return 0;

out:
    if (s->vdisk_hostinfo[0].vdisk_rfd >= 0) {
        qnioops.qemu_iio_devclose(s->qnio_ctx,
                                 s->vdisk_hostinfo[0].vdisk_rfd);
    }
    /* never close qnio_cfd */
    vxhsDbg("vxhs_open failed (%d)\n", ret);
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
    VXHSAIOCB *acb = (VXHSAIOCB *) arg;
    BlockDriverState *bs = acb->common.bs;
    BDRVVXHSState *s = bs->opaque;
    int rv;

    vxhsDbg("finish callback in non-QEMU context... writing on pipe\n");
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
 * and passed to QNIO. When QNIO completed the work,
 * it will be passed back through the callback
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
        vxhsDbg("vDisk %s, vDisk device is in failed state "
                  "iodir = %d size = %lu offset = %lu\n",
                  s->vdisk_guid, iodir, size, offset);
        VXHS_SPIN_UNLOCK(s->vdisk_lock);
        goto errout;
    }
    if (OF_VDISK_IOFAILOVER_IN_PROGRESS(s)) {
        QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq, acb, retry_entry);
        s->vdisk_aio_retry_qd++;
        OF_AIOCB_FLAGS_SET_QUEUED(acb);
        VXHS_SPIN_UNLOCK(s->vdisk_lock);
        vxhsDbg("vDisk %s, added acb %p to retry queue(1)\n",
                  s->vdisk_guid, acb);
        goto out;
    }
    s->vdisk_aio_count++;
    VXHS_SPIN_UNLOCK(s->vdisk_lock);

    iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);

    switch (iodir) {
    case VDISK_AIO_WRITE:
            vxhs_inc_acb_segment_count(acb, 1);
            /*
             * vxhsDbg("WRITING: opaque = %p size = %lu offset = %lu  "
             *           "Segments = %d\n", opaque, size,
             *           sector_num * BDRV_SECTOR_SIZE, acb->segments);
             */
            ret = (*qnioops.qemu_iio_writev)(s->qnio_ctx,
                    s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                    qiov->iov, qiov->niov, offset, (void *)acb, iio_flags);
            /*
             * vxhsDbg("qemu_iio_writev acb %p ret %d\n", acb, ret);
             */
            break;
    case VDISK_AIO_READ:
            vxhs_inc_acb_segment_count(acb, 1);
            /*
             * vxhsDbg("READING : buf = %p size = %lu offset = %lu "
             *           "Segments = %d\n", buf, size,
             *           sector_num * BDRV_SECTOR_SIZE, acb->segments);
             */
            ret = (*qnioops.qemu_iio_readv)(s->qnio_ctx,
                    s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                    qiov->iov, qiov->niov, offset, (void *)acb, iio_flags);
            /*
             * vxhsDbg("qemu_iio_readv returned %d\n", ret);
             */
            break;
    default:
            vxhsDbg("Invalid I/O request iodir %d\n", iodir);
            goto errout;
    }

    if (ret != 0) {
        vxhsDbg("IO ERROR (vDisk %s) FOR : Read/Write = %d size = %lu "
                  "offset = %lu ACB = %p Segments = %d. Error = %d, errno = %d\n",
                  s->vdisk_guid, iodir, size, offset,
                  acb, acb->segments, ret, errno);
        /*
         * Don't retry I/Os against vDisk having no
         * redundency or statefull storage on compute
         *
         * TODO: Revisit this code path to see if any
         *       particular error need to be handled.
         *       At this moment failing the I/O.
         */
        VXHS_SPIN_LOCK(s->vdisk_lock);
        if (s->vdisk_nhosts == 1) {
            vxhsDbg("vDisk %s, I/O operation failed.\n",
                      s->vdisk_guid);
            s->vdisk_aio_count--;
            vxhs_dec_acb_segment_count(acb, 1);
            VXHS_SPIN_UNLOCK(s->vdisk_lock);
            goto errout;
        }
        if (OF_VDISK_FAILED(s)) {
            vxhsDbg("vDisk %s, vDisk device failed "
                      "iodir = %d size = %lu offset = %lu\n",
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
            vxhsDbg("vDisk %s, added acb %p to retry queue(2)\n",
                      s->vdisk_guid, acb);
            goto out;
        }
        OF_VDISK_SET_IOFAILOVER_IN_PROGRESS(s);
        QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq, acb, retry_entry);
        s->vdisk_aio_retry_qd++;
        OF_AIOCB_FLAGS_SET_QUEUED(acb);
        vxhs_dec_acb_segment_count(acb, 1);
        vxhsDbg("vDisk %s, added acb %p to retry queue(3)\n",
                  s->vdisk_guid, acb);
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

/*
 * This is called from qemu-img utility when user want to resize
 * the disk. Currently it's noop within VXHS block driver.
 */
int vxhs_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVVXHSState *s = bs->opaque;
    int ret = 0;

    vxhsErr("Truncating disk (%s): BlockDriverState = %p, "
              "BDRVVXHSState = %p offset = %lu\n",
              s->vdisk_guid, bs, s, offset);

    return ret;
}

BlockAIOCB *vxhs_aio_readv(BlockDriverState *bs,
                                   int64_t sector_num, QEMUIOVector *qiov,
                                   int nb_sectors,
                                   BlockCompletionFunc *cb, void *opaque)
{
    /*
     * vxhsDbg("READING: opaque = %p size = %d offset = %lu\n",
     *           opaque, nb_sectors * BDRV_SECTOR_SIZE, sector_num);
     */
    return vxhs_aio_rw(bs, sector_num, qiov, nb_sectors,
                         cb, opaque, VDISK_AIO_READ);
}

BlockAIOCB *vxhs_aio_writev(BlockDriverState *bs,
                                    int64_t sector_num, QEMUIOVector *qiov,
                                    int nb_sectors,
                                    BlockCompletionFunc *cb, void *opaque)
{
    /*
     * vxhsDbg("WRITING: opaque = %p size = %d offset = %lu\n",
     *           opaque, nb_sectors * BDRV_SECTOR_SIZE, sector_num);
     */
    return vxhs_aio_rw(bs, sector_num, qiov, nb_sectors,
                         cb, opaque, VDISK_AIO_WRITE);
}

/*
 * This is called by QEMU when flush inside guest triggered
 * at block layer either for IDE or SCSI disks.
 */
int vxhs_co_flush(BlockDriverState *bs)
{
    BDRVVXHSState *s = bs->opaque;
    uint64_t size = 0;
    int ret = 0;
    uint32_t iocount = 0;

    ret = (*qnioops.qemu_iio_ioctl)(s->qnio_ctx,
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
        vxhsDbg("vDisk (%s) Flush ioctl failed ret = %d errno = %d\n",
                  s->vdisk_guid, ret, errno);
        ret = 0;
    }

    iocount = vxhs_get_vdisk_iocount(s);
    if (iocount > 0) {
        vxhsDbg("In the flush the IO count = %d\n", iocount);
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

    ret = (*qnioops.qemu_iio_ioctl)(s->qnio_ctx,
            s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
            VDISK_STAT, &vdisk_size, ctx, flags);

    if (ret < 0) {
        vxhsDbg("vDisk (%s) stat ioctl failed, ret = %d, errno = %d\n",
                  s->vdisk_guid, ret, errno);
    }
    vxhsDbg("vDisk size = %lu\n", vdisk_size);

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
            vxhsDbg("Allocated blocks = %lu\n", vdisk_size);
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

    vxhsDbg("closing vDisk\n");

    close(s->fds[VDISK_FD_READ]);
    close(s->fds[VDISK_FD_WRITE]);

    if (s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd >= 0) {
        qnioops.qemu_iio_devclose(s->qnio_ctx,
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
     * never close channel - not ref counted, will
     * close for all vdisks
     */

    vxhsDbg("vxhs_close: done\n");

    /*
     * TODO: Verify that all the resources were relinguished.
     */
}

int vxhs_has_zero_init(BlockDriverState *bs)
{
    vxhsDbg("Returning without doing anything (noop).\n");

    return 0;
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

    vxhsDbg("Query host %s for vdisk %s\n",
              s->vdisk_hostinfo[s->vdisk_ask_failover_idx].hostip,
              s->vdisk_guid);

    res = vxhs_reopen_vdisk(s, s->vdisk_ask_failover_idx);
    if (res == 0) {
        res = (*qnioops.qemu_iio_ioctl)(s->qnio_ctx,
                   s->vdisk_hostinfo[s->vdisk_ask_failover_idx].vdisk_rfd,
                   VDISK_CHECK_IO_FAILOVER_READY, NULL, s, flags);
    }
    if (res != 0) {
        vxhsDbg("Query to host %s for vdisk %s failed, res = %d, errno = %d\n",
                  s->vdisk_hostinfo[s->vdisk_ask_failover_idx].hostip,
                  s->vdisk_guid, res, errno);
        /*
         * TODO: calling failover_ioctl_cb from here ties up the qnio epoll
         * loop if qemu_iio_ioctl fails synchronously (-1) for all hosts in io
         * target list.
         */

        /* try next host */
        failover_ioctl_cb(res, s);
    }
    return res;
}

void failover_ioctl_cb(int res, void *ctx)
{
    BDRVVXHSState *s = ctx;

    if (res == 0) {
        /* found failover target */
        s->vdisk_cur_host_idx = s->vdisk_ask_failover_idx;
        s->vdisk_ask_failover_idx = 0;
        vxhsDbg("Switched to storage server host-IP %s for vdisk %s\n",
                   s->vdisk_hostinfo[s->vdisk_cur_host_idx].hostip,
                   s->vdisk_guid);
        VXHS_SPIN_LOCK(s->vdisk_lock);
        OF_VDISK_RESET_IOFAILOVER_IN_PROGRESS(s);
        VXHS_SPIN_UNLOCK(s->vdisk_lock);
        vxhs_handle_queued_ios(s);
    } else {
        /* keep looking */
        vxhsDbg("failover_ioctl_cb: keep looking for io target for vdisk %s\n",
                  s->vdisk_guid);
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

    vxhsDbg("I/O Failover starting for vDisk %s", s->vdisk_guid);

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
        qnioops.qemu_iio_devclose(s->qnio_ctx,
                                 s->vdisk_hostinfo[index].vdisk_rfd);
        s->vdisk_hostinfo[index].vdisk_rfd = -1;
    }
    /*
     * build storage agent address and vdisk device name strings
     */
    of_vsa_addr = (char *) malloc(sizeof(char) * OF_MAX_SERVER_ADDR);
    if (!of_vsa_addr) {
        res = ENOMEM;
        goto out;
    }
    file_name = (char *) malloc(sizeof(char) * OF_MAX_FILE_LEN);
    if (!file_name) {
        res = ENOMEM;
        goto out;
    }
    snprintf(file_name, OF_MAX_FILE_LEN, "%s%s", vdisk_prefix, s->vdisk_guid);
    snprintf(of_vsa_addr, OF_MAX_SERVER_ADDR, "of://%s:%d",
             s->vdisk_hostinfo[index].hostip, s->vdisk_hostinfo[index].port);
    /*
     * open iridum channel to storage agent if not opened before.
     */
    if (s->vdisk_hostinfo[index].qnio_cfd < 0) {
        vxhsDbg("Trying to connect "
                  "host-IP %s\n", s->vdisk_hostinfo[index].hostip);
        s->vdisk_hostinfo[index].qnio_cfd =
                (*qnioops.qemu_open_iio_conn)(global_qnio_ctx, of_vsa_addr, 0);
        if (s->vdisk_hostinfo[index].qnio_cfd < 0) {
            vxhsDbg("Failed to connect "
                      "to storage agent on host-ip %s\n",
                      s->vdisk_hostinfo[index].hostip);
            res = ENODEV;
            goto out;
        }
    }
    /*
     * open vdisk device
     */
    vxhsDbg("Trying to open vdisk device: %s\n", file_name);
    s->vdisk_hostinfo[index].vdisk_rfd =
            (*qnioops.qemu_iio_devopen)(global_qnio_ctx,
             s->vdisk_hostinfo[index].qnio_cfd, file_name, 0);
    if (s->vdisk_hostinfo[index].vdisk_rfd < 0) {
        vxhsDbg("Failed to open vdisk device: %s\n", file_name);
        res = EIO;
        goto out;
    }
out:
    if (of_vsa_addr) {
        free(of_vsa_addr);
    }
    if (file_name) {
        free(file_name);
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
 *
 */
int vxhs_build_io_target_list(BDRVVXHSState *s, int resilency_count,
                              char **filenames)
{
    URI *uri = NULL;
    int i = 0;

    /*
     * TODO : We need to move to dynamic allocation of number of hosts.
     * s->vdisk_hostinfo = (VXHSvDiskHostsInfo **) malloc (
     *                      sizeof(VXHSvDiskHostsInfo) * resilency_count);
     *  memset(&s->vdisk_hostinfo, 0, sizeof(VXHSvDiskHostsInfo) * MAX_HOSTS);
     */
    for (i = 0; i < resilency_count; i++) {
        uri = uri_parse(filenames[i]);
        s->vdisk_hostinfo[i].hostip = (char *)malloc(strlen(uri->server));
        strncpy((s->vdisk_hostinfo[i].hostip), uri->server, IP_ADDR_LEN);
        s->vdisk_hostinfo[i].port = uri->port;
        s->vdisk_hostinfo[i].qnio_cfd = -1;
        s->vdisk_hostinfo[i].vdisk_rfd = -1;
        if (strstr(uri->path, "vxhs") == NULL) {
            s->vdisk_guid = (char *)malloc(strlen(uri->path));
            strcpy((s->vdisk_guid), uri->path);
        }
        uri_free(uri);
    }
    s->vdisk_nhosts = resilency_count;
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
            vxhsDbg("Restarted acb %p res %d\n", acb, res);
            VXHS_SPIN_LOCK(s->vdisk_lock);
            if (res) {
                QSIMPLEQ_INSERT_TAIL(&s->vdisk_aio_retryq,
                                     acb, retry_entry);
                OF_AIOCB_FLAGS_SET_QUEUED(acb);
                VXHS_SPIN_UNLOCK(s->vdisk_lock);
                vxhsDbg("vDisk %s, added acb %p to retry queue(4)\n",
                          s->vdisk_guid, acb);
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
        res = (*qnioops.qemu_iio_writev)(s->qnio_ctx,
                s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                acb->qiov->iov, acb->qiov->niov,
                acb->io_offset, (void *)acb, iio_flags);
    }

    if (acb->direction == VDISK_AIO_READ) {
        vxhs_inc_vdisk_iocount(s, 1);
        vxhs_inc_acb_segment_count(acb, 1);
        iio_flags = (IIO_FLAG_DONE | IIO_FLAG_ASYNC);
        res = (*qnioops.qemu_iio_readv)(s->qnio_ctx,
                s->vdisk_hostinfo[s->vdisk_cur_host_idx].vdisk_rfd,
                acb->qiov->iov, acb->qiov->niov,
                acb->io_offset, (void *)acb, iio_flags);
    }

    if (res != 0) {
        vxhs_dec_vdisk_iocount(s, 1);
        vxhs_dec_acb_segment_count(acb, 1);
        vxhsDbg("IO ERROR FOR: Read/Write = %d "
                  "Error = %d, errno = %d\n",
                  acb->direction, res, errno);
    }

    return res;
}

void vxhs_fail_aio(VXHSAIOCB *acb, int err)
{
    BDRVVXHSState *s = NULL;
    int segcount = 0;
    int rv = 0;

    s = acb->common.bs->opaque;

    vxhsDbg("vDisk %s, failing acb %p\n", s->vdisk_guid, acb);
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
    .bdrv_truncate                = vxhs_truncate,
    .bdrv_aio_readv               = vxhs_aio_readv,
    .bdrv_aio_writev              = vxhs_aio_writev,
    .bdrv_co_flush_to_disk        = vxhs_co_flush,
    .bdrv_co_pdiscard             = vxhs_co_pdiscard,
    .bdrv_has_zero_init           = vxhs_has_zero_init,
};

void bdrv_vxhs_init(void)
{
    vxhsDbg("Registering VXHS %d AIO driver\n", vxhs_drv_version);

    bdrv_register(&bdrv_vxhs);

    /*
     * Initialize pthread mutex for of_global_ctx_lock
     */
    pthread_mutex_init(&of_global_ctx_lock, NULL);
}


/*
 * The line below is how our drivier is initialized.
 * DO NOT TOUCH IT
 */
block_init(bdrv_vxhs_init);
