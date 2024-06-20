// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "qemu/osdep.h"
#include "hw/acpi/acpi_generic_initiator.h"
#include "hw/acpi/aml-build.h"
#include "hw/boards.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_device.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

typedef struct AcpiGenericInitiatorClass {
    ObjectClass parent_class;
} AcpiGenericInitiatorClass;

OBJECT_DEFINE_TYPE_WITH_INTERFACES(AcpiGenericInitiator, acpi_generic_initiator,
                   ACPI_GENERIC_INITIATOR, OBJECT,
                   { TYPE_USER_CREATABLE },
                   { NULL })

OBJECT_DECLARE_SIMPLE_TYPE(AcpiGenericInitiator, ACPI_GENERIC_INITIATOR)

static void acpi_generic_initiator_init(Object *obj)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);

    gi->node = MAX_NODES;
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

static void acpi_generic_initiator_set_node(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);
    MachineState *ms = MACHINE(qdev_get_machine());
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    if (value >= MAX_NODES) {
        error_printf("%s: Invalid NUMA node specified\n",
                     TYPE_ACPI_GENERIC_INITIATOR);
        exit(1);
    }

    gi->node = value;
    ms->numa_state->nodes[gi->node].has_gi = true;
}

static void acpi_generic_initiator_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "pci-dev", NULL,
        acpi_generic_initiator_set_pci_device);
    object_class_property_add(oc, "node", "int", NULL,
        acpi_generic_initiator_set_node, NULL, NULL);
}

typedef struct AcpiGenericPortClass {
    ObjectClass parent_class;
} AcpiGenericPortClass;

OBJECT_DEFINE_TYPE_WITH_INTERFACES(AcpiGenericPort, acpi_generic_port,
                   ACPI_GENERIC_PORT, OBJECT,
                   { TYPE_USER_CREATABLE },
                   { NULL })

OBJECT_DECLARE_SIMPLE_TYPE(AcpiGenericPort, ACPI_GENERIC_PORT)

static void acpi_generic_port_init(Object *obj)
{
    AcpiGenericPort *gp = ACPI_GENERIC_PORT(obj);

    gp->node = MAX_NODES;
    gp->pci_bus = NULL;
}

static void acpi_generic_port_finalize(Object *obj)
{
    AcpiGenericPort *gp = ACPI_GENERIC_PORT(obj);

    g_free(gp->pci_bus);
}

static void acpi_generic_port_set_pci_bus(Object *obj, const char *val,
                                          Error **errp)
{
    AcpiGenericPort *gp = ACPI_GENERIC_PORT(obj);

    gp->pci_bus = g_strdup(val);
}

static void acpi_generic_port_set_node(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    AcpiGenericPort *gp = ACPI_GENERIC_PORT(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    if (value >= MAX_NODES) {
        error_printf("%s: Invalid NUMA node specified\n",
                     TYPE_ACPI_GENERIC_INITIATOR);
        exit(1);
    }

    gp->node = value;
}

static void acpi_generic_port_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "pci-bus", NULL,
        acpi_generic_port_set_pci_bus);
    object_class_property_add(oc, "node", "int", NULL,
        acpi_generic_port_set_node, NULL, NULL);
}

static int build_acpi_generic_initiator(Object *obj, void *opaque)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    AcpiGenericInitiator *gi;
    GArray *table_data = opaque;
    uint8_t bus, devfn;
    Object *o;

    if (!object_dynamic_cast(obj, TYPE_ACPI_GENERIC_INITIATOR)) {
        return 0;
    }

    gi = ACPI_GENERIC_INITIATOR(obj);
    if (gi->node >= ms->numa_state->num_nodes) {
        error_printf("%s: Specified node %d is invalid.\n",
                     TYPE_ACPI_GENERIC_INITIATOR, gi->node);
        exit(1);
    }

    o = object_resolve_path_type(gi->pci_dev, TYPE_PCI_DEVICE, NULL);
    if (!o) {
        error_printf("%s: Specified device must be a PCI device.\n",
                     TYPE_ACPI_GENERIC_INITIATOR);
        exit(1);
    }

    bus = object_property_get_uint(o, "bus", &error_fatal);
    devfn = object_property_get_uint(o, "addr", &error_fatal);

    build_srat_pci_generic_initiator(table_data, gi->node, 0, bus, devfn);

    return 0;
}

static int build_acpi_generic_port(Object *obj, void *opaque)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    const char *hid = "ACPI0016";
    GArray *table_data = opaque;
    AcpiGenericPort *gp;
    uint32_t uid;
    Object *o;

    if (!object_dynamic_cast(obj, TYPE_ACPI_GENERIC_PORT)) {
        return 0;
    }

    gp = ACPI_GENERIC_PORT(obj);

    if (gp->node >= ms->numa_state->num_nodes) {
        error_printf("%s: node %d is invalid.\n",
                     TYPE_ACPI_GENERIC_PORT, gp->node);
        exit(1);
    }

    o = object_resolve_path_type(gp->pci_bus, TYPE_PXB_CXL_BUS, NULL);
    if (!o) {
        error_printf("%s: device must be a CXL host bridge.\n",
                     TYPE_ACPI_GENERIC_PORT);
        exit(1);
    }

    uid = object_property_get_uint(o, "acpi_uid", &error_fatal);
    build_srat_acpi_generic_port(table_data, gp->node, hid, uid);

    return 0;
}

void build_srat_generic_pci_initiator(GArray *table_data)
{
    object_child_foreach_recursive(object_get_root(),
                                   build_acpi_generic_initiator,
                                   table_data);
    object_child_foreach_recursive(object_get_root(), build_acpi_generic_port,
                                   table_data);
}
