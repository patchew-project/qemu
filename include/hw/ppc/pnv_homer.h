/*
 * QEMU PowerPC PowerNV Emulation of a few HOMER related registers
 *
 * Copyright (c) 2019, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_PNV_HOMER_H
#define PPC_PNV_HOMER_H

#define TYPE_PNV_HOMER "pnv-homer"
#define PNV_HOMER(obj) OBJECT_CHECK(PnvHOMER, (obj), TYPE_PNV_HOMER)
#define TYPE_PNV8_HOMER TYPE_PNV_HOMER "-POWER8"
#define PNV8_HOMER(obj) OBJECT_CHECK(PnvHOMER, (obj), TYPE_PNV8_HOMER)
#define TYPE_PNV9_HOMER TYPE_PNV_HOMER "-POWER9"
#define PNV9_HOMER(obj) OBJECT_CHECK(PnvHOMER, (obj), TYPE_PNV9_HOMER)

typedef struct PnvHOMER {
    DeviceState parent;

    MemoryRegion homer_regs;
} PnvHOMER;

#define PNV_HOMER_CLASS(klass)   \
     OBJECT_CLASS_CHECK(PnvHOMERClass, (klass), TYPE_PNV_HOMER)
#define PNV_HOMER_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvHOMERClass, (obj), TYPE_PNV_HOMER)

typedef struct PnvHOMERClass {
    DeviceClass parent_class;

    int homer_size;
    const MemoryRegionOps *homer_ops;

    hwaddr core_max_base;
} PnvHOMERClass;



#endif /* PPC_PNV_HOMER_H */
