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

static int acpi_generic_initiator_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_ACPI_GENERIC_INITIATOR)) {
        *list = g_slist_append(*list, ACPI_GENERIC_INITIATOR(obj));
    }

    object_child_foreach(obj, acpi_generic_initiator_list, opaque);
    return 0;
}

/*
 * Identify Generic Initiator objects and link them into the list which is
 * returned to the caller.
 *
 * Note: it is the caller's responsibility to free the list to avoid
 * memory leak.
 */
static GSList *acpi_generic_initiator_get_list(void)
{
    GSList *list = NULL;

    object_child_foreach(object_get_root(),
                         acpi_generic_initiator_list, &list);
    return list;
}

/*
 * ACPI 6.3:
 * Table 5-78 Generic Initiator Affinity Structure
 */
static void
build_srat_generic_pci_initiator_affinity(GArray *table_data, int node,
                                          PCIDeviceHandle *handle)
{
    uint8_t index;

    build_append_int_noprefix(table_data, 5, 1);  /* Type */
    build_append_int_noprefix(table_data, 32, 1); /* Length */
    build_append_int_noprefix(table_data, 0, 1);  /* Reserved */
    build_append_int_noprefix(table_data, 1, 1);  /* Device Handle Type: PCI */
    build_append_int_noprefix(table_data, node, 4);  /* Proximity Domain */

    /* Device Handle - PCI */
    build_append_int_noprefix(table_data, handle->segment, 2);
    build_append_int_noprefix(table_data, handle->bdf, 2);
    for (index = 0; index < 12; index++) {
        build_append_int_noprefix(table_data, 0, 1);
    }

    build_append_int_noprefix(table_data, GEN_AFFINITY_ENABLED, 4); /* Flags */
    build_append_int_noprefix(table_data, 0, 4);     /* Reserved */
}

void build_srat_generic_pci_initiator(GArray *table_data)
{
    GSList *gi_list, *list = acpi_generic_initiator_get_list();
    AcpiGenericInitiator *gi;

    for (gi_list = list; gi_list; gi_list = gi_list->next) {
        Object *o;
        uint16_t node;
        PCIDevice *pci_dev;
        bool node_specified = false;

        gi = gi_list->data;

        o = object_resolve_path_type(gi->pci_dev, TYPE_PCI_DEVICE, NULL);
        if (!o) {
            error_printf("Specified device must be a PCI device.\n");
            exit(1);
        }
        pci_dev = PCI_DEVICE(o);

        for (node = 0; (node = find_next_bit(gi->host_nodes,
                             MAX_NODES, node)) != MAX_NODES; node++)
        {
            PCIDeviceHandle dev_handle;
            dev_handle.segment = 0;
            dev_handle.bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pci_dev)),
                                                       pci_dev->devfn);
            build_srat_generic_pci_initiator_affinity(table_data,
                                                      node, &dev_handle);
            node_specified = true;
        }

        if (!node_specified) {
            error_report("Generic Initiator device 0:%x:%x.%x has no associated"
                         " NUMA node.", pci_bus_num(pci_get_bus(pci_dev)),
                         PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
            error_printf("Specify NUMA node with -host-nodes option.\n");
            exit(1);
        }
    }

    g_slist_free(list);
}
