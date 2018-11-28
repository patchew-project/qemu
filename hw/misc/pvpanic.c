/*
 * QEMU simulated pvpanic device.
 *
 * Copyright Fujitsu, Corp. 2013
 * Copyright (c) 2018 ZTE Ltd.
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

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"

#include "hw/nvram/fw_cfg.h"
#include "hw/misc/pvpanic.h"

/* The bit of supported pv event */
#define PVPANIC_F_PANICKED      0

/* The pv event value */
#define PVPANIC_PANICKED        (1 << PVPANIC_F_PANICKED)

#define PVPANIC_ISA_DEVICE(obj)    \
    OBJECT_CHECK(PVPanicISAState, (obj), TYPE_PVPANIC)

#define PVPANIC_MMIO_DEVICE(obj)    \
    OBJECT_CHECK(PVPanicMMIOState, (obj), TYPE_PVPANIC_MMIO)

static void handle_event(int event)
{
    static bool logged;

    if (event & ~PVPANIC_PANICKED && !logged) {
        qemu_log_mask(LOG_GUEST_ERROR, "pvpanic: unknown event %#x.\n", event);
        logged = true;
    }

    if (event & PVPANIC_PANICKED) {
        qemu_system_guest_panicked(NULL);
        return;
    }
}

#include "hw/isa/isa.h"

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
    uint32_t base;
    /* public */
    MemoryRegion mr;
} PVPanicMMIOState;

/* return supported events on read */
static uint64_t pvpanic_read(void *opaque, hwaddr addr, unsigned size)
{
    return PVPANIC_PANICKED;
}

static void pvpanic_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    handle_event(val);
}

static const MemoryRegionOps pvpanic_ops = {
    .read = pvpanic_read,
    .write = pvpanic_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pvpanic_isa_initfn(Object *obj)
{
    PVPanicISAState *s = PVPANIC_ISA_DEVICE(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &pvpanic_ops, s, "pvpanic", 1);
}

static void pvpanic_isa_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    PVPanicISAState *s = PVPANIC_ISA_DEVICE(dev);
    FWCfgState *fw_cfg = fw_cfg_find();
    uint16_t *pvpanic_port;

    if (!fw_cfg) {
        return;
    }

    pvpanic_port = g_malloc(sizeof(*pvpanic_port));
    *pvpanic_port = cpu_to_le16(s->ioport);
    fw_cfg_add_file(fw_cfg, "etc/pvpanic-port", pvpanic_port,
                    sizeof(*pvpanic_port));

    isa_register_ioport(d, &s->mr, s->ioport);
}

static Property pvpanic_isa_properties[] = {
    DEFINE_PROP_UINT16(PVPANIC_IOPORT_PROP, PVPanicISAState, ioport, 0x505),
    DEFINE_PROP_END_OF_LIST(),
};

static void pvpanic_isa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pvpanic_isa_realizefn;
    dc->props = pvpanic_isa_properties;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static TypeInfo pvpanic_isa_info = {
    .name          = TYPE_PVPANIC,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(PVPanicISAState),
    .instance_init = pvpanic_isa_initfn,
    .class_init    = pvpanic_isa_class_init,
};

static void pvpanic_mmio_initfn(Object *obj)
{
    PVPanicMMIOState *s = PVPANIC_MMIO_DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &pvpanic_ops, s,
                          TYPE_PVPANIC_MMIO, 2);
    sysbus_init_mmio(sbd, &s->mr);
}

static Property pvpanic_mmio_properties[] = {
    DEFINE_PROP_UINT32("mmio", PVPanicMMIOState, base, 0x09070000),
    DEFINE_PROP_END_OF_LIST(),
};

static void pvpanic_mmio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->user_creatable = true;
    dc->props = pvpanic_mmio_properties;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static TypeInfo pvpanic_mmio_info = {
    .name          = TYPE_PVPANIC_MMIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PVPanicMMIOState),
    .instance_init = pvpanic_mmio_initfn,
    .class_init    = pvpanic_mmio_class_init,
};

static void pvpanic_register_types(void)
{
    type_register_static(&pvpanic_isa_info);
    type_register_static(&pvpanic_mmio_info);
}

type_init(pvpanic_register_types)
