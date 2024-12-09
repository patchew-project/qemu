/*
 * CTU CAN FD memory mapped device emulation
 * http://canbus.pages.fel.cvut.cz/
 *
 * Copyright (c) 2024 Pavel Pisa (pisa@cmp.felk.cvut.cz)
 *
 * Based on Kvaser PCI CAN device (SJA1000 based) emulation implemented by
 * Jin Yang and Pavel Pisa
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/event_notifier.h"
#include "qemu/module.h"
#include "qemu/thread.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"

#include "ctucan_core.h"

#define TYPE_CTUCAN_MM_DEV "ctucan_mm"

typedef struct CtuCanMmState CtuCanMmState;
DECLARE_INSTANCE_CHECKER(CtuCanMmState, CTUCAN_MM_DEV,
                         TYPE_CTUCAN_MM_DEV)

#define CTUCAN_MM_CORE_COUNT     1
#define CTUCAN_MM_CORE_RANGE     0x1000

#define CTUCAN_MM_BYTES_PER_CORE 0x1000

struct CtuCanMmState {
    /*< private >*/
    SysBusDevice    parent_obj;
    /*< public >*/

    struct {
        uint64_t    iobase;
        uint32_t    irq;
    } cfg;

    MemoryRegion    ctucan_io_region;

    CtuCanCoreState ctucan_state[CTUCAN_MM_CORE_COUNT];
    qemu_irq        irq;

    char            *model;
    CanBusState     *canbus[CTUCAN_MM_CORE_COUNT];
};

static void ctucan_mm_reset(DeviceState *dev)
{
    CtuCanMmState *d = CTUCAN_MM_DEV(dev);
    int i;

    for (i = 0 ; i < CTUCAN_MM_CORE_COUNT; i++) {
        ctucan_hardware_reset(&d->ctucan_state[i]);
    }
}

static uint64_t ctucan_mm_cores_io_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    CtuCanMmState *d = opaque;
    CtuCanCoreState *s;
    hwaddr core_num = addr / CTUCAN_MM_BYTES_PER_CORE;

    if (core_num >= CTUCAN_MM_CORE_COUNT) {
        return 0;
    }

    s = &d->ctucan_state[core_num];

    return ctucan_mem_read(s, addr % CTUCAN_MM_BYTES_PER_CORE, size);
}

static void ctucan_mm_cores_io_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    CtuCanMmState *d = opaque;
    CtuCanCoreState *s;
    hwaddr core_num = addr / CTUCAN_MM_BYTES_PER_CORE;

    if (core_num >= CTUCAN_MM_CORE_COUNT) {
        return;
    }

    s = &d->ctucan_state[core_num];

    return ctucan_mem_write(s, addr % CTUCAN_MM_BYTES_PER_CORE, data, size);
}

static const MemoryRegionOps ctucan_mm_cores_io_ops = {
    .read = ctucan_mm_cores_io_read,
    .write = ctucan_mm_cores_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void ctucan_mm_realize(DeviceState *dev, Error **errp)
{
    CtuCanMmState *d = CTUCAN_MM_DEV(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    for (i = 0 ; i < CTUCAN_MM_CORE_COUNT; i++) {
        ctucan_init(&d->ctucan_state[i], d->irq);
    }

    for (i = 0 ; i < CTUCAN_MM_CORE_COUNT; i++) {
        if (ctucan_connect_to_bus(&d->ctucan_state[i], d->canbus[i]) < 0) {
            error_setg(errp, "ctucan_connect_to_bus failed");
            return;
        }
    }

    /* memory_region_add_subregion(get_system_memory(), 0x43c30000, &d->ctucan_io_region); */
    if (d->cfg.iobase != 0) {
        sysbus_mmio_map(sbd, 0, d->cfg.iobase);
    }
    if (d->cfg.irq != 0) {
        //const char *id = "/machine/unattached/device[3]/gic";
        const char *id = "/machine/unattached/device[3]";
        Object *obj = object_resolve_path_at(container_get(qdev_get_machine(), "/peripheral"), id);
        DeviceState *gicdev;
        if (!obj) {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND, "Device '%s' not found", id);
            return;
        }
        gicdev = (DeviceState *)object_dynamic_cast(obj, TYPE_DEVICE);
        if (!gicdev) {
            error_setg(errp, "%s is not a hotpluggable device", id);
            return;
        }
        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(gicdev, d->cfg.irq));
    }
}

/*
static void ctucan_mm_exit(Object *obj)
{
    CtuCanMmState *d = CTUCAN_MM_DEV(obj);
    int i;

    for (i = 0 ; i < CTUCAN_MM_CORE_COUNT; i++) {
        ctucan_disconnect(&d->ctucan_state[i]);
    }

    qemu_free_irq(d->irq);
}
*/

static void ctucan_mm_reset_init(Object *obj, ResetType type)
{
    CtuCanMmState *d = CTUCAN_MM_DEV(obj);
    unsigned int i;

    for (i = 0 ; i < CTUCAN_MM_CORE_COUNT; i++) {
        ctucan_init(&d->ctucan_state[i], d->irq);
    }
}

static void ctucan_mm_reset_hold(Object *obj, ResetType type)
{
    CtuCanMmState *d = CTUCAN_MM_DEV(obj);
    unsigned int i;

    for (i = 0 ; i < CTUCAN_MM_CORE_COUNT; i++) {
        ctucan_init(&d->ctucan_state[i], d->irq);
    }
}

static const VMStateDescription vmstate_ctucan_mm = {
    .name = "ctucan_mm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(ctucan_state[0], CtuCanMmState, 0, vmstate_ctucan,
                       CtuCanCoreState),
#if CTUCAN_MM_CORE_COUNT >= 2
        VMSTATE_STRUCT(ctucan_state[1], CtuCanMmState, 0, vmstate_ctucan,
                       CtuCanCoreState),
#endif
        VMSTATE_END_OF_LIST()
    }
};

static void ctucan_mm_instance_init(Object *obj)
{
    CtuCanMmState *d = CTUCAN_MM_DEV(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

#if CTUCAN_MM_CORE_COUNT <= 1
    object_property_add_link(obj, "canbus", TYPE_CAN_BUS,
                             (Object **)&d->canbus[0],
                             qdev_prop_allow_set_link_before_realize, 0);
#else /* CTUCAN_MM_CORE_COUNT >= 2 */
    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&d->canbus[0],
                             qdev_prop_allow_set_link_before_realize, 0);
    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&d->canbus[1],
                             qdev_prop_allow_set_link_before_realize, 0);
#endif
    memory_region_init_io(&d->ctucan_io_region, OBJECT(d),
                          &ctucan_mm_cores_io_ops, d,
                          "ctucan_mm", CTUCAN_MM_CORE_RANGE);

    sysbus_init_mmio(sbd, &d->ctucan_io_region);
    sysbus_init_irq(sbd, &d->irq);
}

static Property ctucan_mm_properties[] = {
    //DEFINE_PROP_UNSIGNED_NODEFAULT("base", CtuCanMmState, cfg.base,
    //                               qdev_prop_uint64, uint64_t),
    DEFINE_PROP_UINT64("iobase", CtuCanMmState, cfg.iobase, 0),
    DEFINE_PROP_UINT32("irq", CtuCanMmState, cfg.irq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ctucan_mm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = ctucan_mm_reset_init;
    rc->phases.hold = ctucan_mm_reset_hold;
    dc->realize = ctucan_mm_realize;
    /* ->exit = ctucan_mm_exit; */
    dc->desc = "CTU CAN MM";
    dc->vmsd = &vmstate_ctucan_mm;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->user_creatable = true;
    /* dc->reset = ctucan_mm_reset; */
    device_class_set_legacy_reset(dc, ctucan_mm_reset);

    device_class_set_props(dc, ctucan_mm_properties);
}

static const TypeInfo ctucan_mm_info = {
    .name          = TYPE_CTUCAN_MM_DEV,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CtuCanMmState),
    .class_init    = ctucan_mm_class_init,
    .instance_init = ctucan_mm_instance_init,
};

static void ctucan_mm_register_types(void)
{
    type_register_static(&ctucan_mm_info);
}

type_init(ctucan_mm_register_types)
