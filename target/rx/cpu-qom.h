/*
 * QEMU RX CPU
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_RX_CPU_QOM_H
#define QEMU_RX_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_RXCPU "rxcpu"

#define RXCPU_CLASS(klass)                                     \
    OBJECT_CLASS_CHECK(RXCPUClass, (klass), TYPE_RXCPU)
#define RXCPU(obj) \
    OBJECT_CHECK(RXCPU, (obj), TYPE_RXCPU)
#define RXCPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RXCPUClass, (obj), TYPE_RXCPU)

/*
 * RXCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A RX CPU model.
 */
typedef struct RXCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);

} RXCPUClass;

typedef struct RXCPU RXCPU;

#endif
