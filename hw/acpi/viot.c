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

/* Build PCI range for a given PCI host bridge */
static int viot_host_bridges(Object *obj, void *opaque)
{
    GArray *pci_range_blob = opaque;

    if (object_dynamic_cast(obj, TYPE_PCI_HOST_BRIDGE)) {
        PCIBus *bus = PCI_HOST_BRIDGE(obj)->bus;

        if (bus && !pci_bus_bypass_iommu(bus)) {
            int min_bus, max_bus;

            pci_bus_range(bus, &min_bus, &max_bus);

            AcpiViotPciRange pci_range = {
                .type = ACPI_VIOT_NODE_PCI_RANGE,
                .length = cpu_to_le16(sizeof(pci_range)),
                .bdf_start = cpu_to_le16(PCI_BUILD_BDF(min_bus, 0)),
                .bdf_end = cpu_to_le16(PCI_BUILD_BDF(max_bus, 0xff)),
                .endpoint_start = cpu_to_le32(PCI_BUILD_BDF(min_bus, 0)),
            };

            g_array_append_val(pci_range_blob, pci_range);
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
    int i;
    AcpiViot *viot;
    AcpiViotPciRange *pci_range;
    AcpiViotVirtioIommuPci *viommu;
    int viommu_off = sizeof(*viot);
    int viot_start = table_data->len;
    GArray *pci_ranges = g_array_new(false, true, sizeof(*pci_range));

    viot = acpi_data_push(table_data, sizeof(*viot));
    viot->node_offset = cpu_to_le16(viommu_off);

    viommu = acpi_data_push(table_data, sizeof(*viommu));
    viommu->type = ACPI_VIOT_NODE_VIRTIO_IOMMU_PCI;
    viommu->length = cpu_to_le16(sizeof(*viommu));
    viommu->bdf = cpu_to_le16(virtio_iommu_bdf);

    /* Build the list of PCI ranges that this viommu manages */
    object_child_foreach_recursive(object_get_root(), viot_host_bridges,
                                   pci_ranges);

    for (i = 0; i < pci_ranges->len; i++) {
        pci_range = &g_array_index(pci_ranges, AcpiViotPciRange, i);
        pci_range->output_node = cpu_to_le16(viommu_off);
    }
    viot->node_count = cpu_to_le16(pci_ranges->len + 1);

    g_array_append_vals(table_data, pci_ranges->data,
                        pci_ranges->len * sizeof(*pci_range));
    g_array_free(pci_ranges, true);

    build_header(linker, table_data, (void *)(table_data->data + viot_start),
                 "VIOT", table_data->len - viot_start, 0, oem_id, oem_table_id);
}

