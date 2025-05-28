/*
 * Control Method Device
 *
 * Copyright (c) 2023 Oracle and/or its affiliates.
 *
 *
 * Authors:
 *     Annie Li <annie.li@oracle.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/acpi/control_method_device.h"
#include "hw/acpi/aml-build.h"

/*
 * The control method sleep button[ACPI v6.5 Section 4.8.2.2.2.2]
 * resides in generic hardware address spaces. The sleep button
 * is defined as _HID("PNP0C0E") that associates with device "SLPB".
 */
void acpi_dsdt_add_sleep_button(Aml *scope)
{
    Aml *dev = aml_device(ACPI_SLEEP_BUTTON_DEVICE);
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0C0E")));
    /*
     * No _PRW, the sleep button device is always tied to GPE L07
     * event handler for x86 platform, or a GED event for other
     * platforms such as virt, ARM, microvm, etc.
     */
    aml_append(dev, aml_operation_region("\\SLP", AML_SYSTEM_IO,
                                         aml_int(0x201), 0x1));
    Aml *field = aml_field("\\SLP", AML_BYTE_ACC, AML_NOLOCK,
                           AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("SBP", 1));
    aml_append(dev, field);
    aml_append(scope, dev);
}
