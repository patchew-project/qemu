/*
 * Allwinner H3 CPU Configuration Module emulation
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

#ifndef HW_MISC_ALLWINNER_H3_CPUCFG_H
#define HW_MISC_ALLWINNER_H3_CPUCFG_H

#include "hw/sysbus.h"

#define AW_H3_CPUCFG_REGS_MEM_SIZE  (1024)

#define TYPE_AW_H3_CPUCFG   "allwinner-h3-cpucfg"
#define AW_H3_CPUCFG(obj)   OBJECT_CHECK(AwH3CpuCfgState, (obj), \
                                         TYPE_AW_H3_CPUCFG)

typedef struct AwH3CpuCfgState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t gen_ctrl;
    uint32_t super_standby;
    uint32_t entry_addr;
    uint32_t counter_ctrl;

} AwH3CpuCfgState;

#endif
