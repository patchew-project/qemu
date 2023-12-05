/*
 * Control method devices
 *
 * Copyright (C) 2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */


#ifndef HW_ACPI_CONTROL_METHOD_DEVICE_H
#define HW_ACPI_CONTROL_NETHOD_DEVICE_H

#define ACPI_SLEEP_BUTTON_DEVICE "SLPB"

void acpi_dsdt_add_sleep_button(Aml *scope);
void acpi_dsdt_add_sleep_gpe_event_handler(Aml *scope);

#endif
