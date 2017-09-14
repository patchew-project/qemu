/*
 * 9p
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Gautham R Shenoy <ego@in.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu-fsdev.h"
#include "qemu/config-file.h"
#include "qemu/module.h"
#include "qmp-commands.h"

int qemu_fsdev_add(QemuOpts *opts)
{
    return 0;
}

void qmp_fsdev_set_io_throttle(ThrottleLimits *arg, Error **errp)
{
    return;
}

ThrottleLimitsList *qmp_query_fsdev_io_throttle(Error **errp)
{
    return NULL;
}
