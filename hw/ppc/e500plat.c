/*
 * Generic device-tree-driven paravirt PPC e500 platform
 *
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "e500.h"
#include "hw/net/fsl_etsec/etsec.h"
#include "hw/boards.h"
#include "sysemu/device_tree.h"
#include "sysemu/kvm.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/ppc/openpic.h"
#include "kvm_ppc.h"

static void e500plat_fixup_devtree(PPCE500Params *params, void *fdt)
{
    const char model[] = "QEMU ppce500";
    const char compatible[] = "fsl,qemu-e500";

    qemu_fdt_setprop(fdt, "/", "model", model, sizeof(model));
    qemu_fdt_setprop(fdt, "/", "compatible", compatible,
                     sizeof(compatible));
}

static void e500plat_init(MachineState *machine)
{
    PPCE500Params params = {
        .pci_first_slot = 0x1,
        .pci_nr_slots = PCI_SLOT_MAX - 1,
        .fixup_devtree = e500plat_fixup_devtree,
        .mpic_version = OPENPIC_MODEL_FSL_MPIC_42,
        .has_mpc8xxx_gpio = true,
        .has_platform_bus = true,
        .platform_bus_base = 0xf00000000ULL,
        .platform_bus_size = (128ULL * 1024 * 1024),
        .platform_bus_first_irq = 5,
        .platform_bus_num_irqs = 10,
        .ccsrbar_base = 0xFE0000000ULL,
        .pci_pio_base = 0xFE1000000ULL,
        .pci_mmio_base = 0xC00000000ULL,
        .pci_mmio_bus_base = 0xE0000000ULL,
        .spin_base = 0xFEF000000ULL,
    };

    /* Older KVM versions don't support EPR which breaks guests when we announce
       MPIC variants that support EPR. Revert to an older one for those */
    if (kvm_enabled() && !kvmppc_has_cap_epr()) {
        params.mpic_version = OPENPIC_MODEL_FSL_MPIC_20;
    }

    ppce500_init(machine, &params);
}

typedef struct {
    MachineClass parent;
    HotplugHandler *(*get_hotplug_handler)(MachineState *machine,
                                           DeviceState *dev);
} E500PlatMachineClass;

#define TYPE_E500PLAT_MACHINE  MACHINE_TYPE_NAME("ppce500")
#define E500PLAT_MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(E500PlatMachineClass, obj, TYPE_E500PLAT_MACHINE)
#define E500PLAT_MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(E500PlatMachineClass, klass, TYPE_E500PLAT_MACHINE)

static void e500plat_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                            DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_SYS_BUS_DEVICE)) {
        ppce500_plug_dynamic_sysbus_device(SYS_BUS_DEVICE(dev));
    }
}

static
HotplugHandler *e500plat_machine_get_hotpug_handler(MachineState *machine,
                                                    DeviceState *dev)
{
    E500PlatMachineClass *emc = E500PLAT_MACHINE_GET_CLASS(machine);
    if (object_dynamic_cast(OBJECT(dev), TYPE_SYS_BUS_DEVICE)) {
        return HOTPLUG_HANDLER(machine);
    }

    return emc->get_hotplug_handler ?
        emc->get_hotplug_handler(machine, dev) : NULL;
}

static void e500plat_machine_class_init(ObjectClass *oc, void *data)
{
    E500PlatMachineClass *emc = E500PLAT_MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    emc->get_hotplug_handler = mc->get_hotplug_handler;
    mc->get_hotplug_handler = e500plat_machine_get_hotpug_handler;
    hc->plug = e500plat_machine_device_plug_cb;

    mc->desc = "generic paravirt e500 platform";
    mc->init = e500plat_init;
    mc->max_cpus = 32;
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_ETSEC_COMMON);
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("e500v2_v30");
}

static const TypeInfo e500plat_info = {
    .name          = TYPE_E500PLAT_MACHINE,
    .parent        = TYPE_MACHINE,
    .class_size    = sizeof(E500PlatMachineClass),
    .class_init    = e500plat_machine_class_init,
    .interfaces    = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    }
};

static void e500plat_register_types(void)
{
    type_register_static(&e500plat_info);
}
type_init(e500plat_register_types)
