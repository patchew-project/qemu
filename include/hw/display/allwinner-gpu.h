/*
 * Allwinner A10 GPU Module emulation
 *
 * Copyright (C) 2023 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
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

#ifndef HW_DISPLAY_ALLWINNER_GPU_H
#define HW_DISPLAY_ALLWINNER_GPU_H

#include "qom/object.h"
#include "hw/sysbus.h"

/**
 * @name Constants
 * @{
 */

/** Size of register I/O address space used by GPU device */
#define AW_GPU_IOSIZE        (0x10000)

/** Total number of known registers */
#define AW_GPU_REGS_NUM      (AW_GPU_IOSIZE / sizeof(uint32_t))

/** @} */

/**
 * @name Object model
 * @{
 */

#define TYPE_AW_GPU    "allwinner-gpu"
OBJECT_DECLARE_SIMPLE_TYPE(AwGpuState, AW_GPU)

/** @} */

/**
 * Allwinner GPU object instance state.
 */
struct AwGpuState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** Array of hardware registers */
    uint32_t regs[AW_GPU_REGS_NUM];
};

#endif /* HW_DISPLAY_ALLWINNER_GPU_H */
