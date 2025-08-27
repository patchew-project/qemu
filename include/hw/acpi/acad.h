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
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#endif
