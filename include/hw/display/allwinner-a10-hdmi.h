/*
 * Allwinner A10 HDMI Module emulation
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

#ifndef HW_DISPLAY_ALLWINNER_A10_HDMI_H
#define HW_DISPLAY_ALLWINNER_A10_HDMI_H

#include "hw/display/edid.h"
#include "qom/object.h"
#include "hw/sysbus.h"

/**
 * @name Constants
 * @{
 */

/** Size of register I/O address space used by HDMI device */
#define AW_A10_HDMI_IOSIZE        (0x1000)

/** Total number of known registers */
#define AW_A10_HDMI_REGS_NUM      (AW_A10_HDMI_IOSIZE / sizeof(uint32_t))

/** @} */

/**
 * @name Object model
 * @{
 */

#define TYPE_AW_A10_HDMI    "allwinner-a10-hdmi"
OBJECT_DECLARE_SIMPLE_TYPE(AwA10HdmiState, AW_A10_HDMI)

/** @} */

/**
 * Allwinner A10 HDMI object instance state.
 */
struct AwA10HdmiState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    uint8_t edid_reg;
    qemu_edid_info edid_info;
    uint8_t edid_blob[128];

    /** Array of hardware registers */
    uint32_t regs[AW_A10_HDMI_REGS_NUM];
};

#endif /* HW_DISPLAY_ALLWINNER_A10_HDMI_H */
