/*
 * PLX PEX PCIe Virtual Switch
 *
 * Copyright 2024 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef HW_PCI_BRIDGE_PLX_VSWITCH
#define HW_PCI_BRIDGE_PLX_VSWITCH

#define PLX_VSWITCH_DOWNSTREAM "plx-vswitch-downstream"
#define PLX_VSWITCH_UPSTREAM "plx-vswitch-upstream"

#define PLX_VSWITCH_MSI_OFFSET              0x70
#define PLX_VSWITCH_MSI_SUPPORTED_FLAGS     PCI_MSI_FLAGS_64BIT
#define PLX_VSWITCH_MSI_NR_VECTOR           1
#define PLX_VSWITCH_SSVID_OFFSET            0x80
#define PLX_VSWITCH_EXP_OFFSET              0x90
#define PLX_VSWITCH_AER_OFFSET              0x100

typedef struct PlxVSwitchPci {
    PCIDevice parent;

    /* PCI config properties */
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint32_t class_revision;
} PlxVSwitchPci;

#endif
