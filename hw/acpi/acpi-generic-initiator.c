// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qom/object_interfaces.h"
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "hw/vfio/vfio-common.h"
#include "hw/vfio/pci.h"
#include "hw/pci/pci_device.h"
#include "sysemu/numa.h"
#include "hw/acpi/acpi-generic-initiator.h"

OBJECT_DEFINE_TYPE_WITH_INTERFACES(AcpiGenericInitiator, acpi_generic_initiator,
                   ACPI_GENERIC_INITIATOR, OBJECT,
                   { TYPE_USER_CREATABLE },
                   { NULL })

OBJECT_DECLARE_SIMPLE_TYPE(AcpiGenericInitiator, ACPI_GENERIC_INITIATOR)

static void acpi_generic_initiator_init(Object *obj)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);
    gi->device = NULL;
    gi->node = MAX_NODES;
    gi->node_count = 1;
}

static void acpi_generic_initiator_finalize(Object *obj)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    g_free(gi->device);
}

static void acpi_generic_initiator_set_device(Object *obj, const char *value,
                                              Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    gi->device = g_strdup(value);
}

static void acpi_generic_initiator_set_node(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    if (value >= MAX_NODES) {
        return;
    }

    gi->node = value;
}

static void acpi_generic_initiator_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, ACPI_GENERIC_INITIATOR_DEVICE_PROP, NULL,
                                  acpi_generic_initiator_set_device);
    object_class_property_add(oc, ACPI_GENERIC_INITIATOR_NODE_PROP, "uint32",
                              NULL, acpi_generic_initiator_set_node, NULL,
                              NULL);
}
