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

    object_child_foreach(object_get_root(), acpi_generic_initiator_list, &list);
    return list;
}

/*
 * ACPI spec, Revision 6.5
 * 5.2.16.6 Generic Initiator Affinity Structure
 */
static
void build_srat_generic_pci_initiator_affinity(GArray *table_data, int node,
                                               PCIDeviceHandle *handle)
{
    uint8_t index;

    build_append_int_noprefix(table_data, 5, 1);     /* Type */
    build_append_int_noprefix(table_data, 32, 1);    /* Length */
    build_append_int_noprefix(table_data, 0, 1);     /* Reserved */
    build_append_int_noprefix(table_data, 1, 1);     /* Device Handle Type */
    build_append_int_noprefix(table_data, node, 4);  /* Proximity Domain */
    build_append_int_noprefix(table_data, handle->segment, 2);
    build_append_int_noprefix(table_data, handle->bdf, 2);

    /* Reserved */
    for (index = 0; index < 12; index++) {
        build_append_int_noprefix(table_data, handle->res[index], 1);
    }

    build_append_int_noprefix(table_data, GEN_AFFINITY_ENABLED, 4); /* Flags */
    build_append_int_noprefix(table_data, 0, 4);     /* Reserved */
}

void build_srat_generic_pci_initiator(GArray *table_data)
{
    GSList *gi_list, *list = acpi_generic_initiator_get_list();
    for (gi_list = list; gi_list; gi_list = gi_list->next) {
        AcpiGenericInitiator *gi = gi_list->data;
        Object *o;
        uint16List *l;

        o = object_resolve_path_type(gi->device, TYPE_VFIO_PCI, NULL);
        if (!o) {
            continue;
        }

        for (l = gi->nodelist; l; l = l->next) {
            PCIDeviceHandle dev_handle = {0};
            PCIDevice *pci_dev = PCI_DEVICE(o);
            dev_handle.bdf = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pci_dev)),
                                                       pci_dev->devfn);
            build_srat_generic_pci_initiator_affinity(table_data,
                                                      l->value, &dev_handle);
        }
    }
    g_slist_free(list);
}
