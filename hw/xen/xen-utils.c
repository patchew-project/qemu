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
#include "sysemu/runstate.h"
#include "hw/xen/xen_common.h"

void xen_shutdown_fatal_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "Will destroy the domain.\n");
    /* destroy the domain */
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_ERROR);
}
