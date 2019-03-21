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

#define ACPI_GED_IRQ_SEL_OFFSET 0x0
#define ACPI_GED_IRQ_SEL_LEN    0x4
#define ACPI_GED_IRQ_SEL_MEM    0x1
#define ACPI_GED_REG_LEN        0x4

#define GED_DEVICE      "GED"
#define AML_GED_IRQ_REG "IREG"
#define AML_GED_IRQ_SEL "ISEL"

typedef enum {
    GED_MEMORY_HOTPLUG = 1,
} GedEventType;

typedef struct GedEvent {
    uint32_t     selector;
    GedEventType event;
} GedEvent;

typedef struct GEDState {
    MemoryRegion io;
    uint32_t     sel;
    uint32_t     irq;
    qemu_irq     *gsi;
    QemuMutex    lock;
} GEDState;

typedef struct VirtAcpiState {
    SysBusDevice parent_obj;
    MemHotplugState memhp_state;
    hwaddr memhp_base;
    void *gsi;
    hwaddr ged_base;
    GEDState ged_state;
    uint32_t ged_irq;
    void *ged_events;
    uint32_t ged_events_size;
} VirtAcpiState;

void build_ged_aml(Aml *table, const char* name, uint32_t ged_irq,
                   AmlRegionSpace rs);

#endif
