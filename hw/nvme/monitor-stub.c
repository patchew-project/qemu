/*
 * QEMU NVMe Controller monitor command stubs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"

HumanReadableText *qmp_x_query_nvme(Error **errp)
{
    error_setg(errp, "NVMe support is not compiled in");
    return NULL;
}
