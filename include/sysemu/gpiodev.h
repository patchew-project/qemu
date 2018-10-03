/*
 * QEMU GPIO Backend
 *
 * Copyright (C) 2018 Glider bvba
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

extern DeviceState *the_pl061_dev;

int qemu_gpiodev_add(QemuOpts *opts);
