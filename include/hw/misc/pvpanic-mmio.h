/*
 * pvpanic mmio device emulation
 *
 * Copyright (c) 2018 ZTE Ltd.
 *
 * Author:
 *  Peng Hao <peng.hao2@zte.com.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_MISC_PVPANIC_H
#define HW_MISC_PVPANIC_H
#include "hw/sysbus.h"
#define TYPE_PVPANIC_MMIO "pvpanic-mmio"
#define PVPANIC_MMIO_DEVICE(obj)    \
    OBJECT_CHECK(PVPanicState, (obj), TYPE_PVPANIC_MMIO)
#endif

typedef struct PVPanicState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
} PVPanicState;
