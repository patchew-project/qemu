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


#ifndef HW_ACPI_CONTROL_METHOD_DEVICE_H
#define HW_ACPI_CONTROL_NETHOD_DEVICE_H

#define ACPI_SLEEP_BUTTON_DEVICE "SLPB"

void acpi_dsdt_add_sleep_button(Aml *scope);

#endif
