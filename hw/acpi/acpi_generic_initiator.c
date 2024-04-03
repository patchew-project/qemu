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

typedef struct AcpiGenericNodeClass {
    ObjectClass parent_class;
} AcpiGenericNodeClass;

typedef struct AcpiGenericInitiatorClass {
     AcpiGenericNodeClass parent_class;
} AcpiGenericInitiatorClass;

typedef struct AcpiGenericPortClass {
    AcpiGenericInitiatorClass parent;
} AcpiGenericPortClass;

OBJECT_DEFINE_ABSTRACT_TYPE(AcpiGenericNode, acpi_generic_node,
                            ACPI_GENERIC_NODE, OBJECT)

OBJECT_DECLARE_SIMPLE_TYPE(AcpiGenericNode, ACPI_GENERIC_NODE)

OBJECT_DEFINE_TYPE_WITH_INTERFACES(AcpiGenericInitiator, acpi_generic_initiator,
                   ACPI_GENERIC_INITIATOR, ACPI_GENERIC_NODE,
                   { TYPE_USER_CREATABLE },
                   { NULL })

OBJECT_DECLARE_SIMPLE_TYPE(AcpiGenericInitiator, ACPI_GENERIC_INITIATOR)

OBJECT_DEFINE_TYPE_WITH_INTERFACES(AcpiGenericPort, acpi_generic_port,
                   ACPI_GENERIC_PORT, ACPI_GENERIC_NODE,
                   { TYPE_USER_CREATABLE },
                   { NULL })

OBJECT_DECLARE_SIMPLE_TYPE(AcpiGenericPort, ACPI_GENERIC_PORT)

static void acpi_generic_node_init(Object *obj)
{
    AcpiGenericNode *gn = ACPI_GENERIC_NODE(obj);

    gn->node = MAX_NODES;
    gn->pci_dev = NULL;
}

static void acpi_generic_initiator_init(Object *obj)
{
}

static void acpi_generic_node_finalize(Object *obj)
{
    AcpiGenericNode *gn = ACPI_GENERIC_NODE(obj);

    g_free(gn->pci_dev);
}

static void acpi_generic_initiator_finalize(Object *obj)
{
}

static void acpi_generic_port_init(Object *obj)
{
}

static void acpi_generic_port_finalize(Object *obj)
{
}

static void acpi_generic_node_set_pci_device(Object *obj, const char *val,
                                             Error **errp)
{
    AcpiGenericNode *gn = ACPI_GENERIC_NODE(obj);

    gn->pci_dev = g_strdup(val);
}
static void acpi_generic_node_set_node(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    AcpiGenericNode *gn = ACPI_GENERIC_NODE(obj);
    MachineState *ms = MACHINE(qdev_get_machine());
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    if (value >= MAX_NODES) {
        error_printf("%s: Invalid NUMA node specified\n",
                     TYPE_ACPI_GENERIC_NODE);
        exit(1);
    }

    gn->node = value;
    if (object_dynamic_cast(obj, TYPE_ACPI_GENERIC_INITIATOR)) {
        ms->numa_state->nodes[gn->node].has_gi = true;
    }
}

static void acpi_generic_node_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add(oc, "node", "int", NULL,
        acpi_generic_node_set_node, NULL, NULL);
}

static void acpi_generic_initiator_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "pci-dev", NULL,
        acpi_generic_node_set_pci_device);
}

static void acpi_generic_port_class_init(ObjectClass *oc, void *data)
{
    /*
     * Despite the ID representing a root bridge bus, same storage
     * can be used.
     */
    object_class_property_add_str(oc, "pci-bus", NULL,
        acpi_generic_node_set_pci_device);
}

/*
 * ACPI 6.3:
 * Table 5-78 Generic Initiator Affinity Structure
 * ACPI 6.5:
 * Table 5-67 Generic Port Affinity Structure
 */
static void
build_srat_generic_node_affinity(GArray *table_data, int node,
                                 PCIDeviceHandle *handle, bool gp, bool pci)
{
    build_append_int_noprefix(table_data, gp ? 6 : 5, 1);  /* Type */
    build_append_int_noprefix(table_data, 32, 1); /* Length */
    build_append_int_noprefix(table_data, 0, 1);  /* Reserved */
    /* Device Handle Type: PCI / ACPI */
    build_append_int_noprefix(table_data, pci ? 1 : 0, 1);
    build_append_int_noprefix(table_data, node, 4);  /* Proximity Domain */

    /* Device Handle - PCI */
    if (pci) {
        /* Device Handle - PCI */
        build_append_int_noprefix(table_data, handle->segment, 2);
        build_append_int_noprefix(table_data, handle->bdf, 2);
        build_append_int_noprefix(table_data, 0, 12);
    } else {
        /* Device Handle - ACPI */
        build_append_int_noprefix(table_data, handle->hid, 8);
        build_append_int_noprefix(table_data, handle->uid, 4);
        build_append_int_noprefix(table_data, 0, 4);
    }

    build_append_int_noprefix(table_data, GEN_AFFINITY_ENABLED, 4); /* Flags */
    build_append_int_noprefix(table_data, 0, 4);     /* Reserved */
}

static int build_all_acpi_generic_initiators(Object *obj, void *opaque)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    GArray *table_data = opaque;
    PCIDeviceHandle dev_handle;
    AcpiGenericNode *gn;
    Object *o;

    if (!object_dynamic_cast(obj, TYPE_ACPI_GENERIC_NODE)) {
        return 0;
    }

    gn = ACPI_GENERIC_NODE(obj);

    if (object_dynamic_cast(OBJECT(gn), TYPE_ACPI_GENERIC_INITIATOR)) {
        PCIDevice *pci_dev;

        if (gn->node >= ms->numa_state->num_nodes) {
            error_printf("%s: Specified node %d is invalid.\n",
                         TYPE_ACPI_GENERIC_INITIATOR, gn->node);
            exit(1);
        }

        o = object_resolve_path_type(gn->pci_dev, TYPE_PCI_DEVICE, NULL);
        if (!o) {
            error_printf("%s: Specified device must be a PCI device.\n",
                         TYPE_ACPI_GENERIC_INITIATOR);
            exit(1);
        }
        pci_dev = PCI_DEVICE(o);

        dev_handle.segment = 0;
        dev_handle.bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pci_dev)),
                                       pci_dev->devfn);
        build_srat_generic_node_affinity(table_data,
                                         gn->node, &dev_handle, false, true);

        return 0;
    } else { /* TYPE_ACPI_GENERIC_PORT */
        PCIBus *bus;
        const char *hid = "ACPI0016";

        if (gn->node >= ms->numa_state->num_nodes) {
            error_printf("%s: Specified node %d is invalid.\n",
                         TYPE_ACPI_GENERIC_PORT, gn->node);
            exit(1);
        }

        o = object_resolve_path_type(gn->pci_dev, TYPE_PCI_BUS, NULL);
        if (!o) {
            error_printf("%s: Specified device must be a PCI Host Bridge.\n",
                         TYPE_ACPI_GENERIC_PORT);
            exit(1);
        }
        bus = PCI_BUS(o);
        /* Need to know if this is a PXB Bus so below an expander bridge */
        if (!object_dynamic_cast(OBJECT(bus), TYPE_PXB_CXL_BUS)) {
            error_printf("%s: Specified device is not a bus below a host bridge.\n",
                         TYPE_ACPI_GENERIC_PORT);
            exit(1);
        }
        /* Copy without trailing NULL */
        memcpy(&dev_handle.hid, hid, sizeof(dev_handle.hid));
        dev_handle.uid = pci_bus_num(bus);
        build_srat_generic_node_affinity(table_data,
                                         gn->node, &dev_handle, true, false);

        return 0;
    }
}

void build_srat_generic_pci_initiator(GArray *table_data)
{
    object_child_foreach_recursive(object_get_root(),
                                   build_all_acpi_generic_initiators,
                                   table_data);
}
