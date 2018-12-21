/*
 *  Copyright 2016  Red Hat, Inc.
 *
 *  TCMU Handler Program
 *
 *  Authors:
 *    Fam Zheng <famz@redhat.com>
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
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "sysemu/block-backend.h"
#include "block/block_int.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/option.h"
#include "block/snapshot.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qom/object_interfaces.h"
#include "crypto/init.h"
#include "trace/control.h"
#include "tcmu/tcmu.h"
#include <getopt.h>
#include "qemu-version.h"

#define QEMU_TCMU_OPT_OBJECT        260

static int verbose;
static enum { RUNNING, TERMINATING, TERMINATED } state;

static void usage(const char *name)
{
    (printf) (
"Usage:\n" 
"%s [OPTIONS]\n"
"QEMU TCMU Handler\n"
"\n"
"  -h, --help                display this help and exit\n"
"  -V, --version             output version information and exit\n"
"\n"
"General purpose options:\n"
"  -v, --verbose             display extra debugging information\n"
"  -x, --handler-name=NAME   handler name to be used as the subtype for TCMU\n"
"  --object type,id=ID,...   define an object such as 'secret' for providing\n"
"                            passwords and/or encryption keys\n"
"  -T, --trace [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
"                            specify tracing options\n"
"\n"
"Report bugs to <qemu-devel@nongnu.org>\n"
    , name);
}

static void version(const char *name)
{
    printf("%s v" QEMU_FULL_VERSION "\n", name);
}

static void termsig_handler(int signum)
{
    atomic_cmpxchg(&state, RUNNING, TERMINATING);
    qemu_notify_event();
}

static QemuOptsList qemu_object_opts = {
    .name = "object",
    .implied_opt_name = "qom-type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_object_opts.head),
    .desc = {
        { }
    },
};

static void qemu_tcmu_shutdown(void)
{
    job_cancel_sync_all();
    bdrv_close_all();
}

int main(int argc, char **argv)
{
    const char *sopt = "hVvx:T:";
    bool starting = true;
    struct option lopt[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { "verbose", no_argument, NULL, 'v' },
        { "object", required_argument, NULL, QEMU_TCMU_OPT_OBJECT },
        { "handler-name", required_argument, NULL, 'x' },
        { "trace", required_argument, NULL, 'T' },
        { NULL, 0, NULL, 0 }
    };
    int ch;
    int opt_ind = 0;
    Error *local_err = NULL;
    char *trace_file = NULL;
    const char *subtype = "qemu";

    struct sigaction sa_sigterm;
    memset(&sa_sigterm, 0, sizeof(sa_sigterm));
    sa_sigterm.sa_handler = termsig_handler;
    sigaction(SIGTERM, &sa_sigterm, NULL);
    sigaction(SIGINT, &sa_sigterm, NULL);

    module_call_init(MODULE_INIT_TRACE);
    qcrypto_init(&error_fatal);

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_object_opts);
    qemu_add_opts(&qemu_trace_opts);
    qemu_init_exec_dir(argv[0]);

    while ((ch = getopt_long(argc, argv, sopt, lopt, &opt_ind)) != -1) {
        switch (ch) {
        case 'x':
            subtype = optarg;
            break;
        case 'v':
            verbose = 1;
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
        case QEMU_TCMU_OPT_OBJECT: {
            QemuOpts *opts;
            opts = qemu_opts_parse_noisily(&qemu_object_opts,
                                           optarg, true);
            if (!opts) {
                exit(EXIT_FAILURE);
            }
        }   break;
        case 'T':
            g_free(trace_file);
            trace_file = trace_opt_parse(optarg);
            break;
        }
    }

    if ((argc - optind) != 0) {
        error_report("Invalid number of arguments");
        error_printf("Try `%s --help' for more information.\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (qemu_opts_foreach(&qemu_object_opts,
                          user_creatable_add_opts_foreach,
                          NULL, NULL)) {
        exit(EXIT_FAILURE);
    }

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file(trace_file);
    qemu_set_log(LOG_TRACE);

    if (qemu_init_main_loop(&local_err)) {
        error_report_err(local_err);
        exit(EXIT_FAILURE);
    }
    bdrv_init();
    atexit(qemu_tcmu_shutdown);

    /* now when the initialization is (almost) complete, chdir("/")
     * to free any busy filesystems */
    if (chdir("/") < 0) {
        error_report("Could not chdir to root directory: %s",
                     strerror(errno));
        exit(EXIT_FAILURE);
    }

    state = RUNNING;
    do {
        main_loop_wait(starting);
        if (starting) {
            qemu_tcmu_start(subtype, &local_err);
            if (local_err) {
                error_report_err(local_err);
                exit(EXIT_FAILURE);
            }
            starting = false;
        }
        if (state == TERMINATING) {
            state = TERMINATED;
            qemu_tcmu_stop();
        }
    } while (state != TERMINATED);

    exit(EXIT_SUCCESS);
}
