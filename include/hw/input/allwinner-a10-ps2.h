/*
 * Allwinner A10 PS2 Module emulation
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

#ifndef HW_INPUT_ALLWINNER_A10_PS2_H
#define HW_INPUT_ALLWINNER_A10_PS2_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/input/ps2.h"

/**
 * @name Constants
 * @{
 */

/** Size of register I/O address space used by CCM device */
#define AW_A10_PS2_IOSIZE        (0x400)

/** Total number of known registers */
#define AW_A10_PS2_REGS_NUM      (AW_A10_PS2_IOSIZE / sizeof(uint32_t))

/** @} */

struct AwA10PS2DeviceClass {
    SysBusDeviceClass parent_class;

    DeviceRealize parent_realize;
};

/**
 * @name Object model
 * @{
 */

#define TYPE_AW_A10_PS2    "allwinner-a10-ps2"
OBJECT_DECLARE_TYPE(AwA10PS2State, AwA10PS2DeviceClass, AW_A10_PS2)

/** @} */

/**
 * Allwinner A10 PS2 object instance state.
 */
struct AwA10PS2State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    PS2State *ps2dev;

    /** Array of hardware registers */
    uint32_t regs[AW_A10_PS2_REGS_NUM];
    int pending;
    uint32_t last;
    qemu_irq irq;
    bool is_mouse;
};

#define TYPE_AW_A10_PS2_KBD_DEVICE "allwinner-a10-ps2-keyboard"
OBJECT_DECLARE_SIMPLE_TYPE(AwA10PS2KbdState, AW_A10_PS2_KBD_DEVICE)

struct AwA10PS2KbdState {
    AwA10PS2State parent_obj;

    PS2KbdState kbd;
};

#define TYPE_AW_A10_PS2_MOUSE_DEVICE "allwinner-a10-ps2-mouse"
OBJECT_DECLARE_SIMPLE_TYPE(AwA10PS2MouseState, AW_A10_PS2_MOUSE_DEVICE)

struct AwA10PS2MouseState {
    AwA10PS2State parent_obj;

    PS2MouseState mouse;
};


#endif /* HW_INPUT_ALLWINNER_A10_PS2_H */
