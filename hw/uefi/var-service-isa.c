/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - ISA variant for x64.
 */
#include "qemu/osdep.h"
#include "migration/vmstate.h"

#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"

#include "hw/uefi/var-service.h"
#include "hw/uefi/var-service-api.h"

#define TYPE_UEFI_VARS_ISA "uefi-vars-isa"
OBJECT_DECLARE_SIMPLE_TYPE(uefi_vars_isa_state, UEFI_VARS_ISA)

struct uefi_vars_isa_state {
    ISADevice parent_obj;
    struct uefi_vars_state state;
};

static const VMStateDescription vmstate_uefi_vars_isa = {
    .name = "uefi-vars-isa",
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(state, uefi_vars_isa_state, 0,
                       vmstate_uefi_vars, uefi_vars_state),
        VMSTATE_END_OF_LIST()
    }
};

static Property uefi_vars_isa_properties[] = {
    DEFINE_PROP_SIZE("size", uefi_vars_isa_state, state.max_storage,
                     256 * 1024),
    DEFINE_PROP_STRING("jsonfile", uefi_vars_isa_state, state.jsonfile),
    DEFINE_PROP_END_OF_LIST(),
};

static void uefi_vars_isa_init(Object *obj)
{
    uefi_vars_isa_state *uv = UEFI_VARS_ISA(obj);

    uefi_vars_init(obj, &uv->state);
}

static void uefi_vars_isa_reset(DeviceState *dev)
{
    uefi_vars_isa_state *uv = UEFI_VARS_ISA(dev);

    uefi_vars_hard_reset(&uv->state);
}

static void uefi_vars_isa_realize(DeviceState *dev, Error **errp)
{
    uefi_vars_isa_state *uv = UEFI_VARS_ISA(dev);
    ISADevice *isa = ISA_DEVICE(dev);

    isa_register_ioport(isa, &uv->state.mr, UEFI_VARS_IO_BASE);
    uefi_vars_realize(&uv->state, errp);
}

static void uefi_vars_isa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = uefi_vars_isa_realize;
    dc->reset = uefi_vars_isa_reset;
    dc->vmsd = &vmstate_uefi_vars_isa;
    device_class_set_props(dc, uefi_vars_isa_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo uefi_vars_isa_info = {
    .name          = TYPE_UEFI_VARS_ISA,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(uefi_vars_isa_state),
    .instance_init = uefi_vars_isa_init,
    .class_init    = uefi_vars_isa_class_init,
};
module_obj(TYPE_UEFI_VARS_ISA);
module_dep("hw-uefi-vars");

static void uefi_vars_isa_register_types(void)
{
    type_register_static(&uefi_vars_isa_info);
}

type_init(uefi_vars_isa_register_types)
