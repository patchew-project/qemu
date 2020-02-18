/*
 *  Copyright (C) 2020  Coiby Xu <coiby.xu@gmail.com>
 *
 *  standone-alone vhost-user-blk device server backend
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
#include "backends/vhost-user-blk-server.h"
#include "block/block_int.h"
#include "io/net-listener.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu-common.h"
#include "qemu-version.h"
#include "qom/object_interfaces.h"
#include "sysemu/block-backend.h"
#define QEMU_VU_OPT_CACHE         256
#define QEMU_VU_OPT_AIO           257
#define QEMU_VU_OBJ_ID   "vu_disk"
static QemuOptsList qemu_object_opts = {
    .name = "object",
    .implied_opt_name = "qom-type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_object_opts.head),
    .desc = {
        { }
    },
};
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

static VuBlockDev *vu_block_device;

static void vus_shutdown(void)
{

    Error *local_err = NULL;
    job_cancel_sync_all();
    bdrv_close_all();
    user_creatable_del(QEMU_VU_OBJ_ID, &local_err);
}

int main(int argc, char **argv)
{
    BlockBackend *blk;
    BlockDriverState *bs;
    bool readonly = false;
    char *sockpath = NULL;
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
    bool exit_when_panic = false;

    error_init(argv[0]);

    module_call_init(MODULE_INIT_QOM);
    qemu_init_exec_dir(argv[0]);

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'e':
            exit_when_panic = true;
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

    char buf[300];
    snprintf(buf, 300, "%s,id=%s,node-name=%s,unix-socket=%s,writable=%s",
             TYPE_VHOST_USER_BLK_SERVER, QEMU_VU_OBJ_ID, bdrv_get_node_name(bs),
             sockpath, !readonly ? "on" : "off");
    /* While calling user_creatable_del, 'object' group is required */
    qemu_add_opts(&qemu_object_opts);
    QemuOpts *opts = qemu_opts_parse(&qemu_object_opts, buf, true, &local_err);
    if (local_err) {
        error_report_err(local_err);
        goto error;
    }

    Object *obj = user_creatable_add_opts(opts, &local_err);

    if (local_err) {
        error_report_err(local_err);
        goto error;
    }

    vu_block_device = VHOST_USER_BLK_SERVER(obj);
    vu_block_device->exit_when_panic = exit_when_panic;

    do {
        main_loop_wait(false);
    } while (!vu_block_device->exit_when_panic || !vu_block_device->vu_server->close);

 error:
    vus_shutdown();
    exit(EXIT_SUCCESS);
}
