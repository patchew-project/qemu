// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
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
    gi->nodelist = NULL;
}

static void acpi_generic_initiator_finalize(Object *obj)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    g_free(gi->device);
    qapi_free_uint16List(gi->nodelist);
}

static void acpi_generic_initiator_set_pci_device(Object *obj, const char *val,
                                                  Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    gi->device = g_strdup(val);
}

static void acpi_generic_initiator_set_acpi_device(Object *obj, const char *val,
                                                   Error **errp)
{
    error_setg(errp, "Generic Initiator ACPI device not supported");
}

static void
acpi_generic_initiator_set_host_nodes(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);
    uint16List *l;

    visit_type_uint16List(v, name, &(gi->nodelist), errp);

    for (l = gi->nodelist; l; l = l->next) {
        if (l->value >= MAX_NODES) {
            error_setg(errp, "Invalid host-nodes value: %d", l->value);
            qapi_free_uint16List(gi->nodelist);
            return;
        }
    }
}

static void acpi_generic_initiator_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, ACPI_GENERIC_INITIATOR_PCI_DEVICE_PROP,
                                  NULL, acpi_generic_initiator_set_pci_device);
    object_class_property_add_str(oc, ACPI_GENERIC_INITIATOR_ACPI_DEVICE_PROP,
                                  NULL, acpi_generic_initiator_set_acpi_device);
    object_class_property_add(oc, ACPI_GENERIC_INITIATOR_HOSTNODE_PROP, "int",
        NULL,
        acpi_generic_initiator_set_host_nodes,
        NULL, NULL);
}
