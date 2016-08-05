/*
 * QEMU PowerPC PowerNV CPU model
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

#define TYPE_POWERNV_CPU_CORE "powernv-cpu-core"
#define POWERNV_CPU_CORE(obj) \
    OBJECT_CHECK(PowerNVCPUCore, (obj), TYPE_POWERNV_CPU_CORE)
#define POWERNV_CPU_CLASS(klass) \
     OBJECT_CLASS_CHECK(PowerNVCPUClass, (klass), TYPE_POWERNV_CPU_CORE)
#define POWERNV_CPU_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PowerNVCPUClass, (obj), TYPE_POWERNV_CPU_CORE)

typedef struct PowerNVCPUCore {
    /*< private >*/
    CPUCore parent_obj;

    /*< public >*/
    void *threads;
} PowerNVCPUCore;

typedef struct PowerNVCPUClass {
    DeviceClass parent_class;
    ObjectClass *cpu_oc;
}   PowerNVCPUClass;

extern char *powernv_cpu_core_typename(const char *model);

#endif /* _PPC_PNV_CORE_H */
