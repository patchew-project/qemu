/*
 * QEMU emulated button device.
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

#ifndef HW_ACPI_BUTTON_H
#define HW_ACPI_BUTTON_H

#define TYPE_BUTTON                  "button"
#define BUTTON_IOPORT_PROP           "ioport"
#define BUTTON_PATH_PROP             "procfs_path"
#define BUTTON_PROBE_STATE_INTERVAL  "probe_interval"

#define BUTTON_LEN                   1

static inline uint16_t button_port(void)
{
    Object *o = object_resolve_path_type("", TYPE_BUTTON, NULL);
    if (!o) {
        return 0;
    }
    return object_property_get_uint(o, BUTTON_IOPORT_PROP, NULL);
}

#endif
