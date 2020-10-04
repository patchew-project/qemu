/*
 * AVR processors GPIO registers definition.
 *
 * Copyright (C) 2020 Heecheol Yang <heecheol.yang@outlook.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AVR_GPIO_H
#define AVR_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* Offsets of registers. */
#define GPIO_PIN   0x00
#define GPIO_DDR   0x01
#define GPIO_PORT  0x02

#define TYPE_AVR_GPIO "avr-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(AVRGPIOState, AVR_GPIO)

struct AVRGPIOState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    uint8_t ddr_val;
    uint8_t port_val;

};

#endif /* AVR_GPIO_H */
