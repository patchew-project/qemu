/*
 * QEMU emulated battery device - QMP stubs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-acpi.h"

void qmp_battery_set_state(BatteryInfo *state, Error **errp)
{
    error_setg(errp, "No battery device found");
}

BatteryInfo *qmp_query_battery(Error **errp)
{
    error_setg(errp, "No battery device found");
    return NULL;
}
