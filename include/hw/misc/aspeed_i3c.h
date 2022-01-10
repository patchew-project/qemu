/*
 * ASPEED I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_I3C_H
#define ASPEED_I3C_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_I3C "aspeed.i3c"
#define TYPE_ASPEED_I3C_DEVICE "aspeed.i3c.device"
OBJECT_DECLARE_TYPE(AspeedI3CState, AspeedI3CClass, ASPEED_I3C)

#define ASPEED_I3C_NR_REGS (0x8000 >> 2)
#define ASPEED_I3C_DEVICE_NR_REGS (0x1000 >> 2)
#define ASPEED_I3C_NR_DEVICES 6

OBJECT_DECLARE_SIMPLE_TYPE(AspeedI3CDevice, ASPEED_I3C_DEVICE)
typedef struct AspeedI3CDevice {
    /* <private> */
    SysBusDevice parent;
    struct AspeedI3CState *controller;

    /* <public> */
    MemoryRegion mr;
    qemu_irq irq;

    uint8_t id;
    uint32_t regs[ASPEED_I3C_DEVICE_NR_REGS];
} AspeedI3CDevice;

typedef struct AspeedI3CClass {
    SysBusDeviceClass parent_class;

    uint8_t num_devices;
    uint8_t reg_size;

    qemu_irq (*bus_get_irq)(AspeedI3CDevice *);
} AspeedI3CClass;

typedef struct AspeedI3CState {
    /* <private> */
    SysBusDevice parent;

    /* <public> */
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[ASPEED_I3C_NR_REGS];
    AspeedI3CDevice devices[ASPEED_I3C_NR_DEVICES];
} AspeedI3CState;
#endif /* ASPEED_I3C_H */
