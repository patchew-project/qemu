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
    gi->nodelist = NULL;
}

static void acpi_generic_initiator_finalize(Object *obj)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    g_free(gi->device);
    qapi_free_uint16List(gi->nodelist);
}

static void acpi_generic_initiator_set_device(Object *obj, const char *val,
                                              Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    gi->device = g_strdup(val);
}

static void acpi_generic_initiator_set_nodelist(Object *obj, const char *val,
                                            Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);
    char *value = g_strdup(val);
    uint16_t node;
    uint16List **tail = &(gi->nodelist);
    char *nodestr = value ? strtok(value, ":") : NULL;

    while (nodestr) {
        if (sscanf(nodestr, "%hu", &node) != 1) {
            error_setg(errp, "failed to read node-id");
            return;
        }

        if (node >= MAX_NODES) {
            error_setg(errp, "invalid node-id");
            return;
        }

        QAPI_LIST_APPEND(tail, node);
        nodestr = strtok(NULL, ":");
    }
}

static void acpi_generic_initiator_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, ACPI_GENERIC_INITIATOR_DEVICE_PROP, NULL,
                                  acpi_generic_initiator_set_device);
    object_class_property_add_str(oc, ACPI_GENERIC_INITIATOR_NODELIST_PROP,
                                  NULL, acpi_generic_initiator_set_nodelist);
}
