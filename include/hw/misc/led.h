/*
 * QEMU single LED device
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_LED_H
#define HW_MISC_LED_H

#include "hw/qdev-core.h"
#include "hw/sysbus.h" /* FIXME remove */

#define TYPE_LED "led"
#define LED(obj) OBJECT_CHECK(LEDState, (obj), TYPE_LED)

typedef struct LEDState {
    /* Private */
    SysBusDevice parent_obj; /* FIXME DeviceState */
    /* Public */

    qemu_irq irq;
    uint8_t current_state;

    /* Properties */
    char *name;
    uint8_t reset_state; /* TODO [GPIO_ACTIVE_LOW, GPIO_ACTIVE_HIGH] */
} LEDState;

#endif /* HW_MISC_LED_H */
