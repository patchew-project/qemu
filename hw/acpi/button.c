/*
 * QEMU emulated lid button device
 *
 * Copyright (c) 2019-2026 Janus Technologies, Inc. (http://janustech.com)
 *
 * Authors:
 *     Leonid Bloch <lb.workbox@gmail.com>
 *     Marcel Apfelbaum <marcel.apfelbaum@gmail.com>
 *     Dmitry Fleytman <dmitry.fleytman@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "trace.h"
#include "hw/isa/isa.h"
#include "hw/acpi/acpi.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "qapi/qapi-commands-acpi.h"

#include "hw/acpi/button.h"

#define BUTTON_DEVICE(obj) OBJECT_CHECK(ButtonState, (obj), \
                                        TYPE_BUTTON)

#define BUTTON_STA_ADDR            0

enum {
    LID_CLOSED = 0,
    LID_OPEN = 1,
};

typedef struct ButtonState {
    ISADevice dev;
    MemoryRegion io;
    uint16_t ioport;
    uint8_t lid_state;
    bool qmp_lid_open;
} ButtonState;

static void button_get_dynamic_status(ButtonState *s)
{
    trace_button_get_dynamic_status();

    s->lid_state = s->qmp_lid_open ? LID_OPEN : LID_CLOSED;
}

static void button_realize(DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    ButtonState *s = BUTTON_DEVICE(dev);
    bool ambiguous;

    trace_button_realize();

    object_resolve_path_type("", TYPE_BUTTON, &ambiguous);
    if (ambiguous) {
        error_setg(errp, "at most one %s device is permitted", TYPE_BUTTON);
        return;
    }

    /* Initialize lid to open by default */
    s->qmp_lid_open = true;

    isa_register_ioport(d, &s->io, s->ioport);
}

static const Property button_device_properties[] = {
    DEFINE_PROP_UINT16(BUTTON_IOPORT_PROP, ButtonState, ioport, 0x53d),
};

static const VMStateDescription button_vmstate = {
    .name = "button",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(qmp_lid_open, ButtonState),
        VMSTATE_END_OF_LIST()
    }
};

static void build_button_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    Aml *dev, *field, *method;
    Aml *button_state;
    Aml *sb_scope;
    ButtonState *s = BUTTON_DEVICE(adev);

    button_state = aml_local(0);

    sb_scope = aml_scope("\\_SB");
    dev = aml_device("LID0");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0C0D")));

    aml_append(dev, aml_operation_region("LSTA", AML_SYSTEM_IO,
                                         aml_int(s->ioport),
                                         BUTTON_LEN));
    field = aml_field("LSTA", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("LIDS", 8));
    aml_append(dev, field);

    method = aml_method("_LID", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_name("LIDS"), button_state));
    aml_append(method, aml_return(button_state));
    aml_append(dev, method);

    aml_append(sb_scope, dev);
    aml_append(scope, sb_scope);

    /* Status Change */
    method = aml_method("\\_GPE._E0C", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.LID0"), aml_int(0x80)));
    aml_append(scope, method);
}

static void button_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(class);

    dc->realize = button_realize;
    dc->hotpluggable = false;
    device_class_set_props(dc, button_device_properties);
    dc->vmsd = &button_vmstate;
    adevc->build_dev_aml = build_button_aml;
}

static uint64_t button_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    ButtonState *s = opaque;

    button_get_dynamic_status(s);

    switch (addr) {
    case BUTTON_STA_ADDR:
        return s->lid_state;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps button_ops = {
    .read = button_ioport_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void button_instance_init(Object *obj)
{
    ButtonState *s = BUTTON_DEVICE(obj);

    memory_region_init_io(&s->io, obj, &button_ops, s, "button",
                          BUTTON_LEN);
}

static const TypeInfo button_info = {
    .name          = TYPE_BUTTON,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ButtonState),
    .class_init    = button_class_init,
    .instance_init = button_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static ButtonState *find_button_device(Error **errp)
{
    bool ambiguous;
    Object *o = object_resolve_path_type("", TYPE_BUTTON, &ambiguous);

    if (!o) {
        error_setg(errp, "No lid button device found");
        return NULL;
    }
    if (ambiguous) {
        error_setg(errp, "More than one lid button device present");
        return NULL;
    }
    return BUTTON_DEVICE(o);
}

void qmp_lid_button_set_state(bool open, Error **errp)
{
    ButtonState *s = find_button_device(errp);
    Object *obj;

    if (!s) {
        return;
    }

    s->qmp_lid_open = open;

    obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    if (obj) {
        acpi_send_event(DEVICE(obj), ACPI_BUTTON_CHANGE_STATUS);
    }
}

LidButtonInfo *qmp_query_lid_button(Error **errp)
{
    ButtonState *s = find_button_device(errp);
    LidButtonInfo *ret;

    if (!s) {
        return NULL;
    }

    ret = g_new0(LidButtonInfo, 1);
    ret->open = s->qmp_lid_open;

    return ret;
}

static void button_register_types(void)
{
    type_register_static(&button_info);
}

type_init(button_register_types)
