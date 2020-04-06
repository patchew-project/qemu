/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/boards.h"
#include "sysemu/blockdev.h"
#include "qapi/error.h"
#include "qemu-options.h"
#include "qemu-parse.h"
#include "remote-opts.h"
#include "monitor/monitor.h"

/*
 * In remote process, we parse only subset of options. The code
 * taken from vl.c to re-use in remote command line parser.
 */
void parse_cmdline(int argc, char **argv, char **envp)
{
    int optind;
    const char *optarg;
    MachineClass *mc;

    /* from vl.c */
    optind = 0;

    /* second pass of option parsing */

    for (;;) {
        if (optind >= argc) {
            break;
        }
        if (argv[optind][0] != '-') {
            loc_set_cmdline(argv, optind, 1);
            drive_add(IF_DEFAULT, 0, argv[optind++], HD_OPTS);
        } else {
            const QEMUOption *popt;

            popt = lookup_opt(argc, argv, &optarg, &optind);
            #ifndef REMOTE_PROCESS
            if (!(popt->arch_mask & arch_type)) {
                error_report("Option not supported for this target,"
                             " %x arch_mask, %x arch_type",
                             popt->arch_mask, arch_type);
                exit(1);
            }
            #endif
            switch (popt->index) {
            case QEMU_OPTION_drive:
                if (drive_def(optarg) == NULL) {
                    fprintf(stderr, "Could not init drive\n");
                    exit(1);
                }
                break;
            case QEMU_OPTION_device:
                if (!qemu_opts_parse_noisily(qemu_find_opts("device"),
                                            optarg, true)) {
                    error_report("Unable to process device command");
                    exit(1);
                }
            break;
            case QEMU_OPTION_qmp:
                monitor_parse(optarg, "control", false);
                break;
            case QEMU_OPTION_monitor:
                if (!strncmp(optarg, "stdio", 5)) {
                    warn_report("STDIO not supported in remote process");
                } else if (strncmp(optarg, "none", 4)) {
                    monitor_parse(optarg, "readline", false);
                }
                break;
            default:
                break;
            }
        }
    }
    mc = MACHINE_GET_CLASS(current_machine);

    mc->block_default_type = IF_IDE;
    if (qemu_opts_foreach(qemu_find_opts("drive"), drive_init_func,
                          &mc->block_default_type, &error_fatal)) {
        /* We printed help */
        exit(0);
    }

    qemu_opts_foreach(qemu_find_opts("device"), device_init_func, NULL,
                      &error_fatal);

    return;
}
