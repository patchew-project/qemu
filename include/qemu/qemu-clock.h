/*
 * QEMU Clock
 *
 *  Copyright (C) 2016 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Frederic Konrad <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QEMU_CLOCK_H
#define QEMU_CLOCK_H

#include "qemu/osdep.h"
#include "qom/object.h"

#define TYPE_CLOCK "qemu-clk"
#define QEMU_CLOCK(obj) OBJECT_CHECK(QEMUClock, (obj), TYPE_CLOCK)

typedef struct QEMUClock {
    /*< private >*/
    Object parent_obj;
} QEMUClock;

#endif /* QEMU_CLOCK_H */


