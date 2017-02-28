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

#include "qemu/osdep.h"
#include "qemu/qemu-clock.h"
#include "hw/hw.h"
#include "qemu/log.h"

#ifndef DEBUG_QEMU_CLOCK
#define DEBUG_QEMU_CLOCK 0
#endif

#define DPRINTF(fmt, args...) do {                                           \
    if (DEBUG_QEMU_CLOCK) {                                                  \
        qemu_log("%s: " fmt, __func__, ## args);                             \
    }                                                                        \
} while (0);

static const TypeInfo qemu_clk_info = {
    .name          = TYPE_CLOCK,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(QEMUClock),
};

static void qemu_clk_register_types(void)
{
    type_register_static(&qemu_clk_info);
}

type_init(qemu_clk_register_types);
