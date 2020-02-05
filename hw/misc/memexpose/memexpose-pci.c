/*
 *  Memexpose PCI device
 *
 *  Copyright (C) 2020 Samsung Electronics Co Ltd.
 *    Igor Kotrasinski, <i.kotrasinsk@partner.samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "memexpose-core.h"

#define PCI_VENDOR_ID_MEMEXPOSE            PCI_VENDOR_ID_REDHAT_QUMRANET
#define TYPE_MEMEXPOSE_PCI "memexpose-pci"
#define PCI_DEVICE_ID_MEMEXPOSE     0x1111
#define MEMEXPOSE_PCI(obj) \
    OBJECT_CHECK(MemexposePCIState, (obj), TYPE_MEMEXPOSE_PCI)

typedef struct MemexposePCIState {
    PCIDevice parent_obj;

    CharBackend intr_chr;
    CharBackend mem_chr;

    MemexposeIntr intr;
    uint32_t intr_status;
    MemexposeMem mem;
} MemexposePCIState;

static void raise_irq(MemexposePCIState *s)
{
    s->intr_status |= 1;
    if (msi_enabled(&s->parent_obj)) {
        msi_notify(&s->parent_obj, 0);
    } else {
        pci_set_irq(&s->parent_obj, 1);
    }
}

static void lower_irq(MemexposePCIState *s)
{
    s->intr_status &= (~1);
    if (!s->intr_status && !msi_enabled(&s->parent_obj)) {
        pci_set_irq(&s->parent_obj, 0);
    }
}

static void handle_irq(void *opaque, int dir)
{
    MemexposePCIState *s = opaque;
    if (dir) {
        raise_irq(s);
    } else {
        lower_irq(s);
    }
}

static int memexpose_enable(void *opaque)
{
    int ret;
    MemexposePCIState *s = opaque;

    ret = memexpose_intr_enable(&s->intr);
    if (ret) {
        return ret;
    }

    ret = memexpose_mem_enable(&s->mem);
    if (ret) {
        memexpose_intr_disable(&s->intr);
        return ret;
    }

    return 0;
}

static void memexpose_disable(void *opaque)
{
    MemexposePCIState *s = opaque;

    memexpose_intr_disable(&s->intr);
    memexpose_mem_disable(&s->mem);
}

static void memexpose_pci_intr_init(PCIDevice *dev, Error **errp)
{
    MemexposePCIState *s = MEMEXPOSE_PCI(dev);
    struct memexpose_intr_ops ops;
    ops.intr = handle_irq;
    ops.enable = memexpose_enable;
    ops.disable = memexpose_disable;
    ops.parent = s;

    memexpose_intr_init(&s->intr, &ops, OBJECT(dev), &s->intr_chr, errp);
    if (*errp) {
        return;
    }

    s->intr_status = 0;
    uint8_t *pci_conf;
    pci_conf = dev->config;
    pci_conf[PCI_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEMORY;
    pci_config_set_interrupt_pin(pci_conf, 1);
    if (msi_init(dev, 0, 1, true, false, errp)) {
        error_setg(errp, "Failed to initialize memexpose PCI interrupts");
        memexpose_intr_destroy(&s->intr);
        return;
    }

    /* region for registers*/
    pci_register_bar(dev, 0,
                     PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &s->intr.shmem);
    MEMEXPOSE_DPRINTF("Initialized bar.\n");
}

static void memexpose_pci_intr_exit(PCIDevice *dev)
{
    MemexposePCIState *s = MEMEXPOSE_PCI(dev);
    msi_uninit(dev);
    memexpose_intr_destroy(&s->intr);
}

static void memexpose_pci_realize(PCIDevice *dev, Error **errp)
{
    MemexposePCIState *s = MEMEXPOSE_PCI(dev);
    memexpose_pci_intr_init(dev, errp);
    if (*errp) {
        return;
    }

    Chardev *chrd = qemu_chr_fe_get_driver(&s->mem_chr);
    assert(chrd);
    MEMEXPOSE_DPRINTF("Memexpose endpoint at %s!\n",
                      chrd->filename);
    memexpose_mem_init(&s->mem, OBJECT(dev),
                       get_system_memory(),
                       &s->mem_chr, 0, errp);
    if (*errp) {
        memexpose_pci_intr_exit(dev);
        return;
    }

    pci_register_bar(dev, 1,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->mem.shmem);
    MEMEXPOSE_DPRINTF("Initialized second bar.\n");
}

static void memexpose_pci_exit(PCIDevice *dev)
{
    MemexposePCIState *s = MEMEXPOSE_PCI(dev);
    memexpose_mem_destroy(&s->mem);
    memexpose_pci_intr_exit(dev);
}

static Property memexpose_pci_properties[] = {
    DEFINE_PROP_CHR("mem_chardev", MemexposePCIState, mem_chr),
    DEFINE_PROP_CHR("intr_chardev", MemexposePCIState, intr_chr),
    DEFINE_PROP_UINT64("shm_size", MemexposePCIState, mem.shmem_size, 4096),
    DEFINE_PROP_END_OF_LIST(),
};

static void memexpose_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = memexpose_pci_realize;
    k->exit = memexpose_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_MEMEXPOSE;
    k->device_id = PCI_DEVICE_ID_MEMEXPOSE;
    k->class_id = PCI_CLASS_MEMORY_RAM;
    k->revision = 1;
    device_class_set_props(dc, memexpose_pci_properties);
}

static const TypeInfo memexpose_pci_info = {
    .name          = TYPE_MEMEXPOSE_PCI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MemexposePCIState),
    .class_init    = memexpose_pci_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};


static void memexpose_pci_register_types(void)
{
    type_register_static(&memexpose_pci_info);
}

type_init(memexpose_pci_register_types)
