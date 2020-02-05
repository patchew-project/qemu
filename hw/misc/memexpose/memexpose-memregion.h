/*
 *  Memexpose ARM device
 *
 *  Copyright (C) 2020 Samsung Electronics Co Ltd.
 *    Igor Kotrasinski, <i.kotrasinsk@partner.samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _MEMEXPOSE_MEMDEV_H_
#define _MEMEXPOSE_MEMDEV_H_

#include "memexpose-core.h"
#include "hw/sysbus.h"

#define TYPE_MEMEXPOSE_MEMDEV "memexpose-memdev"
#define MEMEXPOSE_MEMDEV(obj) \
    OBJECT_CHECK(MemexposeMemdev, (obj), TYPE_MEMEXPOSE_MEMDEV)

typedef struct MemexposeMemdev {
    SysBusDevice dev;
    MemexposeIntr intr;
    MemexposeMem mem;
    CharBackend intr_chr;
    CharBackend mem_chr;
    qemu_irq irq;
} MemexposeMemdev;

#endif /* _MEMEXPOSE_MEMDEV_H_ */
