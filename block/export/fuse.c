/*
 * Present a block device as a raw image through FUSE
 *
 * Copyright (c) 2020, 2025 Hanna Czenczek <hreitz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 or later of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define FUSE_USE_VERSION 31

#include "qemu/osdep.h"
#include "qemu/memalign.h"
#include "block/aio.h"
#include "block/block_int-common.h"
#include "block/export.h"
#include "block/fuse.h"
#include "block/qapi.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block.h"
#include "qemu/coroutine.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "system/block-backend.h"
#include "system/block-backend.h"
#include "system/iothread.h"

#include <fuse.h>
#include <fuse_lowlevel.h>

#include "standard-headers/linux/fuse.h"
#include <sys/ioctl.h>

#if defined(CONFIG_FALLOCATE_ZERO_RANGE)
#include <linux/falloc.h>
#endif

#ifdef __linux__
#include <linux/fs.h>
#endif

/* Prevent overly long bounce buffer allocations */
#define FUSE_MAX_READ_BYTES (MIN(BDRV_REQUEST_MAX_BYTES, 1 * 1024 * 1024))
/*
 * FUSE_MAX_WRITE_BYTES determines the maximum number of bytes we support in a
 * write request; FUSE_IN_PLACE_WRITE_BYTES and FUSE_SPILLOVER_BUF_SIZE
 * determine the split between the size of the in-place buffer in FuseRequest
 * and the spill-over buffer in FuseQueue.  See FuseQueue.spillover_buf for a
 * detailed explanation.
 */
#define FUSE_IN_PLACE_WRITE_BYTES (4 * 1024)
#define FUSE_MAX_WRITE_BYTES (64 * 1024)
#define FUSE_SPILLOVER_BUF_SIZE \
    (FUSE_MAX_WRITE_BYTES - FUSE_IN_PLACE_WRITE_BYTES)

typedef struct FuseExport FuseExport;

/*
 * One FUSE "queue", representing one FUSE FD from which requests are fetched
 * and processed.  Each queue is tied to an AioContext.
 */
typedef struct FuseQueue {
    FuseExport *exp;

    AioContext *ctx;
    int fuse_fd;

    /*
     * The request buffer must be able to hold a full write, and/or at least
     * FUSE_MIN_READ_BUFFER (from linux/fuse.h) bytes.
     * This however is just the first part of the buffer; every read is given
     * a vector of this buffer (which should be enough for all normal requests,
     * which we check via the static assertion in FUSE_IN_OP_STRUCT()) and the
     * spill-over buffer below.
     * Therefore, the size of this buffer plus FUSE_SPILLOVER_BUF_SIZE must be
     * FUSE_MIN_READ_BUFFER or more (checked via static assertion below).
     */
    char request_buf[sizeof(struct fuse_in_header) +
                     sizeof(struct fuse_write_in) +
                     FUSE_IN_PLACE_WRITE_BYTES];

    /*
     * When retrieving a FUSE request, the destination buffer must always be
     * sufficiently large for the whole request, i.e. with max_write=64k, we
     * must provide a buffer that fits the WRITE header and 64 kB of space for
     * data.
     * We do want to support 64k write requests without requiring them to be
     * split up, but at the same time, do not want to do such a large allocation
     * for every single request.
     * Therefore, the FuseRequest object provides an in-line buffer that is
     * enough for write requests up to 4k (and all other requests), and for
     * every request that is bigger, we provide a spill-over buffer here (for
     * the remaining 64k - 4k = 60k).
     * When poll_fuse_fd() reads a FUSE request, it passes these buffers as an
     * I/O vector, and then checks the return value (number of bytes read) to
     * find out whether the spill-over buffer was used.  If so, it will move the
     * buffer to the request, and will allocate a new spill-over buffer for the
     * next request.
     *
     * Free this buffer with qemu_vfree().
     */
    void *spillover_buf;
} FuseQueue;

/*
 * Verify that FuseQueue.request_buf plus the spill-over buffer together
 * are big enough to be accepted by the FUSE kernel driver.
 */
QEMU_BUILD_BUG_ON(sizeof(((FuseQueue *)0)->request_buf) +
                  FUSE_SPILLOVER_BUF_SIZE <
                  FUSE_MIN_READ_BUFFER);

struct FuseExport {
    BlockExport common;

    struct fuse_session *fuse_session;
    unsigned int in_flight; /* atomic */
    bool mounted, fd_handler_set_up;

    /*
     * Set when there was an unrecoverable error and no requests should be read
     * from the device anymore (basically only in case of something we would
     * consider a kernel bug)
     */
    bool halted;

    int num_queues;
    FuseQueue *queues;
    /*
     * True if this export should follow the generic export's AioContext.
     * Will be false if the queues' AioContexts have been explicitly set by the
     * user, i.e. are expected to stay in those contexts.
     * (I.e. is always false if there is more than one queue.)
     */
    bool follow_aio_context;

    char *mountpoint;
    bool writable;
    bool growable;
    /* Whether allow_other was used as a mount option or not */
    bool allow_other;

    mode_t st_mode;
    uid_t st_uid;
    gid_t st_gid;
};

/* Parameters to the request processing coroutine */
typedef struct FuseRequestCoParam {
    FuseQueue *q;
    int got_request;
} FuseRequestCoParam;

static GHashTable *exports;

static void fuse_export_shutdown(BlockExport *exp);
static void fuse_export_delete(BlockExport *exp);
static void fuse_export_halt(FuseExport *exp);

static void init_exports_table(void);

static int mount_fuse_export(FuseExport *exp, Error **errp);
static int clone_fuse_fd(int fd, Error **errp);

static bool is_regular_file(const char *path, Error **errp);

static void read_from_fuse_fd(void *opaque);
static void coroutine_fn
fuse_co_process_request(FuseQueue *q, void *spillover_buf);

static void fuse_inc_in_flight(FuseExport *exp)
{
    if (qatomic_fetch_inc(&exp->in_flight) == 0) {
        /* Prevent export from being deleted */
        blk_exp_ref(&exp->common);
    }
}

static void fuse_dec_in_flight(FuseExport *exp)
{
    if (qatomic_fetch_dec(&exp->in_flight) == 1) {
        /* Wake AIO_WAIT_WHILE() */
        aio_wait_kick();

        /* Now the export can be deleted */
        blk_exp_unref(&exp->common);
    }
}

/**
 * Attach FUSE FD read handler.
 */
static void fuse_attach_handlers(FuseExport *exp)
{
    if (exp->halted) {
        return;
    }

    for (int i = 0; i < exp->num_queues; i++) {
        aio_set_fd_handler(exp->queues[i].ctx, exp->queues[i].fuse_fd,
                           read_from_fuse_fd, NULL, NULL, NULL,
                           &exp->queues[i]);
    }
    exp->fd_handler_set_up = true;
}

/**
 * Detach FUSE FD read handler.
 */
static void fuse_detach_handlers(FuseExport *exp)
{
    for (int i = 0; i < exp->num_queues; i++) {
        aio_set_fd_handler(exp->queues[i].ctx, exp->queues[i].fuse_fd,
                           NULL, NULL, NULL, NULL, NULL);
    }
    exp->fd_handler_set_up = false;
}

static void fuse_export_drained_begin(void *opaque)
{
    fuse_detach_handlers(opaque);
}

static void fuse_export_drained_end(void *opaque)
{
    FuseExport *exp = opaque;

    /* Refresh AioContext in case it changed */
    exp->common.ctx = blk_get_aio_context(exp->common.blk);
    if (exp->follow_aio_context) {
        assert(exp->num_queues == 1);
        exp->queues[0].ctx = exp->common.ctx;
    }

    fuse_attach_handlers(exp);
}

static bool fuse_export_drained_poll(void *opaque)
{
    FuseExport *exp = opaque;

    return qatomic_read(&exp->in_flight) > 0;
}

static const BlockDevOps fuse_export_blk_dev_ops = {
    .drained_begin = fuse_export_drained_begin,
    .drained_end   = fuse_export_drained_end,
    .drained_poll  = fuse_export_drained_poll,
};

static int fuse_export_create(BlockExport *blk_exp,
                              BlockExportOptions *blk_exp_args,
                              AioContext *const *multithread,
                              size_t mt_count,
                              Error **errp)
{
    ERRP_GUARD(); /* ensure clean-up even with error_fatal */
    FuseExport *exp = container_of(blk_exp, FuseExport, common);
    BlockExportOptionsFuse *args = &blk_exp_args->u.fuse;
    int ret;

    assert(blk_exp_args->type == BLOCK_EXPORT_TYPE_FUSE);

    if (multithread) {
        /* Guaranteed by common export code */
        assert(mt_count >= 1);

        exp->follow_aio_context = false;
        exp->num_queues = mt_count;
        exp->queues = g_new(FuseQueue, mt_count);

        for (size_t i = 0; i < mt_count; i++) {
            exp->queues[i] = (FuseQueue) {
                .exp = exp,
                .ctx = multithread[i],
                .fuse_fd = -1,
            };
        }
    } else {
        /* Guaranteed by common export code */
        assert(mt_count == 0);

        exp->follow_aio_context = true;
        exp->num_queues = 1;
        exp->queues = g_new(FuseQueue, 1);
        exp->queues[0] = (FuseQueue) {
            .exp = exp,
            .ctx = exp->common.ctx,
            .fuse_fd = -1,
        };
    }

    /* For growable and writable exports, take the RESIZE permission */
    if (args->growable || blk_exp_args->writable) {
        uint64_t blk_perm, blk_shared_perm;

        blk_get_perm(exp->common.blk, &blk_perm, &blk_shared_perm);

        ret = blk_set_perm(exp->common.blk, blk_perm | BLK_PERM_RESIZE,
                           blk_shared_perm, errp);
        if (ret < 0) {
            return ret;
        }
    }

    blk_set_dev_ops(exp->common.blk, &fuse_export_blk_dev_ops, exp);

    /*
     * We handle draining ourselves using an in-flight counter and by disabling
     * the FUSE fd handler. Do not queue BlockBackend requests, they need to
     * complete so the in-flight counter reaches zero.
     */
    blk_set_disable_request_queuing(exp->common.blk, true);

    init_exports_table();

    /*
     * It is important to do this check before calling is_regular_file() --
     * that function will do a stat(), which we would have to handle if we
     * already exported something on @mountpoint.  But we cannot, because
     * we are currently caught up here.
     * (Note that ideally we would want to resolve relative paths here,
     * but bdrv_make_absolute_filename() might do the wrong thing for
     * paths that contain colons, and realpath() would resolve symlinks,
     * which we do not want: The mount point is not going to be the
     * symlink's destination, but the link itself.)
     * So this will not catch all potential clashes, but hopefully at
     * least the most common one of specifying exactly the same path
     * string twice.
     */
    if (g_hash_table_contains(exports, args->mountpoint)) {
        error_setg(errp, "There already is a FUSE export on '%s'",
                   args->mountpoint);
        ret = -EEXIST;
        goto fail;
    }

    if (!is_regular_file(args->mountpoint, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    exp->mountpoint = g_strdup(args->mountpoint);
    exp->writable = blk_exp_args->writable;
    exp->growable = args->growable;

    /* set default */
    if (!args->has_allow_other) {
        args->allow_other = FUSE_EXPORT_ALLOW_OTHER_AUTO;
    }

    exp->st_mode = S_IFREG | S_IRUSR;
    if (exp->writable) {
        exp->st_mode |= S_IWUSR;
    }
    exp->st_uid = getuid();
    exp->st_gid = getgid();

    if (args->allow_other == FUSE_EXPORT_ALLOW_OTHER_AUTO) {
        /* Try allow_other == true first, ignore errors */
        exp->allow_other = true;
        ret = mount_fuse_export(exp, NULL);
        if (ret < 0) {
            exp->allow_other = false;
            ret = mount_fuse_export(exp, errp);
        }
    } else {
        exp->allow_other = args->allow_other == FUSE_EXPORT_ALLOW_OTHER_ON;
        ret = mount_fuse_export(exp, errp);
    }
    if (ret < 0) {
        goto fail;
    }

    g_hash_table_insert(exports, g_strdup(exp->mountpoint), NULL);

    assert(exp->num_queues >= 1);
    exp->queues[0].fuse_fd = fuse_session_fd(exp->fuse_session);
    ret = qemu_fcntl_addfl(exp->queues[0].fuse_fd, O_NONBLOCK);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to make FUSE FD non-blocking");
        goto fail;
    }

    for (int i = 1; i < exp->num_queues; i++) {
        int fd = clone_fuse_fd(exp->queues[0].fuse_fd, errp);
        if (fd < 0) {
            ret = fd;
            goto fail;
        }
        exp->queues[i].fuse_fd = fd;
    }

    fuse_attach_handlers(exp);
    return 0;

fail:
    fuse_export_shutdown(blk_exp);
    fuse_export_delete(blk_exp);
    return ret;
}

/**
 * Allocates the global @exports hash table.
 */
static void init_exports_table(void)
{
    if (exports) {
        return;
    }

    exports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * Create exp->fuse_session and mount it.  Expects exp->mountpoint,
 * exp->writable, and exp->allow_other to be set as intended for the mount.
 */
static int mount_fuse_export(FuseExport *exp, Error **errp)
{
    const char *fuse_argv[4];
    char *mount_opts;
    struct fuse_args fuse_args;
    int ret;

    /*
     * Note that these mount options differ from what we would pass to a direct
     * mount() call:
     * - nosuid, nodev, and noatime are not understood by the kernel; libfuse
     *   uses those options to construct the mount flags (MS_*)
     * - The FUSE kernel driver requires additional options (fd, rootmode,
     *   user_id, group_id); these will be set by libfuse.
     * Note that max_read is set here, while max_write is set via the FUSE INIT
     * operation.
     */
    mount_opts = g_strdup_printf("%s,nosuid,nodev,noatime,max_read=%zu,"
                                 "default_permissions%s",
                                 exp->writable ? "rw" : "ro",
                                 FUSE_MAX_READ_BYTES,
                                 exp->allow_other ? ",allow_other" : "");

    fuse_argv[0] = ""; /* Dummy program name */
    fuse_argv[1] = "-o";
    fuse_argv[2] = mount_opts;
    fuse_argv[3] = NULL;
    fuse_args = (struct fuse_args)FUSE_ARGS_INIT(3, (char **)fuse_argv);

    /* We just create the session for mounting/unmounting, no need to set ops */
    exp->fuse_session = fuse_session_new(&fuse_args, NULL, 0, NULL);
    g_free(mount_opts);
    if (!exp->fuse_session) {
        error_setg(errp, "Failed to set up FUSE session");
        return -EIO;
    }

    ret = fuse_session_mount(exp->fuse_session, exp->mountpoint);
    if (ret < 0) {
        error_setg(errp, "Failed to mount FUSE session to export");
        return -EIO;
    }
    exp->mounted = true;

    return 0;
}

/**
 * Clone the given /dev/fuse file descriptor, yielding a second FD from which
 * requests can be pulled for the associated filesystem.  Returns an FD on
 * success, and -errno on error.
 */
static int clone_fuse_fd(int fd, Error **errp)
{
    uint32_t src_fd = fd;
    int new_fd;
    int ret;

    /*
     * The name "/dev/fuse" is fixed, see libfuse's lib/fuse_loop_mt.c
     * (fuse_clone_chan()).
     */
    new_fd = open("/dev/fuse", O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (new_fd < 0) {
        ret = -errno;
        error_setg_errno(errp, errno, "Failed to open /dev/fuse");
        return ret;
    }

    ret = ioctl(new_fd, FUSE_DEV_IOC_CLONE, &src_fd);
    if (ret < 0) {
        ret = -errno;
        error_setg_errno(errp, errno, "Failed to clone FUSE FD");
        close(new_fd);
        return ret;
    }

    return new_fd;
}

/**
 * Try to read a single request from the FUSE FD.
 * Takes a FuseQueue pointer in `opaque`.
 *
 * Assumes the export's in-flight counter has already been incremented.
 *
 * If a request is available, process it.
 */
static void coroutine_fn co_read_from_fuse_fd(void *opaque)
{
    FuseQueue *q = opaque;
    int fuse_fd = q->fuse_fd;
    FuseExport *exp = q->exp;
    ssize_t ret;
    const struct fuse_in_header *in_hdr;
    struct iovec iov[2];
    void *spillover_buf = NULL;

    if (unlikely(exp->halted)) {
        goto no_request;
    }

    /*
     * If handling the last request consumed the spill-over buffer, allocate a
     * new one.  Align it to the block device's alignment, which admittedly is
     * only useful if FUSE_IN_PLACE_WRITE_BYTES is aligned, too.
     */
    if (unlikely(!q->spillover_buf)) {
        q->spillover_buf = blk_blockalign(exp->common.blk,
                                          FUSE_SPILLOVER_BUF_SIZE);
    }
    /* Construct the I/O vector to hold the FUSE request */
    iov[0] = (struct iovec) { q->request_buf, sizeof(q->request_buf) };
    iov[1] = (struct iovec) { q->spillover_buf, FUSE_SPILLOVER_BUF_SIZE };

    ret = RETRY_ON_EINTR(readv(fuse_fd, iov, ARRAY_SIZE(iov)));
    if (ret < 0 && errno == EAGAIN) {
        /* No request available */
        goto no_request;
    } else if (unlikely(ret < 0)) {
        error_report("Failed to read from FUSE device: %s", strerror(-ret));
        goto no_request;
    }

    if (unlikely(ret < sizeof(*in_hdr))) {
        error_report("Incomplete read from FUSE device, expected at least %zu "
                     "bytes, read %zi bytes; cannot trust subsequent "
                     "requests, halting the export",
                     sizeof(*in_hdr), ret);
        fuse_export_halt(exp);
        goto no_request;
    }

    in_hdr = (const struct fuse_in_header *)q->request_buf;
    if (unlikely(ret != in_hdr->len)) {
        error_report("Number of bytes read from FUSE device does not match "
                     "request size, expected %" PRIu32 " bytes, read %zi "
                     "bytes; cannot trust subsequent requests, halting the "
                     "export",
                     in_hdr->len, ret);
        fuse_export_halt(exp);
        goto no_request;
    }

    if (unlikely(ret > sizeof(q->request_buf))) {
        /* Spillover buffer used, take ownership */
        spillover_buf = q->spillover_buf;
        q->spillover_buf = NULL;
    }

    fuse_co_process_request(q, spillover_buf);

no_request:
    fuse_dec_in_flight(exp);
}

/**
 * Try to read and process a single request from the FUSE FD.
 * (To be used as a handler for when the FUSE FD becomes readable.)
 * Takes a FuseQueue pointer in `opaque`.
 */
static void read_from_fuse_fd(void *opaque)
{
    FuseQueue *q = opaque;
    Coroutine *co;

    co = qemu_coroutine_create(co_read_from_fuse_fd, q);
    /* Decremented by co_read_from_fuse_fd() */
    fuse_inc_in_flight(q->exp);
    qemu_coroutine_enter(co);
}

static void fuse_export_shutdown(BlockExport *blk_exp)
{
    FuseExport *exp = container_of(blk_exp, FuseExport, common);

    if (exp->fd_handler_set_up) {
        fuse_detach_handlers(exp);
    }

    if (exp->mountpoint) {
        /*
         * Safe to drop now, because we will not handle any requests for this
         * export anymore anyway (at least not from the main thread).
         */
        g_hash_table_remove(exports, exp->mountpoint);
    }
}

static void fuse_export_delete(BlockExport *blk_exp)
{
    FuseExport *exp = container_of(blk_exp, FuseExport, common);

    for (int i = 0; i < exp->num_queues; i++) {
        FuseQueue *q = &exp->queues[i];

        /* Queue 0's FD belongs to the FUSE session */
        if (i > 0 && q->fuse_fd >= 0) {
            close(q->fuse_fd);
        }
        if (q->spillover_buf) {
            qemu_vfree(q->spillover_buf);
        }
    }
    g_free(exp->queues);

    if (exp->fuse_session) {
        if (exp->mounted) {
            fuse_session_unmount(exp->fuse_session);
        }

        fuse_session_destroy(exp->fuse_session);
    }

    g_free(exp->mountpoint);
}

/**
 * Halt the export: Detach FD handlers, and set exp->halted to true, preventing
 * fuse_attach_handlers() from re-attaching them, therefore stopping all further
 * request processing.
 *
 * Call this function when an unrecoverable error happens that makes processing
 * all future requests unreliable.
 */
static void fuse_export_halt(FuseExport *exp)
{
    exp->halted = true;
    fuse_detach_handlers(exp);
}

/**
 * Check whether @path points to a regular file.  If not, put an
 * appropriate message into *errp.
 */
static bool is_regular_file(const char *path, Error **errp)
{
    struct stat statbuf;
    int ret;

    ret = stat(path, &statbuf);
    if (ret < 0) {
        error_setg_errno(errp, errno, "Failed to stat '%s'", path);
        return false;
    }

    if (!S_ISREG(statbuf.st_mode)) {
        error_setg(errp, "'%s' is not a regular file", path);
        return false;
    }

    return true;
}

/**
 * Process FUSE INIT.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn
fuse_co_init(FuseExport *exp, struct fuse_init_out *out,
             uint32_t max_readahead, uint32_t flags)
{
    const uint32_t supported_flags = FUSE_ASYNC_READ | FUSE_ASYNC_DIO;

    *out = (struct fuse_init_out) {
        .major = FUSE_KERNEL_VERSION,
        .minor = FUSE_KERNEL_MINOR_VERSION,
        .max_readahead = max_readahead,
        .max_write = FUSE_MAX_WRITE_BYTES,
        .flags = flags & supported_flags,
        .flags2 = 0,

        /* libfuse maximum: 2^16 - 1 */
        .max_background = UINT16_MAX,

        /* libfuse default: max_background * 3 / 4 */
        .congestion_threshold = (int)UINT16_MAX * 3 / 4,

        /* libfuse default: 1 */
        .time_gran = 1,

        /*
         * probably unneeded without FUSE_MAX_PAGES, but this would be the
         * libfuse default
         */
        .max_pages = DIV_ROUND_UP(FUSE_MAX_WRITE_BYTES,
                                  qemu_real_host_page_size()),

        /* Only needed for mappings (i.e. DAX) */
        .map_alignment = 0,
    };

    return sizeof(*out);
}

/**
 * Let clients get file attributes (i.e., stat() the file).
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn
fuse_co_getattr(FuseExport *exp, struct fuse_attr_out *out)
{
    int64_t length, allocated_blocks;
    time_t now = time(NULL);

    length = blk_co_getlength(exp->common.blk);
    if (length < 0) {
        return length;
    }

    allocated_blocks = bdrv_co_get_allocated_file_size(blk_bs(exp->common.blk));
    if (allocated_blocks <= 0) {
        allocated_blocks = DIV_ROUND_UP(length, 512);
    } else {
        allocated_blocks = DIV_ROUND_UP(allocated_blocks, 512);
    }

    *out = (struct fuse_attr_out) {
        .attr_valid = 1,
        .attr = {
            .ino        = 1,
            .mode       = exp->st_mode,
            .nlink      = 1,
            .uid        = exp->st_uid,
            .gid        = exp->st_gid,
            .size       = length,
            .blksize    = blk_bs(exp->common.blk)->bl.request_alignment,
            .blocks     = allocated_blocks,
            .atime      = now,
            .mtime      = now,
            .ctime      = now,
        },
    };

    return sizeof(*out);
}

static int coroutine_fn
fuse_co_do_truncate(const FuseExport *exp, int64_t size, bool req_zero_write,
                    PreallocMode prealloc)
{
    uint64_t blk_perm, blk_shared_perm;
    BdrvRequestFlags truncate_flags = 0;
    bool add_resize_perm;
    int ret, ret_check;

    /* Growable and writable exports have a permanent RESIZE permission */
    add_resize_perm = !exp->growable && !exp->writable;

    if (req_zero_write) {
        truncate_flags |= BDRV_REQ_ZERO_WRITE;
    }

    if (add_resize_perm) {
        if (!qemu_in_main_thread()) {
            /* Changing permissions like below only works in the main thread */
            return -EPERM;
        }

        blk_get_perm(exp->common.blk, &blk_perm, &blk_shared_perm);

        ret = blk_set_perm(exp->common.blk, blk_perm | BLK_PERM_RESIZE,
                           blk_shared_perm, NULL);
        if (ret < 0) {
            return ret;
        }
    }

    ret = blk_co_truncate(exp->common.blk, size, true, prealloc,
                          truncate_flags, NULL);

    if (add_resize_perm) {
        /* Must succeed, because we are only giving up the RESIZE permission */
        ret_check = blk_set_perm(exp->common.blk, blk_perm,
                                 blk_shared_perm, &error_abort);
        assert(ret_check == 0);
    }

    return ret;
}

/**
 * Let clients set file attributes.  Only resizing and changing
 * permissions (st_mode, st_uid, st_gid) is allowed.
 * Changing permissions is only allowed as far as it will actually
 * permit access: Read-only exports cannot be given +w, and exports
 * without allow_other cannot be given a different UID or GID, and
 * they cannot be given non-owner access.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn
fuse_co_setattr(FuseExport *exp, struct fuse_attr_out *out, uint32_t to_set,
                uint64_t size, uint32_t mode, uint32_t uid, uint32_t gid)
{
    int supported_attrs;
    int ret;

    /* SIZE and MODE are actually supported, the others can be safely ignored */
    supported_attrs = FATTR_SIZE | FATTR_MODE |
        FATTR_FH | FATTR_LOCKOWNER | FATTR_KILL_SUIDGID;
    if (exp->allow_other) {
        supported_attrs |= FATTR_UID | FATTR_GID;
    }

    if (to_set & ~supported_attrs) {
        return -ENOTSUP;
    }

    /* Do some argument checks first before committing to anything */
    if (to_set & FATTR_MODE) {
        /*
         * Without allow_other, non-owners can never access the export, so do
         * not allow setting permissions for them
         */
        if (!exp->allow_other && (mode & (S_IRWXG | S_IRWXO)) != 0) {
            return -EPERM;
        }

        /* +w for read-only exports makes no sense, disallow it */
        if (!exp->writable && (mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0) {
            return -EROFS;
        }
    }

    if (to_set & FATTR_SIZE) {
        if (!exp->writable) {
            return -EACCES;
        }

        ret = fuse_co_do_truncate(exp, size, true, PREALLOC_MODE_OFF);
        if (ret < 0) {
            return ret;
        }
    }

    if (to_set & FATTR_MODE) {
        /* Ignore FUSE-supplied file type, only change the mode */
        exp->st_mode = (mode & 07777) | S_IFREG;
    }

    if (to_set & FATTR_UID) {
        exp->st_uid = uid;
    }

    if (to_set & FATTR_GID) {
        exp->st_gid = gid;
    }

    return fuse_co_getattr(exp, out);
}

/**
 * Open an inode.  We only have a single inode in our exported filesystem, so we
 * just acknowledge the request.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn
fuse_co_open(FuseExport *exp, struct fuse_open_out *out)
{
    *out = (struct fuse_open_out) {
        .open_flags = FOPEN_DIRECT_IO | FOPEN_PARALLEL_DIRECT_WRITES,
    };
    return sizeof(*out);
}

/**
 * Handle client reads from the exported image.  Allocates *bufptr and reads
 * data from the block device into that buffer.
 * Returns the buffer (read) size on success, and -errno on error.
 * After use, *bufptr must be freed via qemu_vfree().
 */
static ssize_t coroutine_fn
fuse_co_read(FuseExport *exp, void **bufptr, uint64_t offset, uint32_t size)
{
    int64_t blk_len;
    void *buf;
    int ret;

    /* Limited by max_read, should not happen */
    if (size > FUSE_MAX_READ_BYTES) {
        return -EINVAL;
    }

    /**
     * Clients will expect short reads at EOF, so we have to limit
     * offset+size to the image length.
     */
    blk_len = blk_co_getlength(exp->common.blk);
    if (blk_len < 0) {
        return blk_len;
    }

    if (offset + size > blk_len) {
        size = blk_len - offset;
    }

    buf = qemu_try_blockalign(blk_bs(exp->common.blk), size);
    if (!buf) {
        return -ENOMEM;
    }

    ret = blk_co_pread(exp->common.blk, offset, size, buf, 0);
    if (ret < 0) {
        qemu_vfree(buf);
        return ret;
    }

    *bufptr = buf;
    return size;
}

/**
 * Handle client writes to the exported image.  @in_place_buf has the first
 * FUSE_IN_PLACE_WRITE_BYTES bytes of the data to be written, @spillover_buf
 * contains the rest (if any; NULL otherwise).
 * Data in @in_place_buf is assumed to be overwritten after yielding, so will
 * be copied to a bounce buffer beforehand.  @spillover_buf in contrast is
 * assumed to be exclusively owned and will be used as-is.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn
fuse_co_write(FuseExport *exp, struct fuse_write_out *out,
              uint64_t offset, uint32_t size,
              const void *in_place_buf, const void *spillover_buf)
{
    size_t in_place_size;
    void *copied;
    int64_t blk_len;
    int ret;
    struct iovec iov[2];
    QEMUIOVector qiov;

    /* Limited by max_write, should not happen */
    if (size > BDRV_REQUEST_MAX_BYTES) {
        return -EINVAL;
    }

    if (!exp->writable) {
        return -EACCES;
    }

    /* Must copy to bounce buffer before potentially yielding */
    in_place_size = MIN(size, FUSE_IN_PLACE_WRITE_BYTES);
    copied = blk_blockalign(exp->common.blk, in_place_size);
    memcpy(copied, in_place_buf, in_place_size);

    /**
     * Clients will expect short writes at EOF, so we have to limit
     * offset+size to the image length.
     */
    blk_len = blk_co_getlength(exp->common.blk);
    if (blk_len < 0) {
        ret = blk_len;
        goto fail_free_buffer;
    }

    if (offset + size > blk_len) {
        if (exp->growable) {
            ret = fuse_co_do_truncate(exp, offset + size, true,
                                      PREALLOC_MODE_OFF);
            if (ret < 0) {
                goto fail_free_buffer;
            }
        } else {
            size = blk_len - offset;
        }
    }

    iov[0] = (struct iovec) {
        .iov_base = copied,
        .iov_len = in_place_size,
    };
    if (size > FUSE_IN_PLACE_WRITE_BYTES) {
        assert(size - FUSE_IN_PLACE_WRITE_BYTES <= FUSE_SPILLOVER_BUF_SIZE);
        iov[1] = (struct iovec) {
            .iov_base = (void *)spillover_buf,
            .iov_len = size - FUSE_IN_PLACE_WRITE_BYTES,
        };
        qemu_iovec_init_external(&qiov, iov, 2);
    } else {
        qemu_iovec_init_external(&qiov, iov, 1);
    }
    ret = blk_co_pwritev(exp->common.blk, offset, size, &qiov, 0);
    if (ret < 0) {
        goto fail_free_buffer;
    }

    qemu_vfree(copied);

    *out = (struct fuse_write_out) {
        .size = size,
    };
    return sizeof(*out);

fail_free_buffer:
    qemu_vfree(copied);
    return ret;
}

/**
 * Let clients perform various fallocate() operations.
 * Return 0 on success (no 'out' object), and -errno on error.
 */
static ssize_t coroutine_fn
fuse_co_fallocate(FuseExport *exp,
                  uint64_t offset, uint64_t length, uint32_t mode)
{
    int64_t blk_len;
    int ret;

    if (!exp->writable) {
        return -EACCES;
    }

    blk_len = blk_co_getlength(exp->common.blk);
    if (blk_len < 0) {
        return blk_len;
    }

#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
    if (mode & FALLOC_FL_KEEP_SIZE) {
        length = MIN(length, blk_len - offset);
    }
#endif /* CONFIG_FALLOCATE_PUNCH_HOLE */

    if (!mode) {
        /* We can only fallocate at the EOF with a truncate */
        if (offset < blk_len) {
            return -EOPNOTSUPP;
        }

        if (offset > blk_len) {
            /* No preallocation needed here */
            ret = fuse_co_do_truncate(exp, offset, true, PREALLOC_MODE_OFF);
            if (ret < 0) {
                return ret;
            }
        }

        ret = fuse_co_do_truncate(exp, offset + length, true,
                                  PREALLOC_MODE_FALLOC);
    }
#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
    else if (mode & FALLOC_FL_PUNCH_HOLE) {
        if (!(mode & FALLOC_FL_KEEP_SIZE)) {
            return -EINVAL;
        }

        do {
            int size = MIN(length, BDRV_REQUEST_MAX_BYTES);

            ret = blk_co_pwrite_zeroes(exp->common.blk, offset, size,
                                       BDRV_REQ_MAY_UNMAP |
                                       BDRV_REQ_NO_FALLBACK);
            if (ret == -ENOTSUP) {
                /*
                 * fallocate() specifies to return EOPNOTSUPP for unsupported
                 * operations
                 */
                ret = -EOPNOTSUPP;
            }

            offset += size;
            length -= size;
        } while (ret == 0 && length > 0);
    }
#endif /* CONFIG_FALLOCATE_PUNCH_HOLE */
#ifdef CONFIG_FALLOCATE_ZERO_RANGE
    else if (mode & FALLOC_FL_ZERO_RANGE) {
        if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + length > blk_len) {
            /* No need for zeroes, we are going to write them ourselves */
            ret = fuse_co_do_truncate(exp, offset + length, false,
                                      PREALLOC_MODE_OFF);
            if (ret < 0) {
                return ret;
            }
        }

        do {
            int size = MIN(length, BDRV_REQUEST_MAX_BYTES);

            ret = blk_co_pwrite_zeroes(exp->common.blk,
                                       offset, size, 0);
            offset += size;
            length -= size;
        } while (ret == 0 && length > 0);
    }
#endif /* CONFIG_FALLOCATE_ZERO_RANGE */
    else {
        ret = -EOPNOTSUPP;
    }

    return ret < 0 ? ret : 0;
}

/**
 * Let clients fsync the exported image.
 * Return 0 on success (no 'out' object), and -errno on error.
 */
static ssize_t coroutine_fn fuse_co_fsync(FuseExport *exp)
{
    return blk_co_flush(exp->common.blk);
}

/**
 * Called before an FD to the exported image is closed.  (libfuse
 * notes this to be a way to return last-minute errors.)
 * Return 0 on success (no 'out' object), and -errno on error.
 */
static ssize_t coroutine_fn fuse_co_flush(FuseExport *exp)
{
    return blk_co_flush(exp->common.blk);
}

#ifdef CONFIG_FUSE_LSEEK
/**
 * Let clients inquire allocation status.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn
fuse_co_lseek(FuseExport *exp, struct fuse_lseek_out *out,
              uint64_t offset, uint32_t whence)
{
    if (whence != SEEK_HOLE && whence != SEEK_DATA) {
        return -EINVAL;
    }

    while (true) {
        int64_t pnum;
        int ret;

        ret = bdrv_co_block_status_above(blk_bs(exp->common.blk), NULL,
                                         offset, INT64_MAX, &pnum, NULL, NULL);
        if (ret < 0) {
            return ret;
        }

        if (!pnum && (ret & BDRV_BLOCK_EOF)) {
            int64_t blk_len;

            /*
             * If blk_getlength() rounds (e.g. by sectors), then the
             * export length will be rounded, too.  However,
             * bdrv_block_status_above() may return EOF at unaligned
             * offsets.  We must not let this become visible and thus
             * always simulate a hole between @offset (the real EOF)
             * and @blk_len (the client-visible EOF).
             */

            blk_len = blk_co_getlength(exp->common.blk);
            if (blk_len < 0) {
                return blk_len;
            }

            if (offset > blk_len || whence == SEEK_DATA) {
                return -ENXIO;
            }

            *out = (struct fuse_lseek_out) {
                .offset = offset,
            };
            return sizeof(*out);
        }

        if (ret & BDRV_BLOCK_DATA) {
            if (whence == SEEK_DATA) {
                *out = (struct fuse_lseek_out) {
                    .offset = offset,
                };
                return sizeof(*out);
            }
        } else {
            if (whence == SEEK_HOLE) {
                *out = (struct fuse_lseek_out) {
                    .offset = offset,
                };
                return sizeof(*out);
            }
        }

        /* Safety check against infinite loops */
        if (!pnum) {
            return -ENXIO;
        }

        offset += pnum;
    }
}
#endif

/**
 * Write a FUSE response to the given @fd, using a single buffer consecutively
 * containing both the response header and data: Initialize *out_hdr, and write
 * it plus @response_data_length consecutive bytes to @fd.
 *
 * @fd: FUSE file descriptor
 * @req_id: Corresponding request ID
 * @out_hdr: Pointer to buffer that will hold the output header, and
 *           additionally already contains @response_data_length data bytes
 *           starting at *out_hdr + 1.
 * @err: Error code (-errno, or 0 in case of success)
 * @response_data_length: Length of data to return (following *out_hdr)
 */
static int fuse_write_response(int fd, uint32_t req_id,
                               struct fuse_out_header *out_hdr, int err,
                               size_t response_data_length)
{
    void *write_ptr = out_hdr;
    size_t to_write = sizeof(*out_hdr) + response_data_length;
    ssize_t ret;

    *out_hdr = (struct fuse_out_header) {
        .len = to_write,
        .error = err,
        .unique = req_id,
    };

    while (true) {
        ret = RETRY_ON_EINTR(write(fd, write_ptr, to_write));
        if (ret < 0) {
            ret = -errno;
            error_report("Failed to write to FUSE device: %s", strerror(-ret));
            return ret;
        } else {
            to_write -= ret;
            if (to_write > 0) {
                write_ptr += ret;
            } else {
                return 0; /* success */
            }
        }
    }
}

/**
 * Write a FUSE response to the given @fd, using separate buffers for the
 * response header and data: Initialize *out_hdr, and write it plus the data in
 * *buf to @fd.
 *
 * In contrast to fuse_write_response(), this function cannot return errors, and
 * will always return success (error code 0).
 *
 * @fd: FUSE file descriptor
 * @req_id: Corresponding request ID
 * @out_hdr: Pointer to buffer that will hold the output header
 * @buf: Pointer to response data
 * @buflen: Length of response data
 */
static int fuse_write_buf_response(int fd, uint32_t req_id,
                                   struct fuse_out_header *out_hdr,
                                   const void *buf, size_t buflen)
{
    struct iovec iov[2] = {
        { out_hdr, sizeof(*out_hdr) },
        { (void *)buf, buflen },
    };
    struct iovec *iovp = iov;
    unsigned iov_count = ARRAY_SIZE(iov);
    size_t to_write = sizeof(*out_hdr) + buflen;
    ssize_t ret;

    *out_hdr = (struct fuse_out_header) {
        .len = to_write,
        .unique = req_id,
    };

    while (true) {
        ret = RETRY_ON_EINTR(writev(fd, iovp, iov_count));
        if (ret < 0) {
            ret = -errno;
            error_report("Failed to write to FUSE device: %s", strerror(-ret));
            return ret;
        } else {
            to_write -= ret;
            if (to_write > 0) {
                iov_discard_front(&iovp, &iov_count, ret);
            } else {
                return 0; /* success */
            }
        }
    }
}

/*
 * For use in fuse_co_process_request():
 * Returns a pointer to the parameter object for the given operation (inside of
 * queue->request_buf, which is assumed to hold a fuse_in_header first).
 * Verifies that the object is complete (queue->request_buf is large enough to
 * hold it in one piece, and the request length includes the whole object).
 *
 * Note that queue->request_buf may be overwritten after yielding, so the
 * returned pointer must not be used across a function that may yield!
 */
#define FUSE_IN_OP_STRUCT(op_name, queue) \
    ({ \
        const struct fuse_in_header *__in_hdr = \
            (const struct fuse_in_header *)(queue)->request_buf; \
        const struct fuse_##op_name##_in *__in = \
            (const struct fuse_##op_name##_in *)(__in_hdr + 1); \
        const size_t __param_len = sizeof(*__in_hdr) + sizeof(*__in); \
        uint32_t __req_len; \
        \
        QEMU_BUILD_BUG_ON(sizeof((queue)->request_buf) < __param_len); \
        \
        __req_len = __in_hdr->len; \
        if (__req_len < __param_len) { \
            warn_report("FUSE request truncated (%" PRIu32 " < %zu)", \
                        __req_len, __param_len); \
            ret = -EINVAL; \
            break; \
        } \
        __in; \
    })

/*
 * For use in fuse_co_process_request():
 * Returns a pointer to the return object for the given operation (inside of
 * out_buf, which is assumed to hold a fuse_out_header first).
 * Verifies that out_buf is large enough to hold the whole object.
 *
 * (out_buf should be a char[] array.)
 */
#define FUSE_OUT_OP_STRUCT(op_name, out_buf) \
    ({ \
        struct fuse_out_header *__out_hdr = \
            (struct fuse_out_header *)(out_buf); \
        struct fuse_##op_name##_out *__out = \
            (struct fuse_##op_name##_out *)(__out_hdr + 1); \
        \
        QEMU_BUILD_BUG_ON(sizeof(*__out_hdr) + sizeof(*__out) > \
                          sizeof(out_buf)); \
        \
        __out; \
    })

/**
 * Process a FUSE request, incl. writing the response.
 *
 * Note that yielding in any request-processing function can overwrite the
 * contents of q->request_buf.  Anything that takes a buffer needs to take
 * care that the content is copied before yielding.
 *
 * @spillover_buf can contain the tail of a write request too large to fit into
 * q->request_buf.  This function takes ownership of it (i.e. will free it),
 * which assumes that its contents will not be overwritten by concurrent
 * requests (as opposed to q->request_buf).
 */
static void coroutine_fn
fuse_co_process_request(FuseQueue *q, void *spillover_buf)
{
    FuseExport *exp = q->exp;
    uint32_t opcode;
    uint64_t req_id;
    /*
     * Return buffer.  Must be large enough to hold all return headers, but does
     * not include space for data returned by read requests.
     * (FUSE_IN_OP_STRUCT() verifies at compile time that out_buf is indeed
     * large enough.)
     */
    char out_buf[sizeof(struct fuse_out_header) +
                 MAX_CONST(sizeof(struct fuse_init_out),
                 MAX_CONST(sizeof(struct fuse_open_out),
                 MAX_CONST(sizeof(struct fuse_attr_out),
                 MAX_CONST(sizeof(struct fuse_write_out),
                           sizeof(struct fuse_lseek_out)))))];
    struct fuse_out_header *out_hdr = (struct fuse_out_header *)out_buf;
    /* For read requests: Data to be returned */
    void *out_data_buffer = NULL;
    ssize_t ret;

    /* Limit scope to ensure pointer is no longer used after yielding */
    {
        const struct fuse_in_header *in_hdr =
            (const struct fuse_in_header *)q->request_buf;

        opcode = in_hdr->opcode;
        req_id = in_hdr->unique;
    }

    switch (opcode) {
    case FUSE_INIT: {
        const struct fuse_init_in *in = FUSE_IN_OP_STRUCT(init, q);
        ret = fuse_co_init(exp, FUSE_OUT_OP_STRUCT(init, out_buf),
                           in->max_readahead, in->flags);
        break;
    }

    case FUSE_OPEN:
        ret = fuse_co_open(exp, FUSE_OUT_OP_STRUCT(open, out_buf));
        break;

    case FUSE_RELEASE:
        ret = 0;
        break;

    case FUSE_LOOKUP:
        ret = -ENOENT; /* There is no node but the root node */
        break;

    case FUSE_GETATTR:
        ret = fuse_co_getattr(exp, FUSE_OUT_OP_STRUCT(attr, out_buf));
        break;

    case FUSE_SETATTR: {
        const struct fuse_setattr_in *in = FUSE_IN_OP_STRUCT(setattr, q);
        ret = fuse_co_setattr(exp, FUSE_OUT_OP_STRUCT(attr, out_buf),
                              in->valid, in->size, in->mode, in->uid, in->gid);
        break;
    }

    case FUSE_READ: {
        const struct fuse_read_in *in = FUSE_IN_OP_STRUCT(read, q);
        ret = fuse_co_read(exp, &out_data_buffer, in->offset, in->size);
        break;
    }

    case FUSE_WRITE: {
        const struct fuse_write_in *in = FUSE_IN_OP_STRUCT(write, q);
        uint32_t req_len;

        req_len = ((const struct fuse_in_header *)q->request_buf)->len;
        if (unlikely(req_len < sizeof(struct fuse_in_header) + sizeof(*in) +
                               in->size)) {
            warn_report("FUSE WRITE truncated; received %zu bytes of %" PRIu32,
                        req_len - sizeof(struct fuse_in_header) - sizeof(*in),
                        in->size);
            ret = -EINVAL;
            break;
        }

        /*
         * poll_fuse_fd() has checked that in_hdr->len matches the number of
         * bytes read, which cannot exceed the max_write value we set
         * (FUSE_MAX_WRITE_BYTES).  So we know that FUSE_MAX_WRITE_BYTES >=
         * in_hdr->len >= in->size + X, so this assertion must hold.
         */
        assert(in->size <= FUSE_MAX_WRITE_BYTES);

        /*
         * Passing a pointer to `in` (i.e. the request buffer) is fine because
         * fuse_co_write() takes care to copy its contents before potentially
         * yielding.
         */
        ret = fuse_co_write(exp, FUSE_OUT_OP_STRUCT(write, out_buf),
                            in->offset, in->size, in + 1, spillover_buf);
        break;
    }

    case FUSE_FALLOCATE: {
        const struct fuse_fallocate_in *in = FUSE_IN_OP_STRUCT(fallocate, q);
        ret = fuse_co_fallocate(exp, in->offset, in->length, in->mode);
        break;
    }

    case FUSE_FSYNC:
        ret = fuse_co_fsync(exp);
        break;

    case FUSE_FLUSH:
        ret = fuse_co_flush(exp);
        break;

#ifdef CONFIG_FUSE_LSEEK
    case FUSE_LSEEK: {
        const struct fuse_lseek_in *in = FUSE_IN_OP_STRUCT(lseek, q);
        ret = fuse_co_lseek(exp, FUSE_OUT_OP_STRUCT(lseek, out_buf),
                            in->offset, in->whence);
        break;
    }
#endif

    default:
        ret = -ENOSYS;
    }

    /* Ignore errors from fuse_write*(), nothing we can do anyway */
    if (out_data_buffer) {
        assert(ret >= 0);
        fuse_write_buf_response(q->fuse_fd, req_id, out_hdr,
                                out_data_buffer, ret);
        qemu_vfree(out_data_buffer);
    } else {
        fuse_write_response(q->fuse_fd, req_id, out_hdr,
                            ret < 0 ? ret : 0,
                            ret < 0 ? 0 : ret);
    }

    qemu_vfree(spillover_buf);
}

const BlockExportDriver blk_exp_fuse = {
    .type               = BLOCK_EXPORT_TYPE_FUSE,
    .instance_size      = sizeof(FuseExport),
    .create             = fuse_export_create,
    .delete             = fuse_export_delete,
    .request_shutdown   = fuse_export_shutdown,
};
