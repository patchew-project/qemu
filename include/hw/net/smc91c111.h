/*
 * SMSC 91C111 Ethernet interface emulation
 *
 * Copyright (c) 2005 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL
 */

#ifndef HW_NET_SMC91C111_H
#define HW_NET_SMC91C111_H

#include "hw/qdev.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "net/net.h"

#define TYPE_SMC91C111 "smc91c111"

/*
 * Legacy helper function.  Should go away when machine config files are
 * implemented.
 */
static inline DeviceState *smc91c111_init(NICInfo *nd,
                                          uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    qemu_check_nic_model(nd, "smc91c111");
    dev = qdev_create(NULL, TYPE_SMC91C111);
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);

    return dev;
}

#endif
