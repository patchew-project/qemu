/*
 * QEMU External Snapshot Utility
 *
 * Copyright Virtuozzo GmbH, 2021
 *
 * Authors:
 *  Andrey Gruzdev   <andrey.gruzdev@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <getopt.h>

#include "qemu-common.h"
#include "qemu-version.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "sysemu/block-backend.h"
#include "sysemu/runstate.h" /* for qemu_system_killed() prototype */
#include "qemu/cutils.h"
#include "qemu/coroutine.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/log.h"
#include "trace/control.h"
#include "io/channel-util.h"
#include "io/channel-buffer.h"
#include "migration/qemu-file-channel.h"
#include "migration/qemu-file.h"
#include "qemu-snap.h"

/* QCOW2 image options */
#define BLK_FORMAT_DRIVER       "qcow2"
#define BLK_CREATE_OPT_STRING   "preallocation=off,lazy_refcounts=on,"  \
                                        "extended_l2=off,compat=v3,cluster_size=1M,"    \
                                        "refcount_bits=8"
/* L2 cache size to cover 2TB of memory */
#define BLK_L2_CACHE_SIZE       "16M"
/* Single L2 cache entry for the whole L2 table */
#define BLK_L2_CACHE_ENTRY_SIZE "1M"

#define OPT_CACHE   256
#define OPT_AIO     257

/* Snapshot task execution state */
typedef struct SnapTaskState {
    QEMUBH *bh;                 /* BH to enter task's coroutine */
    Coroutine *co;              /* Coroutine to execute task */

    int ret;                    /* Return code, -EINPROGRESS until complete */
} SnapTaskState;

/* Parameters for snapshot saving */
typedef struct SnapSaveParams {
    const char *filename;       /* QCOW2 image file name */
    int64_t image_size;         /* QCOW2 virtual image size */
    int bdrv_flags;             /* BDRV flags (cache/AIO mode)*/
    bool writethrough;          /* BDRV writes in FUA mode */

    int64_t page_size;          /* Target page size to use */

    int fd;                     /* Migration stream input FD */
} SnapSaveParams;

/* Parameters for snapshot saving */
typedef struct SnapLoadParams {
    const char *filename;       /* QCOW2 image file name */
    int bdrv_flags;             /* BDRV flags (cache/AIO mode)*/

    int64_t page_size;          /* Target page size to use */
    bool postcopy;              /* Use postcopy */
    /* Switch to postcopy after postcopy_percent% of RAM loaded */
    int postcopy_percent;

    int fd;                     /* Migration stream output FD */
    int rp_fd;                  /* Return-path FD (for postcopy) */
} SnapLoadParams;

static SnapSaveState save_state;
static SnapLoadState load_state;

#ifdef CONFIG_POSIX
void qemu_system_killed(int signum, pid_t pid)
{
}
#endif /* CONFIG_POSIX */

static void snap_shutdown(void)
{
    bdrv_close_all();
}

SnapSaveState *snap_save_get_state(void)
{
    return &save_state;
}

SnapLoadState *snap_load_get_state(void)
{
    return &load_state;
}

static void snap_save_init_state(void)
{
    memset(&save_state, 0, sizeof(save_state));
    save_state.status = -1;
}

static void snap_save_destroy_state(void)
{
    SnapSaveState *sn = snap_save_get_state();

    if (sn->ioc_lbuf) {
        object_unref(OBJECT(sn->ioc_lbuf));
    }
    if (sn->ioc_pbuf) {
        object_unref(OBJECT(sn->ioc_pbuf));
    }
    if (sn->f_vmstate) {
        qemu_fclose(sn->f_vmstate);
    }
    if (sn->blk) {
        blk_flush(sn->blk);
        blk_unref(sn->blk);

        /* Delete image file in case of failure */
        if (sn->status) {
            qemu_unlink(sn->filename);
        }
    }
}

static void snap_load_init_state(void)
{
    memset(&load_state, 0, sizeof(load_state));
}

static void snap_load_destroy_state(void)
{
    SnapLoadState *sn = snap_load_get_state();

    if (sn->has_rp_listen_thread) {
        qemu_thread_join(&sn->rp_listen_thread);
    }

    if (sn->aio_pool) {
        aio_pool_free(sn->aio_pool);
    }
    if (sn->ioc_lbuf) {
        object_unref(OBJECT(sn->ioc_lbuf));
    }
    if (sn->f_vmstate) {
        qemu_fclose(sn->f_vmstate);
    }
    if (sn->blk) {
        blk_unref(sn->blk);
    }
}

static BlockBackend *snap_create(const char *filename, int64_t image_size,
        int flags, bool writethrough)
{
    char *create_opt_string;
    QDict *blk_opts;
    BlockBackend *blk;
    Error *local_err = NULL;

    /* Create QCOW2 image with given parameters */
    create_opt_string = g_strdup(BLK_CREATE_OPT_STRING);
    bdrv_img_create(filename, BLK_FORMAT_DRIVER, NULL, NULL,
            create_opt_string, image_size, flags, true, &local_err);
    g_free(create_opt_string);

    if (local_err) {
        error_reportf_err(local_err, "Could not create '%s': ", filename);
        goto fail;
    }

    /* Block backend open options */
    blk_opts = qdict_new();
    qdict_put_str(blk_opts, "driver", BLK_FORMAT_DRIVER);
    qdict_put_str(blk_opts, "l2-cache-size", BLK_L2_CACHE_SIZE);
    qdict_put_str(blk_opts, "l2-cache-entry-size", BLK_L2_CACHE_ENTRY_SIZE);

    /* Open block backend instance for the created image */
    blk = blk_new_open(filename, NULL, blk_opts, flags, &local_err);
    if (!blk) {
        error_reportf_err(local_err, "Could not open '%s': ", filename);
        /* Delete image file */
        qemu_unlink(filename);
        goto fail;
    }

    blk_set_enable_write_cache(blk, !writethrough);
    return blk;

fail:
    return NULL;
}

static BlockBackend *snap_open(const char *filename, int flags)
{
    QDict *blk_opts;
    BlockBackend *blk;
    Error *local_err = NULL;

    /* Block backend open options */
    blk_opts = qdict_new();
    qdict_put_str(blk_opts, "driver", BLK_FORMAT_DRIVER);
    qdict_put_str(blk_opts, "l2-cache-size", BLK_L2_CACHE_SIZE);
    qdict_put_str(blk_opts, "l2-cache-entry-size", BLK_L2_CACHE_ENTRY_SIZE);

    /* Open block backend instance */
    blk = blk_new_open(filename, NULL, blk_opts, flags, &local_err);
    if (!blk) {
        error_reportf_err(local_err, "Could not open '%s': ", filename);
        return NULL;
    }

    return blk;
}

static void coroutine_fn do_snap_save_co(void *opaque)
{
    SnapTaskState *task_state = opaque;
    SnapSaveState *sn = snap_save_get_state();

    /* Switch to non-blocking mode in coroutine context */
    qemu_file_set_blocking(sn->f_fd, false);
    /* Enter main routine */
    task_state->ret = snap_save_state_main(sn);
}

static void coroutine_fn do_snap_load_co(void *opaque)
{
    SnapTaskState *task_state = opaque;
    SnapLoadState *sn = snap_load_get_state();

    /* Switch to non-blocking mode in coroutine context */
    qemu_file_set_blocking(sn->f_vmstate, false);
    qemu_file_set_blocking(sn->f_fd, false);
    /* Initialize AIO buffer pool in coroutine context */
    sn->aio_pool = aio_pool_new(DEFAULT_PAGE_SIZE, AIO_BUFFER_SIZE,
            AIO_TASKS_MAX);
    /* Enter main routine */
    task_state->ret = snap_load_state_main(sn);
}

/* We use BH to enter coroutine from the main loop context */
static void enter_co_bh(void *opaque)
{
    SnapTaskState *task_state = opaque;

    qemu_coroutine_enter(task_state->co);
    /* Delete BH once we entered coroutine from the main loop */
    qemu_bh_delete(task_state->bh);
    task_state->bh = NULL;
}

static int run_snap_task(CoroutineEntry *entry)
{
    SnapTaskState task_state;

    task_state.bh = qemu_bh_new(enter_co_bh, &task_state);
    task_state.co = qemu_coroutine_create(entry, &task_state);
    task_state.ret = -EINPROGRESS;

    qemu_bh_schedule(task_state.bh);
    while (task_state.ret == -EINPROGRESS) {
        main_loop_wait(false);
    }

    return task_state.ret;
}

static int snap_save(const SnapSaveParams *params)
{
    SnapSaveState *sn;
    QIOChannel *ioc_fd;
    uint8_t *buf;
    size_t count;
    int res = -1;

    snap_ram_init_state(ctz64(params->page_size));
    snap_save_init_state();
    sn = snap_save_get_state();

    sn->filename = params->filename;

    ioc_fd = qio_channel_new_fd(params->fd, NULL);
    qio_channel_set_name(QIO_CHANNEL(ioc_fd), "snap-channel-incoming");
    sn->f_fd = qemu_fopen_channel_input(ioc_fd);
    object_unref(OBJECT(ioc_fd));

    /* Create buffer channel to store leading part of incoming stream */
    sn->ioc_lbuf = qio_channel_buffer_new(INPLACE_READ_MAX);
    qio_channel_set_name(QIO_CHANNEL(sn->ioc_lbuf), "snap-leader-buffer");

    count = qemu_peek_buffer(sn->f_fd, &buf, INPLACE_READ_MAX, 0);
    res = qemu_file_get_error(sn->f_fd);
    if (res < 0) {
        goto fail;
    }
    qio_channel_write(QIO_CHANNEL(sn->ioc_lbuf), (char *) buf, count, NULL);

    /* Used for incoming page coalescing */
    sn->ioc_pbuf = qio_channel_buffer_new(128 * 1024);
    qio_channel_set_name(QIO_CHANNEL(sn->ioc_pbuf), "snap-page-buffer");

    sn->blk = snap_create(params->filename, params->image_size,
            params->bdrv_flags, params->writethrough);
    if (!sn->blk) {
        goto fail;
    }

    /* Open QEMUFile so we can write to BDRV vmstate area */
    sn->f_vmstate = qemu_fopen_bdrv_vmstate(blk_bs(sn->blk), 1);

    res = run_snap_task(do_snap_save_co);
    if (res) {
        error_report("Failed to save snapshot: error=%d", res);
    }

fail:
    snap_save_destroy_state();
    snap_ram_destroy_state();

    return res;
}

static int snap_load(SnapLoadParams *params)
{
    SnapLoadState *sn;
    QIOChannel *ioc_fd;
    QIOChannel *ioc_rp_fd;
    uint8_t *buf;
    size_t count;
    int res = -1;

    snap_ram_init_state(ctz64(params->page_size));
    snap_load_init_state();
    sn = snap_load_get_state();

    sn->postcopy = params->postcopy;
    sn->postcopy_percent = params->postcopy_percent;

    ioc_fd = qio_channel_new_fd(params->fd, NULL);
    qio_channel_set_name(QIO_CHANNEL(ioc_fd), "snap-channel-outgoing");
    sn->f_fd = qemu_fopen_channel_output(ioc_fd);
    object_unref(OBJECT(ioc_fd));

    /* Open return path QEMUFile in case we shall use postcopy */
    if (params->postcopy) {
        ioc_rp_fd = qio_channel_new_fd(params->rp_fd, NULL);
        qio_channel_set_name(QIO_CHANNEL(ioc_fd), "snap-channel-rp");
        sn->f_rp_fd = qemu_fopen_channel_input(ioc_rp_fd);
        object_unref(OBJECT(ioc_rp_fd));
    }

    sn->blk = snap_open(params->filename, params->bdrv_flags);
    if (!sn->blk) {
        goto fail;
    }
    /* Open QEMUFile for BDRV vmstate area */
    sn->f_vmstate = qemu_fopen_bdrv_vmstate(blk_bs(sn->blk), 0);

    /* Create buffer channel to store leading part of VMSTATE stream */
    sn->ioc_lbuf = qio_channel_buffer_new(INPLACE_READ_MAX);
    qio_channel_set_name(QIO_CHANNEL(sn->ioc_lbuf), "snap-leader-buffer");

    count = qemu_peek_buffer(sn->f_vmstate, &buf, INPLACE_READ_MAX, 0);
    res = qemu_file_get_error(sn->f_vmstate);
    if (res < 0) {
        goto fail;
    }
    qio_channel_write(QIO_CHANNEL(sn->ioc_lbuf), (char *) buf, count, NULL);

    res = run_snap_task(do_snap_load_co);
    if (res) {
        error_report("Failed to load snapshot: error=%d", res);
    }

fail:
    snap_load_destroy_state();
    snap_ram_destroy_state();

    return res;
}

static int64_t cvtnum_full(const char *name, const char *value,
        int64_t min, int64_t max)
{
    uint64_t res;
    int err;

    err = qemu_strtosz(value, NULL, &res);
    if (err < 0 && err != -ERANGE) {
        error_report("Invalid %s specified. You may use "
                     "k, M, G, T, P or E suffixes for", name);
        error_report("kilobytes, megabytes, gigabytes, terabytes, "
                     "petabytes and exabytes.");
        return err;
    }
    if (err == -ERANGE || res > max || res < min) {
        error_report("Invalid %s specified. Must be between %" PRId64
                     " and %" PRId64 ".", name, min, max);
        return -ERANGE;
    }

    return res;
}

static int64_t cvtnum(const char *name, const char *value)
{
    return cvtnum_full(name, value, 0, INT64_MAX);
}

static bool is_2power(int64_t val)
{
    return val && ((val & (val - 1)) == 0);
}

static void usage(const char *name)
{
    printf(
        "Usage: %s [OPTIONS] save|load FILE\n"
        "QEMU External Snapshot Utility\n"
        "\n"
        "  -h, --help                display this help and exit\n"
        "  -V, --version             output version information and exit\n"
        "\n"
        "General purpose options:\n"
        "  -t, --trace [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
        "                            specify tracing options\n"
        "\n"
        "Image options:\n"
        "  -s, --image-size=SIZE     size of image to create for 'save'\n"
        "  -n, --nocache             disable host cache\n"
        "      --cache=MODE          set cache mode (none, writeback, ...)\n"
        "      --aio=MODE            set AIO mode (native, io_uring or threads)\n"
        "\n"
        "Snapshot options:\n"
        "  -S, --page-size=SIZE      target page size\n"
        "  -p, --postcopy=%%RAM       switch to postcopy after '%%RAM' loaded\n"
        "\n"
        QEMU_HELP_BOTTOM "\n", name);
}

static void version(const char *name)
{
    printf(
        "%s " QEMU_FULL_VERSION "\n"
        "Written by Andrey Gruzdev.\n"
        "\n"
        QEMU_COPYRIGHT "\n"
        "This is free software; see the source for copying conditions.  There is NO\n"
        "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
        name);
}

int main(int argc, char **argv)
{
    static const struct option l_opt[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "image-size", required_argument, NULL, 's' },
        { "page-size", required_argument, NULL, 'S' },
        { "postcopy", required_argument, NULL, 'p' },
        { "nocache", no_argument, NULL, 'n' },
        { "cache", required_argument, NULL, OPT_CACHE },
        { "aio", required_argument, NULL, OPT_AIO },
        { "trace", required_argument, NULL, 't' },
        { NULL, 0, NULL, 0 }
    };
    static const char *s_opt = "hVs:S:p:nt:";

    int ch;
    int l_ind = 0;

    bool seen_image_size = false;
    bool seen_page_size = false;
    bool seen_postcopy = false;
    bool seen_cache = false;
    bool seen_aio = false;
    int64_t image_size = 0;
    int64_t page_size = DEFAULT_PAGE_SIZE;
    int64_t postcopy_percent = 0;
    int bdrv_flags = 0;
    bool writethrough = false;
    bool postcopy = false;
    const char *cmd_name;
    const char *file_name;
    Error *local_err = NULL;

#ifdef CONFIG_POSIX
    signal(SIGPIPE, SIG_IGN);
#endif
    error_init(argv[0]);
    module_call_init(MODULE_INIT_TRACE);
    module_call_init(MODULE_INIT_QOM);

    qemu_add_opts(&qemu_trace_opts);
    qemu_init_exec_dir(argv[0]);

    while ((ch = getopt_long(argc, argv, s_opt, l_opt, &l_ind)) != -1) {
        switch (ch) {
        case '?':
            error_report("Try `%s --help' for more information", argv[0]);
            return EXIT_FAILURE;

        case 's':
            if (seen_image_size) {
                error_report("-s and --image-size can only be specified once");
                return EXIT_FAILURE;
            }
            seen_image_size = true;

            image_size = cvtnum(l_opt[l_ind].name, optarg);
            if (image_size <= 0) {
                error_report("Invalid image size parameter '%s'", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'S':
            if (seen_page_size) {
                error_report("-S and --page-size can only be specified once");
                return EXIT_FAILURE;
            }
            seen_page_size = true;

            page_size = cvtnum(l_opt[l_ind].name, optarg);
            if (page_size <= 0 || !is_2power(page_size) ||
                    page_size > PAGE_SIZE_MAX) {
                error_report("Invalid target page size parameter '%s'", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'p':
            if (seen_postcopy) {
                error_report("-p and --postcopy can only be specified once");
                return EXIT_FAILURE;
            }
            seen_postcopy = true;

            postcopy_percent = cvtnum(l_opt[l_ind].name, optarg);
            if (!(postcopy_percent > 0 && postcopy_percent < 100)) {
                error_report("Invalid postcopy %%RAM parameter '%s'", optarg);
                return EXIT_FAILURE;
            }
            postcopy = true;
            break;

        case 'n':
            optarg = (char *) "none";
            /* fallthrough */

        case OPT_CACHE:
            if (seen_cache) {
                error_report("-n and --cache can only be specified once");
                return EXIT_FAILURE;
            }
            seen_cache = true;

            if (bdrv_parse_cache_mode(optarg, &bdrv_flags, &writethrough)) {
                error_report("Invalid cache mode '%s'", optarg);
                return EXIT_FAILURE;
            }
            break;

        case OPT_AIO:
            if (seen_aio) {
                error_report("--aio can only be specified once");
                return EXIT_FAILURE;
            }
            seen_aio = true;

            if (bdrv_parse_aio(optarg, &bdrv_flags)) {
                error_report("Invalid AIO mode '%s'", optarg);
                return EXIT_FAILURE;
            }
            break;

        case 'V':
            version(argv[0]);
            return EXIT_SUCCESS;

        case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;

        case 't':
            trace_opt_parse(optarg);
            break;

        }
    }

    if ((argc - optind) != 2) {
        error_report("Invalid number of arguments");
        return EXIT_FAILURE;
    }

    if (!trace_init_backends()) {
        return EXIT_FAILURE;
    }
    trace_init_file();
    qemu_set_log(LOG_TRACE);

    if (qemu_init_main_loop(&local_err)) {
        error_report_err(local_err);
        return EXIT_FAILURE;
    }

    bdrv_init();
    atexit(snap_shutdown);

    cmd_name = argv[optind];
    file_name = argv[optind + 1];

    if (!strcmp(cmd_name, "save")) {
        SnapSaveParams params = {
            .filename = file_name,
            .image_size = image_size,
            .page_size = page_size,
            .bdrv_flags = (bdrv_flags | BDRV_O_RDWR),
            .writethrough = writethrough,
            .fd = STDIN_FILENO };
        int res;

        if (seen_postcopy) {
            error_report("-p and --postcopy cannot be used for 'save'");
            return EXIT_FAILURE;
        }
        if (!seen_image_size) {
            error_report("-s or --size are required for 'save'");
            return EXIT_FAILURE;
        }

        res = snap_save(&params);
        if (res < 0) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } else if (!strcmp(cmd_name, "load")) {
        SnapLoadParams params = {
            .filename = file_name,
            .bdrv_flags = bdrv_flags,
            .postcopy = postcopy,
            .postcopy_percent = postcopy_percent,
            .page_size = page_size,
            .fd = STDOUT_FILENO,
            .rp_fd = STDIN_FILENO };
        int res;

        if (seen_image_size) {
            error_report("-s and --size cannot be used for 'load'");
            return EXIT_FAILURE;
        }

        res = snap_load(&params);
        if (res < 0) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    error_report("Invalid command");
    return EXIT_FAILURE;
}
