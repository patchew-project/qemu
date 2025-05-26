/*
 * QEMU PowerPC PowerNV stubs
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/ppc/pnv.h"

PnvCore *pnv_chip_find_core(PnvChip *chip, uint32_t core_id)
{
    g_assert_not_reached();
}
