/*
 * Allwinner A10 LCD Control Module emulation
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

#ifndef HW_DISPLAY_ALLWINNER_A10_LCDC_H
#define HW_DISPLAY_ALLWINNER_A10_LCDC_H

#include "qom/object.h"
#include "hw/ptimer.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "hw/display/allwinner-a10-debe.h"

/**
 * @name Constants
 * @{
 */

/** Size of register I/O address space used by LCDC device */
#define AW_A10_LCDC_IOSIZE        (0x1000)

/** Total number of known registers */
#define AW_A10_LCDC_REGS_NUM      (AW_A10_LCDC_IOSIZE / sizeof(uint32_t))

/** @} */

/**
 * @name Object model
 * @{
 */

#define TYPE_AW_A10_LCDC    "allwinner-a10-lcdc"
OBJECT_DECLARE_SIMPLE_TYPE(AwA10LcdcState, AW_A10_LCDC)

/** @} */

/**
 * Allwinner A10 LCDC object instance state.
 */
struct AwA10LcdcState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    qemu_irq irq;
    struct ptimer_state *timer;
    QemuConsole *con;

    MemoryRegionSection fbsection;

    int invalidate;
    bool is_enabled;

    AwA10DEBEState *debe;

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** Array of hardware registers */
    uint32_t regs[AW_A10_LCDC_REGS_NUM];
};

#endif /* HW_DISPLAY_ALLWINNER_A10_LCDC_H */
