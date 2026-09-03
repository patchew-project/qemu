/*
 * Phytium E2000 SD/MMC controller
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SD_PHYTIUM_E2000_MCI_H
#define HW_SD_PHYTIUM_E2000_MCI_H

#include "hw/sd/dw_mci.h"
#include "qom/object.h"

#define TYPE_PHYTIUM_E2000_MCI "phytium-e2000-mci"
OBJECT_DECLARE_SIMPLE_TYPE(PhytiumE2000MciState, PHYTIUM_E2000_MCI)

#endif
