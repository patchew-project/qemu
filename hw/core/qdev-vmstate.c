/*
 * Device vmstate
 *
 * Copyright (c) 2019 GreenSocs
 *
 * Authors:
 *   Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "migration/vmstate.h"

const struct VMStateDescription device_vmstate_reset = {
    .name = "device_reset",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(resetting, DeviceState),
        VMSTATE_END_OF_LIST()
    }
};
