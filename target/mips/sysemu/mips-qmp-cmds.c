/*
 * QEMU MIPS CPU (monitor definitions)
 *
 * SPDX-FileCopyrightText: 2012 SUSE LINUX Products GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-machine-target.h"
#include "qapi/commands-target-compat.h"

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    return generic_query_cpu_definitions(errp);
}
