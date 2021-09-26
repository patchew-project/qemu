/*
 * Dynamical memory attached PCI device
 *
 * Copyright Montage, Corp. 2014
 *
 * Authors:
 *  David Dai <david.dai@montage-tech.com>
 *  Changguo Du <changguo.du@montage-tech.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/pci/msi.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"
#include "qapi/visitor.h"
#include "qom/object.h"
#include "qemu/error-report.h"

#define PCI_VENDOR_ID_DMDEV   0x1b00
#define PCI_DEVICE_ID_DMDEV   0x1110
#define DYNAMIC_MDEV_BAR_SIZE 0x1000

#define INTERRUPT_MEMORY_ATTACH_SUCCESS           (1 << 0)
#define INTERRUPT_MEMORY_DEATTACH_SUCCESS         (1 << 1)
#define INTERRUPT_MEMORY_ATTACH_NOMEM             (1 << 2)
#define INTERRUPT_MEMORY_ATTACH_ALIGN_ERR         (1 << 3)
#define INTERRUPT_ACCESS_NOT_MAPPED_ADDR          (1 << 4)

#define DYNAMIC_CMD_ENABLE               (0x80000000)
#define DYNAMIC_CMD_MASK                 (0xffff)
#define DYNAMIC_CMD_MEM_ATTACH           (0x1)
#define DYNAMIC_CMD_MEM_DEATTACH         (0x2)

#define DYNAMIC_MDEV_DEBUG               1

#define DYNAMIC_MDEV_DPRINTF(fmt, ...)                          \
    do {                                                        \
        if (DYNAMIC_MDEV_DEBUG) {                               \
            printf("QEMU: " fmt, ## __VA_ARGS__);               \
        }                                                       \
    } while (0)

#define TYPE_DYNAMIC_MDEV "dyanmic-mdevice"

typedef struct DmdevState DmdevState;
DECLARE_INSTANCE_CHECKER(DmdevState, DYNAMIC_MDEV,
                         TYPE_DYNAMIC_MDEV)

struct DmdevState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    /* registers */
    uint32_t mask;
    uint32_t status;
    uint32_t align;
    uint64_t size;
    uint64_t hw_offset;
    uint64_t mem_offset;

    /* mdev name */
    char *devname;
    int fd;

    /* memory bar size */
    uint64_t bsize;

    /* BAR 0 (registers) */
    MemoryRegion dmdev_mmio;

    /* BAR 2 (memory bar for daynamical memory attach) */
    MemoryRegion dmdev_mem;
};

/* registers for the dynamical memory device */
enum dmdev_registers {
    INT_MASK     =     0, /* RW */
    INT_STATUS   =     4, /* RW: write 1 clear */
    DOOR_BELL    =     8, /*
                           * RW: trigger device to act
                           *  31        15        0
                           *  --------------------
                           * |en|xxxxxxxx|  cmd   |
                           *  --------------------
                           */

    /* RO: 4k, 2M, 1G aglign for memory size */
    MEM_ALIGN   =     12,

    /* RO: offset in memory bar shows bar space has had ram map */
    HW_OFFSET    =    16,

    /* RW: size of dynamical attached memory */
    MEM_SIZE     =    24,

    /* RW: offset in host mdev, where dynamical attached memory from  */
    MEM_OFFSET   =    32,

};

static void dmdev_mem_attach(DmdevState *s)
{
    void *ptr;
    struct MemoryRegion *mr;
    uint64_t size = s->size;
    uint64_t align = s->align;
    uint64_t hwaddr = s->hw_offset;
    uint64_t offset = s->mem_offset;
    PCIDevice *pdev = PCI_DEVICE(s);

    DYNAMIC_MDEV_DPRINTF("%s:size =0x%lx,align=0x%lx,hwaddr=0x%lx,\
        offset=0x%lx\n", __func__, size, align, hwaddr, offset);

    if (size % align || hwaddr % align) {
        error_report("%s size doesn't align, size =0x%lx, \
                align=0x%lx, hwaddr=0x%lx\n", __func__, size, align, hwaddr);
        s->status |= INTERRUPT_MEMORY_ATTACH_ALIGN_ERR;
        msi_notify(pdev, 0);
        return;
    }

    ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, offset);
    if (ptr == MAP_FAILED) {
        error_report("Can't map memory err(%d)", errno);
        s->status |= INTERRUPT_MEMORY_ATTACH_ALIGN_ERR;
        msi_notify(pdev, 0);
        return;
    }

    mr = g_new0(MemoryRegion, 1);
    memory_region_init_ram_ptr(mr, OBJECT(PCI_DEVICE(s)),
                            "dynamic_mdev", size, ptr);
    memory_region_add_subregion_overlap(&s->dmdev_mem, hwaddr, mr, 1);

    s->hw_offset += size;

    s->status |= INTERRUPT_MEMORY_ATTACH_SUCCESS;
    msi_notify(pdev, 0);

    DYNAMIC_MDEV_DPRINTF("%s msi_notify success ptr=%p\n", __func__, ptr);
    return;
}

static void dmdev_mem_deattach(DmdevState *s)
{
    struct MemoryRegion *mr = &s->dmdev_mem;
    struct MemoryRegion *subregion;
    void *host;
    PCIDevice *pdev = PCI_DEVICE(s);

    memory_region_transaction_begin();
    while (!QTAILQ_EMPTY(&mr->subregions)) {
        subregion = QTAILQ_FIRST(&mr->subregions);
        memory_region_del_subregion(mr, subregion);
        host = memory_region_get_ram_ptr(subregion);
        munmap(host, memory_region_size(subregion));
        DYNAMIC_MDEV_DPRINTF("%s:host=%p,size=0x%lx\n",
                    __func__, host,  memory_region_size(subregion));
    }

    memory_region_transaction_commit();

    s->hw_offset = 0;

    s->status |= INTERRUPT_MEMORY_DEATTACH_SUCCESS;
    msi_notify(pdev, 0);

    return;
}

static void dmdev_doorbell_handle(DmdevState *s,  uint64_t val)
{
    if (!(val & DYNAMIC_CMD_ENABLE)) {
        return;
    }

    switch (val & DYNAMIC_CMD_MASK) {

    case DYNAMIC_CMD_MEM_ATTACH:
        dmdev_mem_attach(s);
        break;

    case DYNAMIC_CMD_MEM_DEATTACH:
        dmdev_mem_deattach(s);
        break;

    default:
        break;
    }

    return;
}

static void dmdev_mmio_write(void *opaque, hwaddr addr,
                        uint64_t val, unsigned size)
{
    DmdevState *s = opaque;

    DYNAMIC_MDEV_DPRINTF("%s write addr=0x%lx, val=0x%lx, size=0x%x\n",
                __func__, addr, val, size);

    switch (addr) {
    case INT_MASK:
        s->mask = val;
        return;

    case INT_STATUS:
        return;

    case DOOR_BELL:
        dmdev_doorbell_handle(s, val);
        return;

    case MEM_ALIGN:
        return;

    case HW_OFFSET:
        /* read only */
        return;

    case HW_OFFSET + 4:
        /* read only */
        return;

    case MEM_SIZE:
        if (size == 4) {
            s->size &= ~(0xffffffff);
            val &= 0xffffffff;
            s->size |= val;
        } else { /* 64-bit */
            s->size = val;
        }
        return;

    case MEM_SIZE + 4:
        s->size &= 0xffffffff;

        s->size |= val << 32;
        return;

    case MEM_OFFSET:
        if (size == 4) {
            s->mem_offset &= ~(0xffffffff);
            val &= 0xffffffff;
            s->mem_offset |= val;
        } else { /* 64-bit */
            s->mem_offset = val;
        }
        return;

    case MEM_OFFSET + 4:
        s->mem_offset &= 0xffffffff;

        s->mem_offset |= val << 32;
        return;

    default:
        DYNAMIC_MDEV_DPRINTF("default 0x%lx\n", val);
    }

    return;
}

static uint64_t dmdev_mmio_read(void *opaque, hwaddr addr,
                        unsigned size)
{
    DmdevState *s = opaque;
    unsigned int value;

    DYNAMIC_MDEV_DPRINTF("%s read addr=0x%lx, size=0x%x\n",
                         __func__, addr, size);
    switch (addr) {
    case INT_MASK:
        /* mask: read-write */
        return s->mask;

    case INT_STATUS:
        /* status: read-clear */
        value = s->status;
        s->status = 0;
        return value;

    case DOOR_BELL:
        /* doorbell: write-only */
        return 0;

    case MEM_ALIGN:
        /* align: read-only */
        return s->align;

    case HW_OFFSET:
        if (size == 4) {
            return s->hw_offset & 0xffffffff;
        } else { /* 64-bit */
            return s->hw_offset;
        }

    case HW_OFFSET + 4:
        return s->hw_offset >> 32;

    case MEM_SIZE:
        if (size == 4) {
            return s->size & 0xffffffff;
        } else { /* 64-bit */
            return s->size;
        }

    case MEM_SIZE + 4:
        return s->size >> 32;

    case MEM_OFFSET:
        if (size == 4) {
            return s->mem_offset & 0xffffffff;
        } else { /* 64-bit */
            return s->mem_offset;
        }

    case MEM_OFFSET + 4:
        return s->mem_offset >> 32;

    default:
        DYNAMIC_MDEV_DPRINTF("default read err address 0x%lx\n", addr);

    }

    return 0;
}

static const MemoryRegionOps dmdev_mmio_ops = {
    .read = dmdev_mmio_read,
    .write = dmdev_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void dmdev_reset(DeviceState *d)
{
    DmdevState *s = DYNAMIC_MDEV(d);

    s->status = 0;
    s->mask = 0;
    s->hw_offset = 0;
    dmdev_mem_deattach(s);
}

static void dmdev_realize(PCIDevice *dev, Error **errp)
{
    DmdevState *s = DYNAMIC_MDEV(dev);
    int status;

    Error *err = NULL;
    uint8_t *pci_conf;

    pci_conf = dev->config;
    pci_conf[PCI_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEMORY;

    /* init msi */
    status = msi_init(dev, 0, 1, true, false, &err);
    if (status) {
        error_report("msi_init %d failed", status);
        return;
    }

    memory_region_init_io(&s->dmdev_mmio, OBJECT(s), &dmdev_mmio_ops, s,
                          "dmdev-mmio", DYNAMIC_MDEV_BAR_SIZE);

    /* region for registers*/
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &s->dmdev_mmio);

    /* initialize a memory region container */
    memory_region_init(&s->dmdev_mem, OBJECT(s),
                       "dmdev-mem", s->bsize);

    pci_register_bar(PCI_DEVICE(s), 2,
                    PCI_BASE_ADDRESS_SPACE_MEMORY |
                    PCI_BASE_ADDRESS_MEM_PREFETCH |
                    PCI_BASE_ADDRESS_MEM_TYPE_64,
                    &s->dmdev_mem);

    if (s->devname) {
        s->fd = open(s->devname, O_RDWR, 0x0777);
    } else {
        s->fd = -1;
    }

    s->hw_offset = 0;

    DYNAMIC_MDEV_DPRINTF("open file %s %s\n",
            s->devname, s->fd < 0 ? "failed" : "success");
}

static void dmdev_exit(PCIDevice *dev)
{
    DmdevState *s = DYNAMIC_MDEV(dev);

    msi_uninit(dev);
    dmdev_mem_deattach(s);
    DYNAMIC_MDEV_DPRINTF("%s\n", __func__);

}

static Property dmdev_properties[] = {
    DEFINE_PROP_UINT64("size", DmdevState, bsize, 0x40000000),
    DEFINE_PROP_UINT32("align", DmdevState, align, 0x40000000),
    DEFINE_PROP_STRING("mem-path", DmdevState, devname),
    DEFINE_PROP_END_OF_LIST(),
};

static void dmdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = dmdev_realize;
    k->exit = dmdev_exit;
    k->vendor_id = PCI_VENDOR_ID_DMDEV;
    k->device_id = PCI_DEVICE_ID_DMDEV;
    k->class_id = PCI_CLASS_MEMORY_RAM;
    k->revision = 1;
    dc->reset = dmdev_reset;
    device_class_set_props(dc, dmdev_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "pci device to dynamically attach memory";
}

static const TypeInfo dmdev_info = {
    .name          = TYPE_DYNAMIC_MDEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(DmdevState),
    .class_init    = dmdev_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};

static void dmdev_register_types(void)
{
    type_register_static(&dmdev_info);
}

type_init(dmdev_register_types)
