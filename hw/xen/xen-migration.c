/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "exec/memory.h"
#include "qapi/qapi-commands-migration.h"

void qmp_xen_set_global_dirty_log(bool enable, Error **errp)
{
    if (enable) {
        memory_global_dirty_log_start();
    } else {
        memory_global_dirty_log_stop();
    }
}
