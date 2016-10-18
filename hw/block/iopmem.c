/*
 * QEMU iopmem Controller
 *
 * Copyright (c) 2016, Microsemi Corporation
 *
 * Written by Logan Gunthorpe <logang@deltatee.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */


/**
 * Usage: add options:
 *      -drive file=<file>,if=none,id=<drive_id>
 *      -device iopmem,drive=<drive_id>,id=<id[optional]>
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "sysemu/block-backend.h"

typedef struct IoPmemCtrl {
    PCIDevice    parent_obj;
    MemoryRegion iomem;
    BlockBackend *blk;
    uint64_t     size;
} IoPmemCtrl;

#define TYPE_IOPMEM "iopmem"
#define IOPMEM(obj) \
        OBJECT_CHECK(IoPmemCtrl, (obj), TYPE_IOPMEM)

static uint64_t iopmem_bar_read(void *opaque, hwaddr addr, unsigned size)
{
    IoPmemCtrl *ipm = (IoPmemCtrl *)opaque;
    uint64_t val;

    blk_pread(ipm->blk, addr, &val, size);

    return val;
}

static void iopmem_bar_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    IoPmemCtrl *ipm = (IoPmemCtrl *)opaque;

    if (addr & 3) {
        return;
    }

    blk_pwrite(ipm->blk, addr, &data, size, 0);
}

static const MemoryRegionOps iopmem_bar_ops = {
    .read = iopmem_bar_read,
    .write = iopmem_bar_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static int iopmem_init(PCIDevice *pci_dev)
{
    IoPmemCtrl *ipm = IOPMEM(pci_dev);

    if (!ipm->blk) {
        return -1;
    }

    ipm->size = blk_getlength(ipm->blk);

    pci_config_set_prog_interface(pci_dev->config, 0x2);
    pci_config_set_class(pci_dev->config, PCI_CLASS_STORAGE_OTHER);
    pcie_endpoint_cap_init(&ipm->parent_obj, 0x80);

    memory_region_init_io(&ipm->iomem, OBJECT(ipm), &iopmem_bar_ops, ipm,
                          "iopmem", ipm->size);

    pci_register_bar(&ipm->parent_obj, 4,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_PREFETCH |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &ipm->iomem);

    return 0;
}

static void iopmem_exit(PCIDevice *pci_dev)
{
    IoPmemCtrl *ipm = IOPMEM(pci_dev);

    if (ipm->blk) {
        blk_flush(ipm->blk);
    }
}

static Property iopmem_props[] = {
    DEFINE_PROP_DRIVE("drive", IoPmemCtrl, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription iopmem_vmstate = {
    .name = "iopmem",
    .unmigratable = 1,
};

static void iopmem_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->init = iopmem_init;
    pc->exit = iopmem_exit;
    pc->class_id = PCI_CLASS_STORAGE_OTHER;
    pc->vendor_id = PCI_VENDOR_ID_PMC_SIERRA;
    pc->device_id = 0xf115;
    pc->revision = 2;
    pc->is_express = 1;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Non-Volatile IO Memory Storage";
    dc->props = iopmem_props;
    dc->vmsd = &iopmem_vmstate;
}

static const TypeInfo iopmem_info = {
    .name          = "iopmem",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IoPmemCtrl),
    .class_init    = iopmem_class_init,
};

static void iopmem_register_types(void)
{
    type_register_static(&iopmem_info);
}

type_init(iopmem_register_types)
