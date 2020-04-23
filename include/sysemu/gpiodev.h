/*
 * QEMU GPIO Backend
 *
 * Copyright (C) 2018-2020 Glider bv
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/typedefs.h"

void qemu_gpiodev_add(DeviceState *dev, const char *name, unsigned int maxgpio,
                      Error **errp);
