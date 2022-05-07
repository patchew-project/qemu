/*
 * QEMU PowerPC PowerNV Unified PHB model
 *
 * Copyright (c) 2022, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "hw/pci-host/pnv_phb.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/ppc/pnv.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"


static char *pnv_phb_get_chip_typename(void)
{
    Object *qdev_machine = qdev_get_machine();
    PnvMachineState *pnv = PNV_MACHINE(qdev_machine);
    MachineState *machine = MACHINE(pnv);
    g_autofree char *chip_typename = NULL;
    int i;

    if (!machine->cpu_type) {
        return NULL;
    }

    i = strlen(machine->cpu_type) - strlen(POWERPC_CPU_TYPE_SUFFIX);
    chip_typename = g_strdup_printf(PNV_CHIP_TYPE_NAME("%.*s"),
                                    i, machine->cpu_type);

    return g_steal_pointer(&chip_typename);
}

static void pnv_phb_instance_init(Object *obj)
{
    g_autofree char *chip_typename = pnv_phb_get_chip_typename();

    /*
     * When doing command line instrospection we won't have
     * a valid machine->cpu_type value.
     */
    if (!chip_typename) {
        return;
    }

    if (!strcmp(chip_typename, TYPE_PNV_CHIP_POWER8) ||
        !strcmp(chip_typename, TYPE_PNV_CHIP_POWER8E) ||
        !strcmp(chip_typename, TYPE_PNV_CHIP_POWER8NVL)) {
        pnv_phb3_instance_init(obj);
    }
}

static void pnv_phb_realize(DeviceState *dev, Error **errp)
{
    g_autofree char *chip_typename = pnv_phb_get_chip_typename();

    g_assert(chip_typename != NULL);

    if (!strcmp(chip_typename, TYPE_PNV_CHIP_POWER8) ||
        !strcmp(chip_typename, TYPE_PNV_CHIP_POWER8E) ||
        !strcmp(chip_typename, TYPE_PNV_CHIP_POWER8NVL)) {
        /* PnvPHB3 */
        pnv_phb3_realize(dev, errp);
    }
}

static const char *pnv_phb_root_bus_path(PCIHostState *host_bridge,
                                          PCIBus *rootbus)
{
    PnvPHB *phb = PNV_PHB(host_bridge);

    snprintf(phb->bus_path, sizeof(phb->bus_path), "00%02x:%02x",
             phb->chip_id, phb->phb_id);
    return phb->bus_path;
}

static Property pnv_phb_properties[] = {
    DEFINE_PROP_UINT32("index", PnvPHB, phb_id, 0),
    DEFINE_PROP_UINT32("chip-id", PnvPHB, chip_id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_phb_class_init(ObjectClass *klass, void *data)
{
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    hc->root_bus_path = pnv_phb_root_bus_path;
    dc->realize = pnv_phb_realize;
    device_class_set_props(dc, pnv_phb_properties);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->user_creatable = true;
}

static const TypeInfo pnv_phb_type_info = {
    .name          = TYPE_PNV_PHB,
    .parent        = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(PnvPHB),
    .class_init    = pnv_phb_class_init,
    .instance_init = pnv_phb_instance_init,
};

static void pnv_phb_register_types(void)
{
    type_register_static(&pnv_phb_type_info);
}

type_init(pnv_phb_register_types)
