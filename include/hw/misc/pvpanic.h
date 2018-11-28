/*
 * QEMU simulated pvpanic device.
 *
 * Copyright Fujitsu, Corp. 2013
 * Copyright ZTE Ltd. 2018
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *     Hu Tao <hutao@cn.fujitsu.com>
 *     Peng Hao <peng.hao2@zte.com.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef HW_MISC_PVPANIC_H
#define HW_MISC_PVPANIC_H

#define TYPE_PVPANIC "pvpanic"
#define TYPE_PVPANIC_MMIO "pvpanic-mmio"

#define PVPANIC_IOPORT_PROP "ioport"

static inline uint16_t pvpanic_port(void)
{
    Object *o = object_resolve_path_type("", TYPE_PVPANIC, NULL);
    if (!o) {
        return 0;
    }
    return object_property_get_uint(o, PVPANIC_IOPORT_PROP, NULL);
}

static inline Object *pvpanic_mmio(void)
{
    return object_resolve_path_type("", TYPE_PVPANIC_MMIO, NULL);
}

#endif
