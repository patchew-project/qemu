// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "qemu/osdep.h"
#include "hw/acpi/acpi-generic-initiator.h"
#include "hw/pci/pci_device.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"

OBJECT_DEFINE_TYPE_WITH_INTERFACES(AcpiGenericInitiator, acpi_generic_initiator,
                   ACPI_GENERIC_INITIATOR, OBJECT,
                   { TYPE_USER_CREATABLE },
                   { NULL })

OBJECT_DECLARE_SIMPLE_TYPE(AcpiGenericInitiator, ACPI_GENERIC_INITIATOR)

static void acpi_generic_initiator_init(Object *obj)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);
    bitmap_zero(gi->host_nodes, MAX_NODES);
    gi->pci_dev = NULL;
}

static void acpi_generic_initiator_finalize(Object *obj)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    g_free(gi->pci_dev);
}

static void acpi_generic_initiator_set_pci_device(Object *obj, const char *val,
                                                  Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    gi->pci_dev = g_strdup(val);
}

static void
acpi_generic_initiator_set_host_nodes(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);
    uint16List *l = NULL, *host_nodes = NULL;

    visit_type_uint16List(v, name, &host_nodes, errp);

    for (l = host_nodes; l; l = l->next) {
        if (l->value >= MAX_NODES) {
            error_setg(errp, "Invalid host-nodes value: %d", l->value);
            break;
        } else {
            bitmap_set(gi->host_nodes, l->value, 1);
        }
    }

    qapi_free_uint16List(host_nodes);
}

static void acpi_generic_initiator_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "pci-dev", NULL,
        acpi_generic_initiator_set_pci_device);
    object_class_property_add(oc, "host-nodes", "int", NULL,
        acpi_generic_initiator_set_host_nodes, NULL, NULL);
}
