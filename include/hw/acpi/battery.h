/*
 * QEMU emulated battery device.
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

#ifndef HW_ACPI_BATTERY_H
#define HW_ACPI_BATTERY_H

#define TYPE_BATTERY                  "battery"
#define BATTERY_IOPORT_PROP           "ioport"
#define BATTERY_PATH_PROP             "sysfs_path"
#define BATTERY_PROBE_STATE_INTERVAL  "probe_interval"

#define BATTERY_FULL_CAP     10000  /* mWh */

#define BATTERY_CAPACITY_OF_WARNING   (BATTERY_FULL_CAP /  10)  /* 10% */
#define BATTERY_CAPACITY_OF_LOW       (BATTERY_FULL_CAP /  25)  /* 4%  */
#define BATTERY_CAPACITY_GRANULARITY  (BATTERY_FULL_CAP / 100)  /* 1%  */

#define BATTERY_VAL_UNKNOWN  0xFFFFFFFF

#define BATTERY_LEN          0x0C

#endif
