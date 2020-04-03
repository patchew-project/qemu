/*
 * NVDIMM utilities
 *
 * Copyright(C) 2015 Intel Corporation.
 *
 * Author:
 *  Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *
 * NFIT is defined in ACPI 6.0: 5.2.25 NVDIMM Firmware Interface Table (NFIT)
 * and the DSM specification can be found at:
 *       http://pmem.io/documents/NVDIMM_DSM_Interface_Example.pdf
 *
 * Currently, it only supports PMEM Virtualization.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu/nvdimm-utils.h"
#include "hw/mem/nvdimm.h"

static int nvdimm_device_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_NVDIMM)) {
        *list = g_slist_append(*list, DEVICE(obj));
    }

    object_child_foreach(obj, nvdimm_device_list, opaque);
    return 0;
}

/*
 * inquire NVDIMM devices and link them into the list which is
 * returned to the caller.
 *
 * Note: it is the caller's responsibility to free the list to avoid
 * memory leak.
 */
GSList *nvdimm_get_device_list(void)
{
    GSList *list = NULL;

    object_child_foreach(qdev_get_machine(), nvdimm_device_list, &list);
    return list;
}
