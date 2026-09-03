/*
 * Phytium E2000 DDR training status
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_PHYTIUM_E2000_DDR_H
#define HW_MISC_PHYTIUM_E2000_DDR_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_PHYTIUM_E2000_DDR "phytium-e2000-ddr"
OBJECT_DECLARE_SIMPLE_TYPE(PhytiumE2000DDRState, PHYTIUM_E2000_DDR)

#define PHYTIUM_E2000_DDR_MMIO_SIZE 0x400

#endif
