/*
 * QEMU emulated lid button device - QMP stubs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-acpi.h"

void qmp_lid_button_set_state(bool open, Error **errp)
{
    error_setg(errp, "No lid button device found");
}

LidButtonInfo *qmp_query_lid_button(Error **errp)
{
    error_setg(errp, "No lid button device found");
    return NULL;
}
