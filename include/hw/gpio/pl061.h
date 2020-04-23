/*
 * Arm PrimeCell PL061 General Purpose IO with additional Luminary Micro
 * Stellaris bits.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#ifndef PL061_GPIO_H
#define PL061_GPIO_H

#include <stdint.h>

#include "exec/hwaddr.h"

#define TYPE_PL061 "pl061"

uint32_t pl061_create_fdt(void *fdt, const char *parent, unsigned int n_cells,
                          hwaddr addr, hwaddr size, int irq, uint32_t clock);

#endif /* PL061_GPIO_H */
