/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - sysbus variant.
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"

#include "hw/qdev-properties.h"
#include "hw/sysbus.h"

#include "hw/uefi/var-service.h"
#include "hw/uefi/var-service-api.h"

#define TYPE_UEFI_VARS_SYSBUS "uefi-vars-sysbus"
OBJECT_DECLARE_SIMPLE_TYPE(uefi_vars_sysbus_state, UEFI_VARS_SYSBUS)

struct uefi_vars_sysbus_state {
    SysBusDevice parent_obj;
    struct uefi_vars_state state;
};

static const VMStateDescription vmstate_uefi_vars_sysbus = {
    .name = "uefi-vars-sysbus",
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(state, uefi_vars_sysbus_state, 0,
                       vmstate_uefi_vars, uefi_vars_state),
        VMSTATE_END_OF_LIST()
    }
};

static Property uefi_vars_sysbus_properties[] = {
    DEFINE_PROP_SIZE("size", uefi_vars_sysbus_state, state.max_storage,
                     256 * 1024),
    DEFINE_PROP_STRING("jsonfile", uefi_vars_sysbus_state, state.jsonfile),
    DEFINE_PROP_END_OF_LIST(),
};

static void uefi_vars_sysbus_init(Object *obj)
{
    uefi_vars_sysbus_state *uv = UEFI_VARS_SYSBUS(obj);

    uefi_vars_init(obj, &uv->state);
}

static void uefi_vars_sysbus_reset(DeviceState *dev)
{
    uefi_vars_sysbus_state *uv = UEFI_VARS_SYSBUS(dev);

    uefi_vars_hard_reset(&uv->state);
}

static void uefi_vars_sysbus_realize(DeviceState *dev, Error **errp)
{
    uefi_vars_sysbus_state *uv = UEFI_VARS_SYSBUS(dev);
    SysBusDevice *sysbus = SYS_BUS_DEVICE(dev);

    sysbus_init_mmio(sysbus, &uv->state.mr);
    uefi_vars_realize(&uv->state, errp);
}

static void uefi_vars_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = uefi_vars_sysbus_realize;
    dc->reset = uefi_vars_sysbus_reset;
    dc->vmsd = &vmstate_uefi_vars_sysbus;
    device_class_set_props(dc, uefi_vars_sysbus_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo uefi_vars_sysbus_info = {
    .name          = TYPE_UEFI_VARS_SYSBUS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(uefi_vars_sysbus_state),
    .instance_init = uefi_vars_sysbus_init,
    .class_init    = uefi_vars_sysbus_class_init,
};
module_obj(TYPE_UEFI_VARS_SYSBUS);

static void uefi_vars_sysbus_register_types(void)
{
    type_register_static(&uefi_vars_sysbus_info);
}

type_init(uefi_vars_sysbus_register_types)
