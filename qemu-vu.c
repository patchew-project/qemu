/*
 *  Copyright (C) 2020  Coiby Xu <coiby.xu@gmail.com>
 *
 *  Vhost-user-blk device backend
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <getopt.h>
#include <libgen.h>
#include "block/vhost-user.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qom/object_interfaces.h"
#include "io/net-listener.h"
#include "qemu-version.h"

#define QEMU_VU_OPT_CACHE         256

#define QEMU_VU_OPT_AIO           257

static char *srcpath;

static void usage(const char *name)
{
    (printf) (
"Usage: %s [OPTIONS] FILE\n"
"  or:  %s -L [OPTIONS]\n"
"QEMU Vhost-user Server Utility\n"
"\n"
"  -h, --help                display this help and exit\n"
"  -V, --version             output version information and exit\n"
"\n"
"Connection properties:\n"
"  -k, --socket=PATH         path to the unix socket\n"
"\n"
"General purpose options:\n"
"  -e, -- exit-panic         When the panic callback is called, the program\n"
"                            will exit. Useful for make check-qtest.\n"
"\n"
"Block device options:\n"
"  -f, --format=FORMAT       set image format (raw, qcow2, ...)\n"
"  -r, --read-only           export read-only\n"
"  -n, --nocache             disable host cache\n"
"      --cache=MODE          set cache mode (none, writeback, ...)\n"
"      --aio=MODE            set AIO mode (native or threads)\n"
"\n"
QEMU_HELP_BOTTOM "\n"
    , name, name);
}

static void version(const char *name)
{
    printf(
"%s " QEMU_FULL_VERSION "\n"
"Written by Coiby Xu, based on qemu-nbd by Anthony Liguori\n"
"\n"
QEMU_COPYRIGHT "\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    , name);
}

static VubDev *vub_device;

static void vus_shutdown(void)
{
    job_cancel_sync_all();
    bdrv_close_all();
    vub_free(vub_device, false);
}

int main(int argc, char **argv)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    bool readonly = false;
    char *sockpath = NULL;
    int64_t fd_size;
    const char *sopt = "hVrnvek:f:";
    struct option lopt[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "exit-panic", no_argument, NULL, 'e' },
        { "socket", required_argument, NULL, 'k' },
        { "read-only", no_argument, NULL, 'r' },
        { "nocache", no_argument, NULL, 'n' },
        { "cache", required_argument, NULL, QEMU_VU_OPT_CACHE },
        { "aio", required_argument, NULL, QEMU_VU_OPT_AIO },
        { "format", required_argument, NULL, 'f' },
        { NULL, 0, NULL, 0 }
    };
    int ch;
    int opt_ind = 0;
    int flags = BDRV_O_RDWR;
    bool seen_cache = false;
    bool seen_aio = false;
    const char *fmt = NULL;
    Error *local_err = NULL;
    QDict *options = NULL;
    bool writethrough = true;
    bool exit_panic = false;

    error_init(argv[0]);

    module_call_init(MODULE_INIT_QOM);
    qemu_init_exec_dir(argv[0]);

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'e':
            exit_panic = true;
            break;
        case 'n':
            optarg = (char *) "none";
            /* fallthrough */
        case QEMU_VU_OPT_CACHE:
            if (seen_cache) {
                error_report("-n and --cache can only be specified once");
                exit(EXIT_FAILURE);
            }
            seen_cache = true;
            if (bdrv_parse_cache_mode(optarg, &flags, &writethrough) == -1) {
                error_report("Invalid cache mode `%s'", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case QEMU_VU_OPT_AIO:
            if (seen_aio) {
                error_report("--aio can only be specified once");
                exit(EXIT_FAILURE);
            }
            seen_aio = true;
            if (!strcmp(optarg, "native")) {
                flags |= BDRV_O_NATIVE_AIO;
            } else if (!strcmp(optarg, "threads")) {
                /* this is the default */
            } else {
               error_report("invalid aio mode `%s'", optarg);
               exit(EXIT_FAILURE);
            }
            break;
        case 'r':
            readonly = true;
            flags &= ~BDRV_O_RDWR;
            break;
        case 'k':
            sockpath = optarg;
            if (sockpath[0] != '/') {
                error_report("socket path must be absolute");
                exit(EXIT_FAILURE);
            }
            break;
        case 'f':
            fmt = optarg;
            break;
        case 'V':
            version(argv[0]);
            exit(0);
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
            break;
        case '?':
            error_report("Try `%s --help' for more information.", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if ((argc - optind) != 1) {
        error_report("Invalid number of arguments");
        error_printf("Try `%s --help' for more information.\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (qemu_init_main_loop(&local_err)) {
        error_report_err(local_err);
        exit(EXIT_FAILURE);
    }
    bdrv_init();

    srcpath = argv[optind];
    if (fmt) {
        options = qdict_new();
        qdict_put_str(options, "driver", fmt);
    }
    blk = blk_new_open(srcpath, NULL, options, flags, &local_err);

    if (!blk) {
        error_reportf_err(local_err, "Failed to blk_new_open '%s': ",
                          argv[optind]);
        exit(EXIT_FAILURE);
    }
    bs = blk_bs(blk);

    blk_set_enable_write_cache(blk, !writethrough);

    fd_size = blk_getlength(blk);
    if (fd_size < 0) {
        error_report("Failed to determine the image length: %s",
                     strerror(-fd_size));
        exit(EXIT_FAILURE);
    }

    AioContext *ctx = bdrv_get_aio_context(bs);
    bdrv_invalidate_cache(bs, NULL);

    vub_device = g_new0(VubDev, 1);
    vub_device->unix_socket = g_strdup(sockpath);
    vub_device->writable = !readonly;
    vub_device->blkcfg.wce = !writethrough;
    vub_device->backend = blk;
    vub_device->ctx = ctx;
    vub_initialize_config(bs, &vub_device->blkcfg);
    vub_device->listener = qio_net_listener_new();
    vub_device->exit_panic = exit_panic;

    qio_net_listener_set_name(vub_device->listener,
                              "vhost-user-backend-listener");

    SocketAddress *addr = g_new0(SocketAddress, 1);
    addr->u.q_unix.path = (char *) sockpath;
    addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    Error **errp = NULL;
    if (qio_net_listener_open_sync(vub_device->listener, addr, 1, errp) < 0) {
        goto error;
    }

    qio_net_listener_set_client_func(vub_device->listener,
                                     vub_accept,
                                     vub_device,
                                     NULL);

    QTAILQ_INIT(&vub_device->clients);

    do {
        main_loop_wait(false);
    } while (!vub_device->exit_panic || !vub_device->close);

 error:
    vus_shutdown();
    exit(EXIT_SUCCESS);
}
