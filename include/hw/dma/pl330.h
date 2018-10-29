/*
 * ARM PrimeCell PL330 DMA Controller
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DMA_PL330_H
#define HW_DMA_PL330_H

#include "hw/sysbus.h"

#define TYPE_PL330 "pl330"

static inline void pl330_init(uint32_t base, qemu_irq irq, int nreq)
{
    SysBusDevice *busdev;
    DeviceState *dev;

    dev = qdev_create(NULL, TYPE_PL330);
    qdev_prop_set_uint8(dev, "num_chnls", 8);
    qdev_prop_set_uint8(dev, "num_periph_req", nreq);
    qdev_prop_set_uint8(dev, "num_events", 16);
    qdev_prop_set_uint8(dev, "data_width", 64);
    qdev_prop_set_uint8(dev, "wr_cap", 8);
    qdev_prop_set_uint8(dev, "wr_q_dep", 16);
    qdev_prop_set_uint8(dev, "rd_cap", 8);
    qdev_prop_set_uint8(dev, "rd_q_dep", 16);
    qdev_prop_set_uint16(dev, "data_buffer_dep", 256);
    qdev_init_nofail(dev);

    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, base);
    sysbus_connect_irq(busdev, 0, irq);
}

#endif /* HW_DMA_PL330_H */
