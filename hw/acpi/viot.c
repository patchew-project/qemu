/*
 * ACPI Virtual I/O Translation table implementation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/viot.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"

struct viot_pci_ranges {
    GArray *blob;
    size_t count;
    uint16_t output_node;
};

/* Build PCI range for a given PCI host bridge */
static int viot_host_bridges(Object *obj, void *opaque)
{
    struct viot_pci_ranges *pci_ranges = opaque;
    GArray *blob = pci_ranges->blob;

    if (object_dynamic_cast(obj, TYPE_PCI_HOST_BRIDGE)) {
        PCIBus *bus = PCI_HOST_BRIDGE(obj)->bus;

        if (bus && !pci_bus_bypass_iommu(bus)) {
            int min_bus, max_bus;

            pci_bus_range(bus, &min_bus, &max_bus);

            /* Type */
            build_append_int_noprefix(blob, ACPI_VIOT_NODE_PCI_RANGE, 1);
            /* Reserved */
            build_append_int_noprefix(blob, 0, 1);
            /* Length */
            build_append_int_noprefix(blob, sizeof(AcpiViotPciRange), 2);
            /* Endpoint start */
            build_append_int_noprefix(blob, PCI_BUILD_BDF(min_bus, 0), 4);
            /* PCI Segment start */
            build_append_int_noprefix(blob, 0, 2);
            /* PCI Segment end */
            build_append_int_noprefix(blob, 0, 2);
            /* PCI BDF start */
            build_append_int_noprefix(blob, PCI_BUILD_BDF(min_bus, 0), 2);
            /* PCI BDF end */
            build_append_int_noprefix(blob, PCI_BUILD_BDF(max_bus, 0xff), 2);
            /* Output node */
            build_append_int_noprefix(blob, pci_ranges->output_node, 2);
            /* Reserved */
            build_append_int_noprefix(blob, 0, 6);

            pci_ranges->count++;
        }
    }

    return 0;
}

/*
 * Generate a VIOT table with one PCI-based virtio-iommu that manages PCI
 * endpoints.
 */
void build_viot(GArray *table_data, BIOSLinker *linker,
                uint16_t virtio_iommu_bdf, const char *oem_id,
                const char *oem_table_id)
{
    /* virtio-iommu node follows the header */
    int viommu_off = sizeof(AcpiViot);
    int viot_start = table_data->len;
    struct viot_pci_ranges pci_ranges = {
        .output_node = viommu_off,
        .blob = g_array_new(false, true, 1),
    };

    /* Build the list of PCI ranges that this viommu manages */
    object_child_foreach_recursive(object_get_root(), viot_host_bridges,
                                   &pci_ranges);

    /* VIOT header */
    acpi_data_push(table_data, sizeof(AcpiTableHeader));
    /* Node count */
    build_append_int_noprefix(table_data, pci_ranges.count + 1, 2);
    /* Node offset */
    build_append_int_noprefix(table_data, viommu_off, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 8);

    /* Virtio-iommu based on virtio-pci */
    /* Type */
    build_append_int_noprefix(table_data, ACPI_VIOT_NODE_VIRTIO_IOMMU_PCI, 1);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 1);
    /* Length */
    build_append_int_noprefix(table_data, sizeof(AcpiViotVirtioIommuPci), 2);
    /* PCI Segment */
    build_append_int_noprefix(table_data, 0, 2);
    /* PCI BDF number */
    build_append_int_noprefix(table_data, virtio_iommu_bdf, 2);
    /* Reserved */
    build_append_int_noprefix(table_data, 0, 8);

    /* PCI ranges found above */
    g_array_append_vals(table_data, pci_ranges.blob->data,
                        pci_ranges.blob->len);
    g_array_free(pci_ranges.blob, true);

    build_header(linker, table_data, (void *)(table_data->data + viot_start),
                 "VIOT", table_data->len - viot_start, 0, oem_id, oem_table_id);
}

