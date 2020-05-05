/*
 * x86 variant of the generic event device for hw reduced acpi
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/i386/pc.h"
#include "hw/irq.h"
#include "hw/mem/pc-dimm.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "sysemu/runstate.h"

typedef struct AcpiGedX86State {
    struct AcpiGedState parent_obj;
    MemoryRegion regs;
    Notifier powerdown_req;
} AcpiGedX86State;

static uint64_t acpi_ged_x86_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void acpi_ged_x86_regs_write(void *opaque, hwaddr addr, uint64_t data,
                                    unsigned int size)
{
    bool slp_en;
    int slp_typ;

    switch (addr) {
    case ACPI_GED_X86_REG_SLEEP_CTL:
        slp_typ = (data >> 2) & 0x07;
        slp_en  = (data >> 5) & 0x01;
        if (slp_en && slp_typ == 5) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
        return;
    case ACPI_GED_X86_REG_SLEEP_STS:
        return;
    case ACPI_GED_X86_REG_RESET:
        if (data == ACPI_GED_X86_RESET_VALUE) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
        return;
    }
}

static const MemoryRegionOps acpi_ged_x86_regs_ops = {
    .read = acpi_ged_x86_regs_read,
    .write = acpi_ged_x86_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void acpi_ged_x86_powerdown_req(Notifier *n, void *opaque)
{
    AcpiGedX86State *s = container_of(n, AcpiGedX86State, powerdown_req);
    AcpiDeviceIf *adev = ACPI_DEVICE_IF(s);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(OBJECT(s));

    adevc->send_event(adev, ACPI_POWER_DOWN_STATUS);
}

static void acpi_ged_x86_initfn(Object *obj)
{
    AcpiGedX86State *s = ACPI_GED_X86(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->regs, obj, &acpi_ged_x86_regs_ops,
                          obj, "acpi-regs", ACPI_GED_X86_REG_COUNT);
    sysbus_init_mmio(sbd, &s->regs);

    s->powerdown_req.notify = acpi_ged_x86_powerdown_req;
    qemu_register_powerdown_notifier(&s->powerdown_req);
}

static void acpi_ged_x86_class_init(ObjectClass *class, void *data)
{
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_CLASS(class);

    adevc->madt_cpu = pc_madt_cpu_entry;
}

static const TypeInfo acpi_ged_x86_info = {
    .name          = TYPE_ACPI_GED_X86,
    .parent        = TYPE_ACPI_GED,
    .instance_size = sizeof(AcpiGedX86State),
    .instance_init  = acpi_ged_x86_initfn,
    .class_init    = acpi_ged_x86_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { TYPE_ACPI_DEVICE_IF },
        { }
    }
};

static void acpi_ged_x86_register_types(void)
{
    type_register_static(&acpi_ged_x86_info);
}

type_init(acpi_ged_x86_register_types)
