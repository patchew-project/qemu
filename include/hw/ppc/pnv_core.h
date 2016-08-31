/*
 * QEMU PowerPC PowerNV CPU Core model
 *
 * Copyright (c) 2016 IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _PPC_PNV_CORE_H
#define _PPC_PNV_CORE_H

#include "hw/cpu/core.h"
#include "hw/ppc/pnv_xscom.h"

#define TYPE_PNV_CORE_XSCOM "powernv-cpu-core-xscom"
#define PNV_CORE_XSCOM(obj) \
     OBJECT_CHECK(PnvCoreXScom, (obj), TYPE_PNV_CORE_XSCOM)

typedef struct PnvCoreXScom {
    XScomDevice xd;
    uint32_t core_pir;
} PnvCoreXScom;

#define P8_PIR2COREID(pir) (((pir) >> 3) & 0xf)

#define TYPE_PNV_CORE "powernv-cpu-core"
#define PNV_CORE(obj) \
    OBJECT_CHECK(PnvCore, (obj), TYPE_PNV_CORE)
#define PNV_CORE_CLASS(klass) \
     OBJECT_CLASS_CHECK(PnvCoreClass, (klass), TYPE_PNV_CORE)
#define PNV_CORE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvCoreClass, (obj), TYPE_PNV_CORE)

typedef struct PnvCore {
    /*< private >*/
    CPUCore parent_obj;

    /*< public >*/
    void *threads;
    PnvCoreXScom *xd;
} PnvCore;

typedef struct PnvCoreClass {
    DeviceClass parent_class;
    ObjectClass *cpu_oc;
} PnvCoreClass;

extern char *pnv_core_typename(const char *model);

#endif /* _PPC_PNV_CORE_H */
