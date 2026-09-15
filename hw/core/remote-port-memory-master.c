/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU remote port memory master.
 *
 * Copyright (c) 2014 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "system/system.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-properties.h"
#include "trace.h"

#include "hw/core/remote-port-proto.h"
#include "hw/core/remote-port.h"
#include "hw/core/remote-port-memory-master.h"

#define REMOTE_PORT_MEMORY_MASTER_PARENT_CLASS \
    object_class_get_parent( \
            object_class_by_name(TYPE_REMOTE_PORT_MEMORY_MASTER))

#define RP_MAX_ACCESS_SIZE 4096

static MemTxResult rp_mm_read(void *opaque, hwaddr addr, uint64_t *data,
                                 unsigned size, MemTxAttrs attrs)
{
    /* TBD */
    return MEMTX_OK;
}

static MemTxResult rp_mm_write(void *opaque, hwaddr addr, uint64_t data,
                                  unsigned size, MemTxAttrs attrs)
{
    /* TBD */
    return MEMTX_OK;
}

static const MemoryRegionOps rp_ops_template = {
    .read_with_attrs = rp_mm_read,
    .write_with_attrs = rp_mm_write,
    .valid.max_access_size = RP_MAX_ACCESS_SIZE,
    .valid.unaligned = true,
    .impl.max_access_size = RP_MAX_ACCESS_SIZE,
    .impl.unaligned = false,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void rp_memory_master_realize(DeviceState *dev, Error **errp)
{
    RemotePortMemoryMaster *s = REMOTE_PORT_MEMORY_MASTER(dev);
    RemotePortDevice *rpd = REMOTE_PORT_DEVICE(dev);
    int i;

    /* Sanity check max access size.  */
    if (s->max_access_size > RP_MAX_ACCESS_SIZE) {
        error_setg(errp, "%s: max-access-size %d too large! MAX is %d",
                   TYPE_REMOTE_PORT_MEMORY_MASTER, s->max_access_size,
                   RP_MAX_ACCESS_SIZE);
        return;
    }

    if (s->max_access_size < 4) {
        error_setg(errp, "%s: max-access-size %d too small! MIN is 4",
                   TYPE_REMOTE_PORT_MEMORY_MASTER, s->max_access_size);
        return;
    }

    assert(s->rp);

    rp_register_dev(s->rp, rpd, s->rp_dev);

    s->peer = rp_get_peer(s->rp);

    /* Create a single static region if configuration says so.  */
    if (s->map_num) {
        /* Initialize rp_ops from template.  */
        s->rp_ops = g_malloc(sizeof *s->rp_ops);
        memcpy(s->rp_ops, &rp_ops_template, sizeof *s->rp_ops);
        s->rp_ops->valid.max_access_size = s->max_access_size;
        s->rp_ops->impl.max_access_size = s->max_access_size;

        s->mmaps = g_new0(typeof(*s->mmaps), s->map_num);
        for (i = 0; i < s->map_num; ++i) {
            char *name = g_strdup_printf("rp-%d", i);

            s->mmaps[i].offset = s->map_offset;
            memory_region_init_io(&s->mmaps[i].iomem, OBJECT(dev), s->rp_ops,
                                  &s->mmaps[i], name, s->map_size);
            sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmaps[i].iomem);
            s->mmaps[i].parent = s;
            g_free(name);
        }
    }
}

static void rp_prop_allow_set_link(const Object *obj, const char *name,
                                   Object *val, Error **errp)
{
}

static void rp_memory_master_init(Object *obj)
{
    RemotePortMemoryMaster *rpms = REMOTE_PORT_MEMORY_MASTER(obj);
    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&rpms->rp,
                             rp_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
}

static Property rp_properties[] = {
    DEFINE_PROP_UINT32("map-num", RemotePortMemoryMaster, map_num, 0),
    DEFINE_PROP_UINT64("map-offset", RemotePortMemoryMaster, map_offset, 0),
    DEFINE_PROP_UINT64("map-size", RemotePortMemoryMaster, map_size, 0),
    DEFINE_PROP_UINT32("rp-chan0", RemotePortMemoryMaster, rp_dev, 0),
    DEFINE_PROP_BOOL("relative", RemotePortMemoryMaster, relative, false),
    DEFINE_PROP_UINT32("max-access-size", RemotePortMemoryMaster,
                       max_access_size, RP_MAX_ACCESS_SIZE),
};

static void rp_memory_master_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    device_class_set_props_n(dc, rp_properties, ARRAY_SIZE(rp_properties));
    dc->realize = rp_memory_master_realize;
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_MEMORY_MASTER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RemotePortMemoryMaster),
    .instance_init = rp_memory_master_init,
    .class_init    = rp_memory_master_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_REMOTE_PORT_DEVICE },
        { },
    },
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
}

type_init(rp_register_types)
