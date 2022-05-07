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


#define PNV_MACHINE_POWER8    1
#define PNV_MACHINE_POWER9    2
#define PNV_MACHINE_POWER10   3

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

static int pnv_phb_get_current_machine(void)
{
    g_autofree char *chip_typename = pnv_phb_get_chip_typename();

    /*
     * When doing command line instrospection we won't have
     * a valid machine->cpu_type value.
     */
    if (!chip_typename) {
        return 0;
    }

    if (!strcmp(chip_typename, TYPE_PNV_CHIP_POWER8) ||
        !strcmp(chip_typename, TYPE_PNV_CHIP_POWER8E) ||
        !strcmp(chip_typename, TYPE_PNV_CHIP_POWER8NVL)) {
        return PNV_MACHINE_POWER8;
    } else if (!strcmp(chip_typename, TYPE_PNV_CHIP_POWER9)) {
        return PNV_MACHINE_POWER9;
    } else if (!strcmp(chip_typename, TYPE_PNV_CHIP_POWER10)) {
        return PNV_MACHINE_POWER10;
    }

    return 0;
}

static void pnv_phb_instance_init(Object *obj)
{
    int pnv_current_machine = pnv_phb_get_current_machine();

    if (pnv_current_machine == 0) {
        return;
    }

    if (pnv_current_machine == PNV_MACHINE_POWER8) {
        pnv_phb3_instance_init(obj);
        return;
    }

    pnv_phb4_instance_init(obj);
}

static void pnv_phb_realize(DeviceState *dev, Error **errp)
{
    int pnv_current_machine = pnv_phb_get_current_machine();
    PnvPHB *phb = PNV_PHB(dev);

    g_assert(pnv_current_machine != 0);

    if (pnv_current_machine == PNV_MACHINE_POWER8) {
        /* PnvPHB3 */
        phb->version = PHB_VERSION_3;
        pnv_phb3_realize(dev, errp);
        return;
    }

    if (pnv_current_machine == PNV_MACHINE_POWER9) {
        phb->version = PHB_VERSION_4;
    } else if (pnv_current_machine == PNV_MACHINE_POWER10) {
        phb->version = PHB_VERSION_5;
    } else {
        g_autofree char *chip_typename = pnv_phb_get_chip_typename();
        error_setg(errp, "unknown PNV chip: %s", chip_typename);
        return;
    }

    pnv_phb4_realize(dev, errp);
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
    DEFINE_PROP_LINK("chip", PnvPHB, chip, TYPE_PNV_CHIP, PnvChip *),
    DEFINE_PROP_LINK("pec", PnvPHB, pec, TYPE_PNV_PHB4_PEC,
                     PnvPhb4PecState *),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_phb_class_init(ObjectClass *klass, void *data)
{
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveNotifierClass *xfc = XIVE_NOTIFIER_CLASS(klass);

    hc->root_bus_path = pnv_phb_root_bus_path;
    dc->realize = pnv_phb_realize;
    device_class_set_props(dc, pnv_phb_properties);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->user_creatable = true;

    xfc->notify         = pnv_phb4_xive_notify;
}

static const TypeInfo pnv_phb_type_info = {
    .name          = TYPE_PNV_PHB,
    .parent        = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(PnvPHB),
    .class_init    = pnv_phb_class_init,
    .instance_init = pnv_phb_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_XIVE_NOTIFIER },
        { },
    },
};

static void pnv_phb_root_port_reset(DeviceState *dev)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    PCIDevice *d = PCI_DEVICE(dev);
    uint8_t *conf = d->config;
    int pnv_current_machine = pnv_phb_get_current_machine();

    rpc->parent_reset(dev);

    if (pnv_current_machine == PNV_MACHINE_POWER8) {
        return;
    }

    pci_byte_test_and_set_mask(conf + PCI_IO_BASE,
                               PCI_IO_RANGE_MASK & 0xff);
    pci_byte_test_and_clear_mask(conf + PCI_IO_LIMIT,
                                 PCI_IO_RANGE_MASK & 0xff);
    pci_set_word(conf + PCI_MEMORY_BASE, 0);
    pci_set_word(conf + PCI_MEMORY_LIMIT, 0xfff0);
    pci_set_word(conf + PCI_PREF_MEMORY_BASE, 0x1);
    pci_set_word(conf + PCI_PREF_MEMORY_LIMIT, 0xfff1);
    pci_set_long(conf + PCI_PREF_BASE_UPPER32, 0x1); /* Hack */
    pci_set_long(conf + PCI_PREF_LIMIT_UPPER32, 0xffffffff);
    pci_config_set_interrupt_pin(conf, 0);
}

static void pnv_phb_root_port_realize(DeviceState *dev, Error **errp)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    PCIDevice *pci = PCI_DEVICE(dev);
    PCIBus *bus = pci_get_bus(pci);
    PnvPHB *phb = NULL;
    Error *local_err = NULL;

    phb = (PnvPHB *) object_dynamic_cast(OBJECT(bus->qbus.parent),
                                          TYPE_PNV_PHB);

    if (!phb) {
        error_setg(errp,
"pnv_phb_root_port devices must be connected to pnv-phb buses");
        return;
    }

    /* Set unique chassis/slot values for the root port */
    qdev_prop_set_uint8(&pci->qdev, "chassis", phb->chip_id);
    qdev_prop_set_uint16(&pci->qdev, "slot", phb->phb_id);

    rpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    pci_config_set_interrupt_pin(pci->config, 0);
}

static void pnv_phb_root_port_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);

    dc->desc     = "IBM PHB PCIE Root Port";

    device_class_set_parent_realize(dc, pnv_phb_root_port_realize,
                                    &rpc->parent_realize);

    device_class_set_parent_reset(dc, pnv_phb_root_port_reset,
                                  &rpc->parent_reset);
    dc->reset = &pnv_phb_root_port_reset;

    dc->user_creatable = true;

    k->vendor_id = PCI_VENDOR_ID_IBM;
    /*
     * k->device_id is defaulted to PNV_PHB3_DEVICE_ID. We'll fix
     * it during instance_init() when we are aware of what machine
     * we're running.
     */
    k->device_id = 0x03dc;
    k->revision  = 0;

    rpc->exp_offset = 0x48;
    rpc->aer_offset = 0x100;
}

static const TypeInfo pnv_phb_root_port_info = {
    .name          = TYPE_PNV_PHB_ROOT_PORT,
    .parent        = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(PnvPHBRootPort),
    .class_init    = pnv_phb_root_port_class_init,
};

static void pnv_phb_register_types(void)
{
    type_register_static(&pnv_phb_type_info);
    type_register_static(&pnv_phb_root_port_info);
}

type_init(pnv_phb_register_types)
