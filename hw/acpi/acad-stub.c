/*
 * QEMU emulated AC adapter device - QMP stubs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-acpi.h"

void qmp_ac_adapter_set_state(bool connected, Error **errp)
{
    error_setg(errp, "No AC adapter device found");
}

AcAdapterInfo *qmp_query_ac_adapter(Error **errp)
{
    error_setg(errp, "No AC adapter device found");
    return NULL;
}
