/*
 * QEMU simulated pvpanic device.
 *
 * Copyright Fujitsu, Corp. 2013
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *     Hu Tao <hutao@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef HW_MISC_PVPANIC_H
#define HW_MISC_PVPANIC_H
#include "hw/isa/isa.h"

#define TYPE_PVPANIC "pvpanic"
#define TYPE_PVPANIC_MMIO "pvpanic-mmio"

#define PVPANIC_IOPORT_PROP "ioport"

/* PVPanicISAState for ISA device and
 * use ioport.
 */
typedef struct PVPanicISAState {
    ISADevice parent_obj;
    /*< private>*/
    uint16_t ioport;
    /*<public>*/
    MemoryRegion mr;
} PVPanicISAState;

/* PVPanicMMIOState for sysbus device and
 * use mmio.
 */
typedef struct PVPanicMMIOState {
    SysBusDevice parent_obj;
    /*<private>*/

    /* public */
    MemoryRegion mr;
} PVPanicMMIOState;

#define PVPANIC_ISA_DEVICE(obj)    \
    OBJECT_CHECK(PVPanicISAState, (obj), TYPE_PVPANIC)

#define PVPANIC_MMIO_DEVICE(obj)    \
    OBJECT_CHECK(PVPanicMMIOState, (obj), TYPE_PVPANIC_MMIO)

static inline uint16_t pvpanic_port(void)
{
    Object *o = object_resolve_path_type("", TYPE_PVPANIC, NULL);
    if (!o) {
        return 0;
    }
    return object_property_get_uint(o, PVPANIC_IOPORT_PROP, NULL);
}

#endif
