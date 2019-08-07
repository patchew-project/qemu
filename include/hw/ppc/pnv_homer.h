/*
 * QEMU PowerPC PowerNV Homer and occ common area definitions
 *
 * Copyright (c) 2019, IBM Corporation.
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
#ifndef _PPC_PNV_HOMER_H
#define _PPC_PNV_HOMER_H

#include "qom/object.h"

/*
 *  HOMER region size 4M per OCC (1 OCC is defined per chip  in struct PnvChip)
 *  so chip_num can be used to offset between HOMER region from its base address
 */
#define PNV_HOMER_SIZE        0x300000
#define PNV_OCC_COMMON_AREA_SIZE      0x700000

#define PNV_HOMER_BASE(chip)                                            \
    (0x7ffd800000ull + ((uint64_t)(chip)->chip_num) * PNV_HOMER_SIZE)
#define PNV_OCC_COMMON_AREA(chip)                                       \
    (0x7fff800000ull + ((uint64_t)(chip)->chip_num) * PNV_OCC_COMMON_AREA_SIZE)

#define PNV9_HOMER_BASE(chip)                                            \
    (0x203ffd800000ull + ((uint64_t)(chip)->chip_num) * PNV_HOMER_SIZE)
#define PNV9_OCC_COMMON_AREA(chip)                                       \
    (0x203fff800000ull + ((uint64_t)(chip)->chip_num) * PNV_OCC_COMMON_AREA_SIZE)

#endif /* _PPC_PNV_HOMER_H */
