/*
 * ASPEED Serial GPIO Controller
 *
 * Copyright 2025 Google LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_SGPIO_H
#define ASPEED_SGPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ASPEED_SGPIO "aspeed.sgpio"
OBJECT_DECLARE_TYPE(AspeedSGPIOState, AspeedSGPIOClass, ASPEED_SGPIO)

#define ASPEED_SGPIO_MAX_PIN_PAIR 256
#define ASPEED_SGPIO_MAX_INT 8

struct AspeedSGPIOClass {
    SysBusDevice parent_obj;
    uint32_t nr_sgpio_pin_pairs;
    uint64_t mem_size;
    const MemoryRegionOps *reg_ops;
};

struct AspeedSGPIOState {
  /* <private> */
  SysBusDevice parent;

  /*< public >*/
  MemoryRegion iomem;
  qemu_irq irq;
  uint32_t ctrl_regs[ASPEED_SGPIO_MAX_PIN_PAIR];
  uint32_t int_regs[ASPEED_SGPIO_MAX_INT];
};

#endif /* ASPEED_SGPIO_H */
