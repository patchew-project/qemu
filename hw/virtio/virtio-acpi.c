/*
 * virtio ACPI Support
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/virtio/virtio-acpi.h"
#include "hw/acpi/aml-build.h"

void virtio_acpi_dsdt_add(Aml *scope, const hwaddr base, const hwaddr size,
                          uint32_t mmio_irq, long int start_index, int num)
{
    long int i;
    hwaddr virtio_base = base;

    for (i = start_index; i < start_index + num; i++) {
        uint32_t irq = mmio_irq + i;
        Aml *dev = aml_device("VR%02u", (unsigned)i);
        aml_append(dev, aml_name_decl("_HID", aml_string("LNRO0005")));
        aml_append(dev, aml_name_decl("_UID", aml_int(i)));
        aml_append(dev, aml_name_decl("_CCA", aml_int(1)));

        Aml *crs = aml_resource_template();
        aml_append(crs, aml_memory32_fixed(base, size, AML_READ_WRITE));
        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, &irq, 1));
        aml_append(dev, aml_name_decl("_CRS", crs));
        aml_append(scope, dev);
        virtio_base += size;
    }
}
