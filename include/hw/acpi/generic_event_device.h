/*
 *
 * Copyright (c) 2018 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ACPI_GED_H
#define HW_ACPI_GED_H

#include "hw/acpi/memory_hotplug.h"

#define TYPE_VIRT_ACPI "virt-acpi"
#define VIRT_ACPI(obj) \
    OBJECT_CHECK(VirtAcpiState, (obj), TYPE_VIRT_ACPI)

typedef struct VirtAcpiState {
    SysBusDevice parent_obj;
    MemHotplugState memhp_state;
    hwaddr memhp_base;
} VirtAcpiState;


#endif
