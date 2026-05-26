/*
 * QEMU emulated AC adapter device.
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

#include "hw/acpi/acad.h"

#define AC_ADAPTER_DEVICE(obj) OBJECT_CHECK(ACADState, (obj), \
                                            TYPE_AC_ADAPTER)

#define AC_STA_ADDR               0

enum {
    AC_ADAPTER_OFFLINE = 0,
    AC_ADAPTER_ONLINE = 1,
};

typedef struct ACADState {
    ISADevice dev;
    MemoryRegion io;
    uint16_t ioport;
    uint8_t state;
    bool qmp_connected;
} ACADState;

static void acad_get_dynamic_status(ACADState *s)
{
    s->state = s->qmp_connected ? AC_ADAPTER_ONLINE : AC_ADAPTER_OFFLINE;

    trace_acad_get_dynamic_status(s->state);
}

static void acad_realize(DeviceState *dev, Error **errp)
{
    ISADevice *d = ISA_DEVICE(dev);
    ACADState *s = AC_ADAPTER_DEVICE(dev);
    bool ambiguous;

    trace_acad_realize();

    object_resolve_path_type("", TYPE_AC_ADAPTER, &ambiguous);
    if (ambiguous) {
        error_setg(errp, "at most one %s device is permitted",
                   TYPE_AC_ADAPTER);
        return;
    }

    /* Initialize to disconnected by default */
    s->qmp_connected = false;

    isa_register_ioport(d, &s->io, s->ioport);
}

static const Property acad_device_properties[] = {
    DEFINE_PROP_UINT16(AC_ADAPTER_IOPORT_PROP, ACADState, ioport, 0x53c),
};

static const VMStateDescription acad_vmstate = {
    .name = "acad",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(qmp_connected, ACADState),
        VMSTATE_END_OF_LIST()
    }
};

static void build_acad_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    Aml *dev, *field, *method, *pkg;
    Aml *acad_state;
    Aml *sb_scope;
    ACADState *s = AC_ADAPTER_DEVICE(adev);

    acad_state  = aml_local(0);

    sb_scope = aml_scope("\\_SB");
    dev = aml_device("ADP0");
    aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0003")));

    aml_append(dev, aml_operation_region("ACST", AML_SYSTEM_IO,
                                         aml_int(s->ioport),
                                         AC_ADAPTER_LEN));
    field = aml_field("ACST", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("PWRS", 8));
    aml_append(dev, field);

    method = aml_method("_PSR", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_name("PWRS"), acad_state));
    aml_append(method, aml_return(acad_state));
    aml_append(dev, method);

    method = aml_method("_PCL", 0, AML_NOTSERIALIZED);
    pkg = aml_package(1);
    aml_append(pkg, aml_name("_SB"));
    aml_append(method, aml_return(pkg));
    aml_append(dev, method);

    method = aml_method("_PIF", 0, AML_NOTSERIALIZED);
    pkg = aml_package(6);
    /* Power Source State */
    aml_append(pkg, aml_int(0));  /* Non-redundant, non-shared */
    /* Maximum Output Power */
    aml_append(pkg, aml_int(AC_ADAPTER_VAL_UNKNOWN));
    /* Maximum Input Power */
    aml_append(pkg, aml_int(AC_ADAPTER_VAL_UNKNOWN));
    /* Model Number */
    aml_append(pkg, aml_string("QADP001"));
    /* Serial Number */
    aml_append(pkg, aml_string("SN00000"));
    /* OEM Information */
    aml_append(pkg, aml_string("QEMU"));
    aml_append(method, aml_return(pkg));
    aml_append(dev, method);

    aml_append(sb_scope, dev);
    aml_append(scope, sb_scope);

    /* Status Change */
    method = aml_method("\\_GPE._E0B", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.ADP0"), aml_int(0x80)));
    aml_append(scope, method);
}

static void acad_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(class);

    dc->realize = acad_realize;
    dc->hotpluggable = false;
    device_class_set_props(dc, acad_device_properties);
    dc->vmsd = &acad_vmstate;
    adevc->build_dev_aml = build_acad_aml;
}

static uint64_t acad_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    ACADState *s = opaque;

    acad_get_dynamic_status(s);

    switch (addr) {
    case AC_STA_ADDR:
        return s->state;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps acad_ops = {
    .read = acad_ioport_read,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void acad_instance_init(Object *obj)
{
    ACADState *s = AC_ADAPTER_DEVICE(obj);

    memory_region_init_io(&s->io, obj, &acad_ops, s, "acad",
                          AC_ADAPTER_LEN);
}

static const TypeInfo acad_info = {
    .name          = TYPE_AC_ADAPTER,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ACADState),
    .class_init    = acad_class_init,
    .instance_init = acad_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static ACADState *find_acad_device(Error **errp)
{
    bool ambiguous;
    Object *o = object_resolve_path_type("", TYPE_AC_ADAPTER, &ambiguous);

    if (!o) {
        error_setg(errp, "No AC adapter device found");
        return NULL;
    }
    if (ambiguous) {
        error_setg(errp, "More than one AC adapter device present");
        return NULL;
    }
    return AC_ADAPTER_DEVICE(o);
}

void qmp_ac_adapter_set_state(bool connected, Error **errp)
{
    ACADState *s = find_acad_device(errp);
    Object *obj;

    if (!s) {
        return;
    }

    s->qmp_connected = connected;

    obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    if (obj) {
        acpi_send_event(DEVICE(obj), ACPI_AC_ADAPTER_CHANGE_STATUS);
    }
}

AcAdapterInfo *qmp_query_ac_adapter(Error **errp)
{
    ACADState *s = find_acad_device(errp);
    AcAdapterInfo *ret;

    if (!s) {
        return NULL;
    }

    ret = g_new0(AcAdapterInfo, 1);
    ret->connected = s->qmp_connected;

    return ret;
}

static void acad_register_types(void)
{
    type_register_static(&acad_info);
}

type_init(acad_register_types)
