/*
 * QEMU MEN Chameleon Bus emulation
 *
 * Copyright (C) 2023 Johannes Thumshirn <jth@kernel.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/mcb/mcb.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

typedef struct {
    uint8_t revision;
    char model;
    uint8_t minor;
    uint8_t bus_type;
    uint16_t magic;
    uint16_t reserved;
    /* This one has no '\0' at the end!!! */
    char filename[12];
} ChameleonFPGAHeader;
#define CHAMELEON_BUS_TYPE_WISHBONE 0
#define CHAMELEONV2_MAGIC 0xabce

typedef struct {
    PCIDevice dev;
    MCBus bus;
    MemoryRegion ctbl;
    uint16_t status;
    uint8_t int_set;
    ChameleonFPGAHeader *header;

    uint8_t minor;
    uint8_t rev;
    uint8_t model;
} MPCIState;

#define TYPE_MCB_PCI "mcb-pci"

#define MPCI(obj)                                       \
    OBJECT_CHECK(MPCIState, (obj), TYPE_MCB_PCI)

#define CHAMELEON_TABLE_SIZE 0x200
#define N_MODULES 32

#define PCI_VENDOR_ID_MEN 0x1a88
#define PCI_DEVICE_ID_MEN_MCBPCI 0x4d45

static uint32_t read_header(MPCIState *s, hwaddr addr)
{
    uint32_t ret = 0;
    ChameleonFPGAHeader *header = s->header;

    switch (addr >> 2) {
    case 0:
        ret |= header->revision;
        ret |= header->model << 8;
        ret |= header->minor << 16;
        ret |= header->bus_type << 24;
        break;
    case 1:
        ret |= header->magic;
        ret |= header->reserved << 16;
        break;
    case 2:
        memcpy(&ret, header->filename, sizeof(uint32_t));
        break;
    case 3:
        memcpy(&ret, header->filename + sizeof(uint32_t),
               sizeof(uint32_t));
        break;
    case 4:
        memcpy(&ret, header->filename + 2 * sizeof(uint32_t),
               sizeof(uint32_t));
    }

    return ret;
}

static uint32_t read_gdd(MCBDevice *mdev, int reg)
{
    ChameleonDeviceDescriptor *gdd;
    uint32_t ret = 0;

    gdd = mdev->gdd;

    switch (reg) {
    case 0:
        ret = gdd->reg1;
        break;
    case 1:
        ret = gdd->reg2;
        break;
    case 2:
        ret = gdd->offset;
        break;
    case 3:
        ret = gdd->size;
        break;
    }

    return ret;
}

static uint64_t mpci_chamtbl_read(void *opaque, hwaddr addr, unsigned size)
{
    MPCIState *s = opaque;
    MCBus *bus = &s->bus;
    MCBDevice *mdev;

    trace_mpci_chamtbl_read(addr, size);

    if (addr < sizeof(ChameleonFPGAHeader)) {
        return le32_to_cpu(read_header(s, addr));
    } else if (addr >= sizeof(ChameleonFPGAHeader) &&
               addr < CHAMELEON_TABLE_SIZE) {
        /* Handle read on chameleon table */
        BusChild *kid;
        DeviceState *qdev;
        int slot;
        int offset;
        int i;

        offset = addr - sizeof(ChameleonFPGAHeader);
        slot = offset / sizeof(ChameleonDeviceDescriptor);

        kid = QTAILQ_FIRST(&BUS(bus)->children);
        for (i = 0; i < slot; i++) {
            kid = QTAILQ_NEXT(kid, sibling);
            if (!kid) { /* Last element */
                return ~0U;
            }
        }
        qdev = kid->child;
        mdev = MCB_DEVICE(qdev);
        offset -= slot * 16;

        return le32_to_cpu(read_gdd(mdev, offset / 4));
    }

    return 0;
}

static void mpci_chamtbl_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{

    if (addr < CHAMELEON_TABLE_SIZE) {
        trace_mpci_chamtbl_write(addr, val);
    }

    return;
}

static const MemoryRegionOps mpci_chamtbl_ops = {
    .read = mpci_chamtbl_read,
    .write = mpci_chamtbl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4
    },
};

static void mcb_pci_set_irq(void *opaque, int intno, int level)
{
    MCBDevice *mdev = opaque;
    MCBus *bus = MCB_BUS(qdev_get_parent_bus(DEVICE(mdev)));
    PCIDevice *pcidev = PCI_DEVICE(BUS(bus)->parent);
    MPCIState *dev = MPCI(pcidev);

    trace_mpci_set_irq(intno, level);
    if (level) {
        pci_set_irq(pcidev, !dev->int_set);
        pci_set_irq(pcidev,  dev->int_set);
    } else {
        uint16_t level_status = dev->status;

        if (level_status && !dev->int_set) {
            pci_irq_assert(pcidev);
            dev->int_set = 1;
        } else if (!level_status && dev->int_set) {
            pci_irq_deassert(pcidev);
            dev->int_set = 0;
        }
    }
}

static void mcb_pci_write_config(PCIDevice *pci_dev, uint32_t address,
                                 uint32_t val, int len)
{
    pci_default_write_config(pci_dev, address, val, len);
}

static void mcb_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    MPCIState *s = MPCI(pci_dev);
    uint8_t *pci_conf = s->dev.config;
    ChameleonFPGAHeader *header;
    MCBus *bus = &s->bus;

    header = g_new0(ChameleonFPGAHeader, 1);

    s->header = header;

    header->revision = s->rev;
    header->model = (char) s->model;
    header->minor = s->minor;
    header->bus_type = CHAMELEON_BUS_TYPE_WISHBONE;
    header->magic = CHAMELEONV2_MAGIC;
    memcpy(&header->filename, "QEMU MCB PCI", 12);

    pci_dev->config_write = mcb_pci_write_config;
    pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 0x01); /* Interrupt pin A */
    pci_conf[PCI_COMMAND] = PCI_COMMAND_MEMORY;

    mcb_bus_init(bus, sizeof(MCBus), DEVICE(pci_dev), N_MODULES,
                 mcb_pci_set_irq);

    memory_region_init(&bus->mmio_region, OBJECT(s), "mcb-pci.mmio",
                       2048 * 1024);
    memory_region_init_io(&s->ctbl, OBJECT(s), &mpci_chamtbl_ops,
                          s, "mpci_chamtbl_ops", CHAMELEON_TABLE_SIZE);
    memory_region_add_subregion(&bus->mmio_region, 0, &s->ctbl);
    pci_register_bar(&s->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &bus->mmio_region);

}

static void mcb_pci_unrealize(PCIDevice *pci_dev)
{
    MPCIState *s = MPCI(pci_dev);

    g_free(s->header);
    s->header = NULL;
}

static const VMStateDescription vmstate_mcb_pci = {
    .name = "mcb-pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, MPCIState),
        VMSTATE_END_OF_LIST()
    }
};

static Property mcb_pci_props[] = {
    DEFINE_PROP_UINT8("revision", MPCIState, rev, 1),
    DEFINE_PROP_UINT8("minor", MPCIState, minor, 0),
    DEFINE_PROP_UINT8("model", MPCIState, model, 0x41),
    DEFINE_PROP_END_OF_LIST(),
};

static void mcb_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = mcb_pci_realize;
    k->exit = mcb_pci_unrealize;
    k->vendor_id = PCI_VENDOR_ID_MEN;
    k->device_id = PCI_DEVICE_ID_MEN_MCBPCI;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "MEN Chameleon Bus over PCI";
    dc->vmsd = &vmstate_mcb_pci;
    device_class_set_props(dc, mcb_pci_props);
}

static const TypeInfo mcb_pci_info = {
    .name = TYPE_MCB_PCI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MPCIState),
    .class_init = mcb_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void mcb_pci_register_types(void)
{
    type_register(&mcb_pci_info);
}
type_init(mcb_pci_register_types);
