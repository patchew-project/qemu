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
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "sysemu/runstate.h" /* for qemu_system_killed() prototype */
#include "qemu/cutils.h"
#include "qemu/coroutine.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/log.h"
#include "qemu/option_int.h"
#include "trace/control.h"
#include "io/channel-util.h"
#include "io/channel-buffer.h"
#include "migration/qemu-file-channel.h"
#include "migration/qemu-file.h"
#include "qemu-snapshot.h"

int64_t page_size;
int64_t page_mask;
int page_bits;
int64_t slice_size;
int64_t slice_mask;
int slice_bits;

static QemuOptsList snap_blk_optslist = {
    .name = "blockdev",
    .implied_opt_name = "file.filename",
    .head = QTAILQ_HEAD_INITIALIZER(snap_blk_optslist.head),
    .desc = {
        { /*End of the list */ }
    },
};

static struct {
    bool revert;                /* Operation is snapshot revert */

    int fd;                     /* Migration channel fd */
    int rp_fd;                  /* Return path fd (for postcopy) */

    const char *blk_optstr;     /* Command-line options for vmstate blockdev */
    QDict *blk_options;         /* Blockdev options */
    int blk_flags;              /* Blockdev flags */

    bool postcopy;              /* Use postcopy */
    int postcopy_percent;       /* Start postcopy after % of normal pages loaded */
} params;

static StateSaveCtx state_save_ctx;
static StateLoadCtx state_load_ctx;

static enum {
    RUNNING = 0,
    TERMINATED
} state;

#ifdef CONFIG_POSIX
void qemu_system_killed(int signum, pid_t pid)
{
}
#endif /* CONFIG_POSIX */

StateSaveCtx *get_save_context(void)
{
    return &state_save_ctx;
}

StateLoadCtx *get_load_context(void)
{
    return &state_load_ctx;
}

static void init_save_context(void)
{
    memset(&state_save_ctx, 0, sizeof(state_save_ctx));
}

static void destroy_save_context(void)
{
    StateSaveCtx *s = get_save_context();

    if (s->f_vmstate) {
        qemu_fclose(s->f_vmstate);
    }
    if (s->blk) {
        blk_flush(s->blk);
        blk_unref(s->blk);
    }
    if (s->zero_buf) {
        qemu_vfree(s->zero_buf);
    }
    if (s->ioc_leader) {
        object_unref(OBJECT(s->ioc_leader));
    }
    if (s->ioc_pages) {
        object_unref(OBJECT(s->ioc_pages));
    }
}

static void init_load_context(void)
{
    memset(&state_load_ctx, 0, sizeof(state_load_ctx));
}

static void destroy_load_context(void)
{
    StateLoadCtx *s = get_load_context();

    if (s->f_vmstate) {
        qemu_fclose(s->f_vmstate);
    }
    if (s->blk) {
        blk_unref(s->blk);
    }
    if (s->aio_ring) {
        aio_ring_free(s->aio_ring);
    }
    if (s->ioc_leader) {
        object_unref(OBJECT(s->ioc_leader));
    }
}

static BlockBackend *image_open_opts(const char *optstr, QDict *options, int flags)
{
    BlockBackend *blk;
    Error *local_err = NULL;

    /* Open image and create block backend */
    blk = blk_new_open(NULL, NULL, options, flags, &local_err);
    if (!blk) {
        error_reportf_err(local_err, "Failed to open image '%s': ", optstr);
        return NULL;
    }

    blk_set_enable_write_cache(blk, true);

    return blk;
}

/* Use BH to enter coroutine from the main loop */
static void enter_co_bh(void *opaque)
{
    Coroutine *co = (Coroutine *) opaque;
    qemu_coroutine_enter(co);
}

static void coroutine_fn snapshot_save_co(void *opaque)
{
    StateSaveCtx *s = get_save_context();
    QIOChannel *ioc_fd;
    uint8_t *buf;
    size_t count;
    int res = -1;

    init_save_context();

    /* Block backend */
    s->blk = image_open_opts(params.blk_optstr, params.blk_options,
                             params.blk_flags);
    if (!s->blk) {
        goto fail;
    }

    /* QEMUFile on vmstate */
    s->f_vmstate = qemu_fopen_bdrv_vmstate(blk_bs(s->blk), 1);
    qemu_file_set_blocking(s->f_vmstate, false);

    /* QEMUFile on migration fd */
    ioc_fd = qio_channel_new_fd(params.fd, &error_fatal);
    qio_channel_set_name(QIO_CHANNEL(ioc_fd), "migration-channel-incoming");
    s->f_fd = qemu_fopen_channel_input(ioc_fd);
    object_unref(OBJECT(ioc_fd));
    /* Use non-blocking mode in coroutine */
    qemu_file_set_blocking(s->f_fd, false);

    /* Buffer channel to store leading part of migration stream */
    s->ioc_leader = qio_channel_buffer_new(INPLACE_READ_MAX);
    qio_channel_set_name(QIO_CHANNEL(s->ioc_leader), "migration-leader-buffer");

    /* Page coalescing buffer */
    s->ioc_pages = qio_channel_buffer_new(128 * 1024);
    qio_channel_set_name(QIO_CHANNEL(s->ioc_pages), "migration-page-buffer");

    /* Bounce buffer to fill unwritten extents in image backing */
    s->zero_buf = qemu_blockalign0(blk_bs(s->blk), slice_size);

    /*
     * Here we stash the leading part of migration stream without promoting read
     * position. Later we'll make use of it when writing the vmstate stream.
     */
    count = qemu_peek_buffer(s->f_fd, &buf, INPLACE_READ_MAX, 0);
    res = qemu_file_get_error(s->f_fd);
    if (res < 0) {
        goto fail;
    }
    qio_channel_write(QIO_CHANNEL(s->ioc_leader), (char *) buf, count, NULL);

    res = save_state_main(s);
    if (res) {
        error_report("Failed to save snapshot: %s", strerror(-res));
    }

fail:
    destroy_save_context();
    state = TERMINATED;
}

static void coroutine_fn snapshot_load_co(void *opaque)
{
    StateLoadCtx *s = get_load_context();
    QIOChannel *ioc_fd;
    uint8_t *buf;
    size_t count;
    int res = -1;

    init_load_context();

    /* Block backend */
    s->blk = image_open_opts(params.blk_optstr, params.blk_options,
                             params.blk_flags);
    if (!s->blk) {
        goto fail;
    }

    /* QEMUFile on vmstate */
    s->f_vmstate = qemu_fopen_bdrv_vmstate(blk_bs(s->blk), 0);
    qemu_file_set_blocking(s->f_vmstate, false);

    /* QEMUFile on migration fd */
    ioc_fd = qio_channel_new_fd(params.fd, NULL);
    qio_channel_set_name(QIO_CHANNEL(ioc_fd), "migration-channel-outgoing");
    s->f_fd = qemu_fopen_channel_output(ioc_fd);
    object_unref(OBJECT(ioc_fd));
    qemu_file_set_blocking(s->f_fd, false);

    /* Buffer channel to store leading part of migration stream */
    s->ioc_leader = qio_channel_buffer_new(INPLACE_READ_MAX);
    qio_channel_set_name(QIO_CHANNEL(s->ioc_leader), "migration-leader-buffer");

    /* AIO ring */
    s->aio_ring = aio_ring_new(ram_load_aio_co, AIO_RING_SIZE, AIO_RING_INFLIGHT);

    /*
     * Here we stash the leading part of vmstate stream without promoting read
     * position.
     */
    count = qemu_peek_buffer(s->f_vmstate, &buf, INPLACE_READ_MAX, 0);
    res = qemu_file_get_error(s->f_vmstate);
    if (res < 0) {
        goto fail;
    }
    qio_channel_write(QIO_CHANNEL(s->ioc_leader), (char *) buf, count, NULL);

    res = load_state_main(s);
    if (res) {
        error_report("Failed to load snapshot: %s", strerror(-res));
    }

fail:
    destroy_load_context();
    state = TERMINATED;
}

static void usage(const char *name)
{
    printf(
        "Usage: %s [options] <image-blockspec>\n"
        "QEMU External Snapshot Utility\n"
        "\n"
        "'image-blockspec' is a block device specification for vmstate image\n"
        "\n"
        "  -h, --help                display this help and exit\n"
        "  -V, --version             output version information and exit\n"
        "\n"
        "Options:\n"
        "  -T, --trace [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
        "                            specify tracing options\n"
        "  -r, --revert              revert to snapshot\n"
        "      --uri=fd:<fd>         specify migration fd\n"
        "      --page-size=<size>    specify target page size\n"
        "      --postcopy=<%%ram>     switch to postcopy after %%ram loaded\n"
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

enum {
    OPTION_PAGE_SIZE = 256,
    OPTION_POSTCOPY,
    OPTION_URI,
};

static void process_options(int argc, char *argv[])
{
    static const char *s_opt = "rhVT:";
    static const struct option l_opt[] = {
        { "page-size", required_argument, NULL, OPTION_PAGE_SIZE },
        { "postcopy", required_argument, NULL, OPTION_POSTCOPY },
        { "uri", required_argument, NULL,  OPTION_URI },
        { "revert", no_argument, NULL, 'r' },
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "trace", required_argument, NULL, 'T' },
        { NULL, 0, NULL, 0 }
    };

    bool has_page_size = false;
    bool has_uri = false;

    long target_page_size = qemu_real_host_page_size;
    int uri_fd = -1;
    bool revert = false;
    bool postcopy = false;
    long postcopy_percent = 0;
    const char *blk_optstr;
    QemuOpts *blk_opts;
    QDict *blk_options;
    int c;

    while ((c = getopt_long(argc, argv, s_opt, l_opt, NULL)) != -1) {
        switch (c) {
        case '?':
            exit(EXIT_FAILURE);

        case 'h':
            usage(argv[0]);
            exit(EXIT_SUCCESS);

        case 'V':
            version(argv[0]);
            exit(EXIT_SUCCESS);

        case 'T':
            trace_opt_parse(optarg);
            break;

        case 'r':
            if (revert) {
                error_report("-r and --revert can only be specified once");
                exit(EXIT_FAILURE);
            }
            revert = true;

            break;

        case OPTION_POSTCOPY:
        {
            const char *r;

            if (postcopy) {
                error_report("--postcopy can only be specified once");
                exit(EXIT_FAILURE);
            }
            postcopy = true;

            qemu_strtol(optarg, &r, 10, &postcopy_percent);
            if (*r != '\0' || postcopy_percent < 0 || postcopy_percent > 100) {
                error_report("Invalid argument to --postcopy");
                exit(EXIT_FAILURE);
            }

            break;
        }

        case OPTION_PAGE_SIZE:
        {
            const char *r;

            if (has_page_size) {
                error_report("--page-size can only be specified once");
                exit(EXIT_FAILURE);
            }
            has_page_size = true;

            qemu_strtol(optarg, &r, 0, &target_page_size);
            if (*r != '\0' || (target_page_size & (target_page_size - 1)) != 0 ||
                    target_page_size < PAGE_SIZE_MIN ||
                    target_page_size > PAGE_SIZE_MAX) {
                error_report("Invalid argument to --page-size");
                exit(EXIT_FAILURE);
            }

            break;
        }

        case OPTION_URI:
        {
            const char *p;

            if (has_uri) {
                error_report("--uri can only be specified once");
                exit(EXIT_FAILURE);
            }
            has_uri = true;

            /* Only "--uri=fd:<fd>" is currently supported */
            if (strstart(optarg, "fd:", &p)) {
                const char *r;
                long fd;

                qemu_strtol(p, &r, 10, &fd);
                if (*r != '\0' || fd <= STDERR_FILENO) {
                    error_report("Invalid FD value");
                    exit(EXIT_FAILURE);
                }

                uri_fd = qemu_dup_flags(fd, O_CLOEXEC);
                if (uri_fd < 0) {
                    error_report("Could not dup FD %ld", fd);
                    exit(EXIT_FAILURE);
                }

                /* Close original fd */
                close(fd);
            } else {
                error_report("Invalid argument to --uri");
                exit(EXIT_FAILURE);
            }

            break;
        }

        default:
            g_assert_not_reached();
        }
    }

    if ((argc - optind) != 1) {
        error_report("Invalid number of arguments");
        exit(EXIT_FAILURE);
    }

    blk_optstr = argv[optind];

    blk_opts = qemu_opts_parse_noisily(&snap_blk_optslist, blk_optstr, true);
    if (!blk_opts) {
        exit(EXIT_FAILURE);
    }
    blk_options = qemu_opts_to_qdict(blk_opts, NULL);
    qemu_opts_reset(&snap_blk_optslist);

    /* Enforced block layer options */
    qdict_put_str(blk_options, "driver", "qcow2");
    qdict_put_null(blk_options, "backing");
    qdict_put_str(blk_options, "overlap-check", "none");
    qdict_put_str(blk_options, "auto-read-only", "off");
    qdict_put_str(blk_options, "detect-zeroes", "off");
    qdict_put_str(blk_options, "lazy-refcounts", "on");
    qdict_put_str(blk_options, "file.auto-read-only", "off");
    qdict_put_str(blk_options, "file.detect-zeroes", "off");

    params.revert = revert;

    if (uri_fd != -1) {
        params.fd = params.rp_fd = uri_fd;
    } else {
        params.fd = revert ? STDOUT_FILENO : STDIN_FILENO;
        params.rp_fd = revert ? STDIN_FILENO : -1;
    }
    params.blk_optstr = blk_optstr;
    params.blk_options = blk_options;
    params.blk_flags = revert ? 0 : BDRV_O_RDWR;
    params.postcopy = postcopy;
    params.postcopy_percent = postcopy_percent;

    page_size = target_page_size;
    page_mask = ~(page_size - 1);
    page_bits = ctz64(page_size);
    slice_size = revert ? SLICE_SIZE_REVERT : SLICE_SIZE;
    slice_mask = ~(slice_size - 1);
    slice_bits = ctz64(slice_size);
}

int main(int argc, char **argv)
{
    Coroutine *co;

    os_setup_early_signal_handling();
    os_setup_signal_handling();
    error_init(argv[0]);
    qemu_init_exec_dir(argv[0]);
    module_call_init(MODULE_INIT_TRACE);
    module_call_init(MODULE_INIT_QOM);
    qemu_init_main_loop(&error_fatal);
    bdrv_init();

    qemu_add_opts(&qemu_trace_opts);
    process_options(argc, argv);

    if (!trace_init_backends()) {
        exit(EXIT_FAILURE);
    }
    trace_init_file();
    qemu_set_log(LOG_TRACE);

    ram_init_state();

    if (params.revert) {
        co = qemu_coroutine_create(snapshot_load_co, NULL);
    } else {
        co = qemu_coroutine_create(snapshot_save_co, NULL);
    }
    aio_bh_schedule_oneshot(qemu_get_aio_context(), enter_co_bh, co);

    do {
        main_loop_wait(false);
    } while (state != TERMINATED);

    exit(EXIT_SUCCESS);
}
