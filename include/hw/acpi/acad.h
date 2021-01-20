/*
 * QEMU emulated AC adapter device.
 *
 * Copyright (c) 2019 Janus Technologies, Inc. (http://janustech.com)
 *
 * Authors:
 *     Leonid Bloch <lb.workbox@gmail.com>
 *     Marcel Apfelbaum <marcel.apfelbaum@gmail.com>
 *     Dmitry Fleytman <dmitry.fleytman@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory for details.
 *
 */

#ifndef HW_ACPI_AC_ADAPTER_H
#define HW_ACPI_AC_ADAPTER_H

#define TYPE_AC_ADAPTER                  "acad"
#define AC_ADAPTER_IOPORT_PROP           "ioport"
#define AC_ADAPTER_PATH_PROP             "sysfs_path"
#define AC_ADAPTER_PROBE_STATE_INTERVAL  "probe_interval"

#define AC_ADAPTER_VAL_UNKNOWN  0xFFFFFFFF

#define AC_ADAPTER_LEN          1

static inline uint16_t acad_port(void)
{
    Object *o = object_resolve_path_type("", TYPE_AC_ADAPTER, NULL);
    if (!o) {
        return 0;
    }
    return object_property_get_uint(o, AC_ADAPTER_IOPORT_PROP, NULL);
}

#endif
