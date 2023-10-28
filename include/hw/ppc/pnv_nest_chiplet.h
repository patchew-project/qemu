/*
 * QEMU PowerPC nest chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_PNV_NEST1_CHIPLET_H
#define PPC_PNV_NEST1_CHIPLET_H

#include "hw/ppc/pnv_pervasive.h"

#define TYPE_PNV_NEST1_CHIPLET "pnv-nest1-chiplet"
#define PNV_NEST1CHIPLET(obj) OBJECT_CHECK(PnvNest1Chiplet, (obj), TYPE_PNV_NEST1_CHIPLET)

typedef struct PnvNest1Chiplet {
    DeviceState parent;

    struct PnvChip *chip;

    /* common pervasive chiplet unit */
    PnvPervChiplet perv_chiplet;
} PnvNest1Chiplet;

#endif /*PPC_PNV_NEST1_CHIPLET_H */
