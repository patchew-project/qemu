/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * qdev internal helpers
 *
 * Copyright (c) 2009-2021 QEMU contributors
 */
#ifndef HW_CORE_QDEV_INTERNAL_H
#define HW_CORE_QDEV_INTERNAL_H

#include "hw/qdev-core.h"

/* Following functions are only used by the platform-bus subsystem */
qemu_irq qdev_get_gpio_out_connector(DeviceState *dev, const char *name, int n);

#endif /* HW_CORE_QDEV_INTERNAL_H */
