/*
 * BCM2838 PCIe Root Complex emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/pci-host/gpex.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "hw/arm/bcm2838_pcie.h"
#include "trace.h"

/*
 * RC root part (D0:F0)
 */

static void bcm2838_pcie_root_port_reset_hold(Object *obj)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(obj);
    PCIDevice *dev = PCI_DEVICE(obj);
    BCM2838PcieRootState *s = BCM2838_PCIE_ROOT(dev);

    if (rpc->parent_phases.hold) {
        rpc->parent_phases.hold(obj);
    }

    memset(s->regs, 0xFF, sizeof(s->regs));
}

static void bcm2838_pcie_root_init(Object *obj)
{
    PCIBridge *br = PCI_BRIDGE(obj);
    br->bus_name = "pcie.1";
}

static void bcm2838_pcie_root_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(class);

    dc->desc = "BCM2711 PCIe Bridge";
    /*
     * PCI-facing part of the host bridge, not usable without the host-facing
     * part, which can't be device_add'ed.
     */

    resettable_class_set_parent_phases(rc, NULL,
                                       bcm2838_pcie_root_port_reset_hold,
                                       NULL, &rpc->parent_phases);

    dc->user_creatable = false;
    k->vendor_id = BCM2838_PCIE_VENDOR_ID;
    k->device_id = BCM2838_PCIE_DEVICE_ID;
    k->revision = BCM2838_PCIE_REVISION;

    rpc->exp_offset = BCM2838_PCIE_EXP_CAP_OFFSET;
    rpc->aer_offset = BCM2838_PCIE_AER_CAP_OFFSET;
}

static const TypeInfo bcm2838_pcie_root_info = {
    .name = TYPE_BCM2838_PCIE_ROOT,
    .parent = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(BCM2838PcieRootState),
    .instance_init = bcm2838_pcie_root_init,
    .class_init = bcm2838_pcie_root_class_init,
};

static void bcm2838_pcie_register(void)
{
    type_register_static(&bcm2838_pcie_root_info);
}

type_init(bcm2838_pcie_register)
