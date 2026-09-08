/*
 * Phytium E2000 random number generator
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_PHYTIUM_E2000_RNG_H
#define HW_MISC_PHYTIUM_E2000_RNG_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_PHYTIUM_E2000_RNG "phytium-e2000-rng"
OBJECT_DECLARE_SIMPLE_TYPE(PhytiumE2000RNGState, PHYTIUM_E2000_RNG)

#define PHYTIUM_E2000_RNG_MMIO_SIZE 0x1000

#endif
