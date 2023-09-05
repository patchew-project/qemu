/*
 * Allwinner A10 Display engine Backend emulation
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

#ifndef HW_DISPLAY_ALLWINNER_A10_DEBE_H
#define HW_DISPLAY_ALLWINNER_A10_DEBE_H

#include "qom/object.h"
#include "hw/sysbus.h"

/**
 * @name Constants
 * @{
 */

/** Size of register I/O address space used by DEBE device */
#define AW_A10_DEBE_IOSIZE      (0x20000)

/** Total number of known registers for DEBE */
#define AW_A10_DEBE_REGS_NUM    (AW_A10_DEBE_IOSIZE / sizeof(uint32_t))

/** @} */

/**
 * @name Object model
 * @{
 */

#define TYPE_AW_A10_DEBE    "allwinner-a10-debe"
OBJECT_DECLARE_SIMPLE_TYPE(AwA10DEBEState, AW_A10_DEBE)

/** @} */

/**
 * Allwinner A10 DEBE object instance state.
 */
struct AwA10DEBEState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    uint32_t width;
    uint32_t height;
    hwaddr framebuffer_offset;
    hwaddr ram_base;
    uint8_t bpp;
    bool ready;
    bool invalidate;

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** Array of hardware registers */
    uint32_t regs[AW_A10_DEBE_REGS_NUM];
};

#endif /* HW_DISPLAY_ALLWINNER_A10_DEBE_H */
