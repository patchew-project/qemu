/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SysBus internal helpers
 *
 * Copyright (c) 2021 QEMU contributors
 */
#ifndef HW_CORE_SYSBUS_INTERNAL_H
#define HW_CORE_SYSBUS_INTERNAL_H

#include "hw/sysbus.h"

/* Following functions are only used by the platform-bus subsystem */
qemu_irq sysbus_get_connected_irq(SysBusDevice *dev, int n);
bool sysbus_is_irq_connected(SysBusDevice *dev, int n);

#endif /* HW_CORE_SYSBUS_INTERNAL_H */
