/*
 * QEMU single Latching Switch device
 *
 * Copyright (C) Jian Zhang <zhangjian.3032@bytedance.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_LATCHING_SWITCH_H
#define HW_MISC_LATCHING_SWITCH_H

#include "qom/object.h"
#include "hw/qdev-core.h"

#define TYPE_LATCHING_SWITCH "latching-switch"

enum TriggerEdge {
    TRIGGER_EDGE_FALLING,
    TRIGGER_EDGE_RISING,
    TRIGGER_EDGE_BOTH,
};

struct LatchingSwitchState {
    /* Private */
    DeviceState parent_obj;

    /* Public */
    qemu_irq input;
    qemu_irq output;

    /* Properties */
    bool state;
    uint8_t trigger_edge;
    char *description;
};
typedef struct LatchingSwitchState LatchingSwitchState;
DECLARE_INSTANCE_CHECKER(LatchingSwitchState, LATCHING_SWITCH,
                         TYPE_LATCHING_SWITCH)

/**
 * latching_switch_create_simple: Create and realize a  device
 * @parentobj: the parent object
 * @state: the initial state of the switch
 * @trigger_edge: the trigger edge of the switch
 *                0: falling edge
 *                1: rising edge
 *                2: both edge
 *
 * Create the device state structure, initialize it, and
 * drop the reference to it (the device is realized).
 *
 */
LatchingSwitchState *latching_switch_create_simple(Object *parentobj,
                                                   bool state,
                                                   uint8_t trigger_edge);

#endif /* HW_MISC_LATCHING_SWITCH_H */
