/*
 * Allwinner H3 Security ID emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_MISC_ALLWINNER_H3_SID_H
#define HW_MISC_ALLWINNER_H3_SID_H

#include "hw/sysbus.h"

#define AW_H3_SID_NUM_IDS        (4)
#define AW_H3_SID_REGS_MEM_SIZE  (1024)

#define TYPE_AW_H3_SID    "allwinner-h3-sid"
#define AW_H3_SID(obj)    OBJECT_CHECK(AwH3SidState, (obj), TYPE_AW_H3_SID)

typedef struct AwH3SidState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t control;
    uint32_t rdkey;
    uint32_t identifier[AW_H3_SID_NUM_IDS];
} AwH3SidState;

#endif
