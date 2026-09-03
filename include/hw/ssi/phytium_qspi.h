/*
 * Phytium E2000 QSPI controller
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SSI_PHYTIUM_QSPI_H
#define HW_SSI_PHYTIUM_QSPI_H

#include "hw/core/sysbus.h"

#define TYPE_PHYTIUM_E2000_QSPI "phytium-e2000-qspi"
OBJECT_DECLARE_SIMPLE_TYPE(PhytiumE2000QSPIState, PHYTIUM_E2000_QSPI)

#define PHYTIUM_E2000_QSPI_REG_SIZE    0x1000
#define PHYTIUM_E2000_QSPI_DIRECT_SIZE 0x10000000

#endif
