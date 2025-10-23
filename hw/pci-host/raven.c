/*
 * QEMU PREP PCI host
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2011-2013 Andreas Färber
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/intc/i8259.h"
#include "hw/irq.h"

#define TYPE_RAVEN_PCI_DEVICE "raven"
#define TYPE_RAVEN_PCI_HOST_BRIDGE "raven-pcihost"

OBJECT_DECLARE_SIMPLE_TYPE(PREPPCIState, RAVEN_PCI_HOST_BRIDGE)

struct PREPPCIState {
    PCIHostState parent_obj;

    qemu_irq irq;
    MemoryRegion pci_io;
    MemoryRegion pci_discontiguous_io;
    MemoryRegion pci_memory;
    MemoryRegion pci_intack;
    AddressSpace bm_as;
};

static inline uint32_t raven_idsel_to_addr(hwaddr addr)
{
    return (ctz16(addr >> 11) << 11) | (addr & 0x7ff);
}

static void raven_mmcfg_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned int size)
{
    PCIBus *hbus = opaque;

    pci_data_write(hbus, raven_idsel_to_addr(addr), val, size);
}

static uint64_t raven_mmcfg_read(void *opaque, hwaddr addr, unsigned int size)
{
    PCIBus *hbus = opaque;

    return pci_data_read(hbus, raven_idsel_to_addr(addr), size);
}

static const MemoryRegionOps raven_mmcfg_ops = {
    .read = raven_mmcfg_read,
    .write = raven_mmcfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t raven_intack_read(void *opaque, hwaddr addr,
                                  unsigned int size)
{
    return pic_read_irq(isa_pic);
}

static void raven_intack_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s not implemented\n", __func__);
}

static const MemoryRegionOps raven_intack_ops = {
    .read = raven_intack_read,
    .write = raven_intack_write,
    .valid = {
        .max_access_size = 1,
    },
};

/* Convert 8 MB non-contiguous address to 64k ISA IO address */
static inline hwaddr raven_io_addr(hwaddr addr)
{
    return ((addr & 0x007FFF000) >> 7) | (addr & 0x1F);
}

static uint64_t raven_io_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint64_t val = 0xffffffffULL;

    memory_region_dispatch_read(opaque, raven_io_addr(addr), &val,
                                size_memop(size) | MO_LE,
                                MEMTXATTRS_UNSPECIFIED);
    return val;
}

static void raven_io_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned int size)
{
    memory_region_dispatch_write(opaque, raven_io_addr(addr), val,
                                 size_memop(size) | MO_LE,
                                 MEMTXATTRS_UNSPECIFIED);
}

static const MemoryRegionOps raven_io_ops = {
    .read = raven_io_read,
    .write = raven_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.max_access_size = 4,
    .impl.unaligned = true,
    .valid.unaligned = true,
};

/*
 * All four IRQ[ABCD] pins from all slots are tied to a single board
 * IRQ, so our mapping function here maps everything to IRQ 0.
 * The code in pci_change_irq_level() tracks the number of times
 * the mapped IRQ is asserted and deasserted, so if multiple devices
 * assert an IRQ at the same time the behaviour is correct.
 *
 * This may need further refactoring for boards that use multiple IRQ lines.
 */
static int raven_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return 0;
}

static void raven_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *irq = opaque;

    qemu_set_irq(*irq, level);
}

static AddressSpace *raven_pcihost_set_iommu(PCIBus *bus, void *opaque,
                                             int devfn)
{
    PREPPCIState *s = opaque;

    return &s->bm_as;
}

static const PCIIOMMUOps raven_iommu_ops = {
    .get_address_space = raven_pcihost_set_iommu,
};

static void raven_pcihost_realize(DeviceState *d, Error **errp)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(d);
    PCIHostState *h = PCI_HOST_BRIDGE(dev);
    PREPPCIState *s = RAVEN_PCI_HOST_BRIDGE(dev);
    Object *o = OBJECT(d);
    MemoryRegion *mr, *bm, *address_space_mem = get_system_memory();

    memory_region_init(&s->pci_io, o, "pci-io", 0x3f800000);
    memory_region_init_io(&s->pci_discontiguous_io, o,
                          &raven_io_ops, &s->pci_io,
                          "pci-discontiguous-io", 8 * MiB);
    memory_region_set_enabled(&s->pci_discontiguous_io, false);
    memory_region_init(&s->pci_memory, o, "pci-memory", 0x3f000000);

    sysbus_init_mmio(dev, &s->pci_io);
    sysbus_init_mmio(dev, &s->pci_discontiguous_io);
    sysbus_init_mmio(dev, &s->pci_memory);
    sysbus_init_irq(dev, &s->irq);

    h->bus = pci_register_root_bus(d, NULL, raven_set_irq, raven_map_irq,
                                   &s->irq, &s->pci_memory, &s->pci_io, 0, 1,
                                   TYPE_PCI_BUS);

    memory_region_init_io(&h->conf_mem, o, &pci_host_conf_le_ops, s,
                          "pci-conf-idx", 4);
    memory_region_add_subregion(&s->pci_io, 0xcf8, &h->conf_mem);

    memory_region_init_io(&h->data_mem, o, &pci_host_data_le_ops, s,
                          "pci-conf-data", 4);
    memory_region_add_subregion(&s->pci_io, 0xcfc, &h->data_mem);

    mr = g_new0(MemoryRegion, 1);
    memory_region_init_io(mr, o, &raven_mmcfg_ops, h->bus,
                          "pci-mmcfg", 8 * MiB);
    memory_region_add_subregion(&s->pci_io, 0x800000, mr);

    memory_region_init_io(&s->pci_intack, o, &raven_intack_ops, s,
                          "pci-intack", 1);
    memory_region_add_subregion(address_space_mem, 0xbffffff0, &s->pci_intack);

    pci_create_simple(h->bus, PCI_DEVFN(0, 0), TYPE_RAVEN_PCI_DEVICE);

    /* Bus master address space */
    bm = g_new0(MemoryRegion, 1);
    memory_region_init(bm, o, "raven-bm", 4 * GiB);
    mr = g_new0(MemoryRegion, 1);
    memory_region_init_alias(mr, o, "bm-pci-memory", &s->pci_memory, 0,
                             memory_region_size(&s->pci_memory));
    memory_region_add_subregion(bm, 0, mr);
    mr = g_new0(MemoryRegion, 1);
    memory_region_init_alias(mr, o, "bm-system", get_system_memory(),
                             0, 0x80000000);
    memory_region_add_subregion(bm, 0x80000000, mr);
    address_space_init(&s->bm_as, bm, "raven-bm-as");
    pci_setup_iommu(h->bus, &raven_iommu_ops, s);
}

static void raven_pcihost_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->realize = raven_pcihost_realize;
    dc->fw_name = "pci";
}

static void raven_realize(PCIDevice *d, Error **errp)
{
    d->config[PCI_CACHE_LINE_SIZE] = 0x08;
    d->config[PCI_LATENCY_TIMER] = 0x10;
    d->config[PCI_CAPABILITY_LIST] = 0x00;
}

static void raven_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = raven_realize;
    k->vendor_id = PCI_VENDOR_ID_MOTOROLA;
    k->device_id = PCI_DEVICE_ID_MOTOROLA_RAVEN;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    dc->desc = "PReP Host Bridge - Motorola Raven";
    /*
     * Reason: PCI-facing part of the host bridge, not usable without
     * the host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo raven_types[] = {
    {
        .name = TYPE_RAVEN_PCI_HOST_BRIDGE,
        .parent = TYPE_PCI_HOST_BRIDGE,
        .instance_size = sizeof(PREPPCIState),
        .class_init = raven_pcihost_class_init,
    },
    {
        .name = TYPE_RAVEN_PCI_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .class_init = raven_class_init,
        .interfaces = (const InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    },
};

DEFINE_TYPES(raven_types)
