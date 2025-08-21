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
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef HW_ACPI_BUTTON_H
#define HW_ACPI_BUTTON_H

#define TYPE_BUTTON                  "button"
#define BUTTON_IOPORT_PROP           "ioport"
#define BUTTON_PATH_PROP             "procfs_path"
#define BUTTON_PROBE_STATE_INTERVAL  "probe_interval"

#define BUTTON_LEN                   1

#endif
