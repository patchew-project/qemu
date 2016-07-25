/*
 * QEMU PowerNV various definitions
 *
 * Copyright (c) 2014-2016 BenH, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _PPC_PNV_H
#define _PPC_PNV_H

#include "hw/hw.h"

/* Should we turn that into a QOjb of some sort ? */
typedef struct PnvChip {
    uint32_t         chip_id;
} PnvChip;

typedef struct PnvSystem {
    uint32_t  num_chips;
#define PNV_MAX_CHIPS      1
    PnvChip   chips[PNV_MAX_CHIPS];
} PnvSystem;

#endif /* _PPC_PNV_H */
