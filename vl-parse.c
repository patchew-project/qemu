/*
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "monitor/qdev.h"
#include "monitor/qdev.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "qemu/option.h"
#include "qemu-options.h"
#include "sysemu/blockdev.h"

#include "chardev/char.h"
#include "monitor/monitor.h"
#include "qemu/config-file.h"

#include "sysemu/arch_init.h"

#include "vl.h"

int only_migratable; /* turn it off unless user states otherwise */
bool enable_mlock;

/***********************************************************/
/* QEMU Block devices */

static const QEMUOption qemu_options[] = {
    { "h", 0, QEMU_OPTION_h, QEMU_ARCH_ALL },
#define QEMU_OPTIONS_GENERATE_OPTIONS
#include "qemu-options-wrapper.h"
    { NULL },
};

const QEMUOption *lookup_opt(int argc, char **argv,
                                    const char **poptarg, int *poptind)
{
    const QEMUOption *popt;
    int optind = *poptind;
    char *r = argv[optind];
    const char *optarg;

    loc_set_cmdline(argv, optind, 1);
    optind++;
    /* Treat --foo the same as -foo.  */
    if (r[1] == '-') {
        r++;
    }
    popt = qemu_options;
    if (!popt) {
        error_report("No valide qemu_options");
    }
    for (;;) {
        if (!popt->name) {
            error_report("invalid option*");
            exit(1);
            popt++;
            continue;
        }
        if (!strcmp(popt->name, r + 1)) {
            break;
        }
        popt++;
    }
    if (popt->flags & HAS_ARG) {
        if (optind >= argc) {
            error_report("optind %d, argc %d", optind, argc);
            error_report("requires an argument");
            exit(1);
        }
        optarg = argv[optind++];
        loc_set_cmdline(argv, optind - 2, 2);
    } else {
        optarg = NULL;
    }

    *poptarg = optarg;
    *poptind = optind;

    return popt;
}

int drive_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    BlockInterfaceType *block_default_type = opaque;

    if (!drive_new(opts, *block_default_type, errp)) {
        error_report_err(*errp);
    }

    return 0;
}

int device_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    DeviceState *dev;
    const char *remote = NULL;

    remote = qemu_opt_get(opts, "rid");
    if (remote) {
        return 0;
    }

    dev = qdev_device_add(opts, errp);
    if (!dev) {
        return -1;
    }
    object_unref(OBJECT(dev));
    return 0;
}

#if defined(CONFIG_MPQEMU)
int rdrive_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    DeviceState *dev;

    dev = qdev_remote_add(opts, false /* this is drive */, errp);
    if (!dev) {
        error_setg(errp, "qdev_remote_add failed for drive.");
        return -1;
    }
    object_unref(OBJECT(dev));
    return 0;
}
#endif

#if defined(CONFIG_MPQEMU)
int rdevice_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    DeviceState *dev;

    dev = qdev_remote_add(opts, true /* this is device */, errp);
    if (!dev) {
        error_setg(errp, "qdev_remote_add failed for device.");
        return -1;
    }
    return 0;
}
#endif

