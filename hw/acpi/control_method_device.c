/*
 * Control method devices
 *
 * Copyright (C) 2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/acpi/control_method_device.h"
#include "hw/mem/nvdimm.h"

void acpi_dsdt_add_sleep_button(Aml *scope)
{
    Aml *dev = aml_device("\\_SB."ACPI_SLEEP_BUTTON_DEVICE);
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0C0E")));
    Aml *pkg = aml_package(2);
    aml_append(pkg, aml_int(0x01));
    aml_append(pkg, aml_int(0x04));
    aml_append(dev, aml_name_decl("_PRW", pkg));
    aml_append(dev, aml_operation_region("\\Boo", AML_SYSTEM_IO,
                                         aml_int(0x201), 0x1));
    Aml *field = aml_field("\\Boo", AML_BYTE_ACC, AML_NOLOCK,
                           AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("SBP", 1));
    aml_append(field, aml_named_field("SBW", 1));
    aml_append(dev, field);
    aml_append(scope, dev);
}

void acpi_dsdt_add_sleep_gpe_event_handler(Aml *scope)
{
     Aml *method = aml_method("_L07", 0, AML_NOTSERIALIZED);
     Aml *condition = aml_if(aml_name("\\_SB.SLPB.SBP"));
     aml_append(condition, aml_store(aml_int(1), aml_name("\\_SB.SLPB.SBP")));
     aml_append(condition,
                aml_notify(aml_name("\\_SB."ACPI_SLEEP_BUTTON_DEVICE),
                                    aml_int(0x80)));
     aml_append(method, condition);
     condition = aml_if(aml_name("\\_SB.SLPB.SBW"));
     aml_append(condition, aml_store(aml_int(1), aml_name("\\_SB.SLPB.SBW")));
     aml_append(condition,
                aml_notify(aml_name("\\_SB."ACPI_SLEEP_BUTTON_DEVICE),
                                    aml_int(0x2)));
     aml_append(method, condition);
     aml_append(scope, method);
}
