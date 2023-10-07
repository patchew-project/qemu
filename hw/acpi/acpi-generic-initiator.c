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
static void build_srat_generic_initiator_affinity(GArray *table_data, int node,
                                                  PCIDeviceHandle *handle,
                                                  GenericAffinityFlags flags)
{
    build_append_int_noprefix(table_data, 5, 1);     /* Type */
    build_append_int_noprefix(table_data, 32, 1);    /* Length */
    build_append_int_noprefix(table_data, 0, 1);     /* Reserved */
    build_append_int_noprefix(table_data, 1, 1);     /* Device Handle Type */
    build_append_int_noprefix(table_data, node, 4);  /* Proximity Domain */
    build_append_int_noprefix(table_data, handle->segment, 2);
    build_append_int_noprefix(table_data, handle->bdf, 2);
    build_append_int_noprefix(table_data, handle->res0, 4);
    build_append_int_noprefix(table_data, handle->res1, 8);
    build_append_int_noprefix(table_data, flags, 4); /* Flags */
    build_append_int_noprefix(table_data, 0, 4);     /* Reserved */
}

void build_srat_generic_initiator(GArray *table_data)
{
    GSList *gi_list, *list = acpi_generic_initiator_get_list();
    for (gi_list = list; gi_list; gi_list = gi_list->next) {
        AcpiGenericInitiator *gi = gi_list->data;
        Object *o;
        int count;

        if (gi->node == MAX_NODES) {
            continue;
        }

        o = object_resolve_path_type(gi->device, TYPE_VFIO_PCI_NOHOTPLUG, NULL);
        if (!o) {
            continue;
        }

        for (count = 0; count < gi->node_count; count++) {
            PCIDeviceHandle dev_handle = {0};
            PCIDevice *pci_dev = PCI_DEVICE(o);

            dev_handle.bdf = pci_dev->devfn;
            build_srat_generic_initiator_affinity(table_data,
                                                  gi->node + count, &dev_handle,
                                                  GEN_AFFINITY_ENABLED);
        }
    }
    g_slist_free(list);
}

static void
nvidia_acpi_generic_initiator_set_node_start(Object *obj, Visitor *v,
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

static void
nvidia_acpi_generic_initiator_set_node_count(Object *obj, Visitor *v,
                                             const char *name, void *opaque,
                                             Error **errp)
{
    AcpiGenericInitiator *gi = ACPI_GENERIC_INITIATOR(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    gi->node_count = value;
}

static void nvidia_acpi_generic_initiator_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add(oc, NVIDIA_ACPI_GENERIC_INITIATOR_NODE_START_PROP,
                              "uint32", NULL,
                              nvidia_acpi_generic_initiator_set_node_start,
                              NULL, NULL);
    object_class_property_add(oc, NVIDIA_ACPI_GENERIC_INITIATOR_NODE_COUNT_PROP,
                              "uint32", NULL,
                              nvidia_acpi_generic_initiator_set_node_count,
                              NULL, NULL);
}

static const TypeInfo nvidia_acpi_generic_initiator_info = {
    .parent = TYPE_ACPI_GENERIC_INITIATOR,
    .name = TYPE_NVIDIA_ACPI_GENERIC_INITIATOR,
    .instance_size = sizeof(NvidiaAcpiGenericInitiator),
    .class_size = sizeof(NvidiaAcpiGenericInitiatorClass),
    .class_init = nvidia_acpi_generic_initiator_class_init,
};

static void
nvidia_acpi_generic_initiator_register_types(void)
{
    type_register_static(&nvidia_acpi_generic_initiator_info);
}
type_init(nvidia_acpi_generic_initiator_register_types);
