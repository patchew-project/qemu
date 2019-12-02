/*
 * Allwinner H3 Clock Control Unit emulation
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

#ifndef HW_MISC_ALLWINNER_H3_CLK_H
#define HW_MISC_ALLWINNER_H3_CLK_H

#include "hw/sysbus.h"

#define AW_H3_CLK_REGS_MAX_ADDR (0x304)
#define AW_H3_CLK_REGS_NUM      (AW_H3_CLK_REGS_MAX_ADDR / sizeof(uint32_t))
#define AW_H3_CLK_REGS_MEM_SIZE (1024)

#define TYPE_AW_H3_CLK    "allwinner-h3-clk"
#define AW_H3_CLK(obj)    OBJECT_CHECK(AwH3ClockState, (obj), TYPE_AW_H3_CLK)

typedef struct AwH3ClockState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t regs[AW_H3_CLK_REGS_NUM];
} AwH3ClockState;

#endif
