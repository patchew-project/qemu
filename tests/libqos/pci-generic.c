/*
 * libqos PCI bindings for non-PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Prem Mallappa     <prem.mallappa@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci-generic.h"

#include "hw/pci/pci_regs.h"

#include "qemu-common.h"
#include "qemu/host-utils.h"

#include <glib.h>

static uint8_t qpci_generic_io_readb(QPCIBus *bus, void *addr)
{
    return readb((uintptr_t)addr);
}

static uint16_t qpci_generic_io_readw(QPCIBus *bus, void *addr)
{
    return readw((uintptr_t)addr);
}

static uint32_t qpci_generic_io_readl(QPCIBus *bus, void *addr)
{
    return readl((uintptr_t)addr);
}

static void qpci_generic_io_writeb(QPCIBus *bus, void *addr, uint8_t value)
{
    writeb((uintptr_t)addr, value);
}

static void qpci_generic_io_writew(QPCIBus *bus, void *addr, uint16_t value)
{
    writew((uintptr_t)addr, value);
}

static void qpci_generic_io_writel(QPCIBus *bus, void *addr, uint32_t value)
{
    writel((uintptr_t)addr, value);
}

#define devfn2addr(base, devfn, offset) \
	((base) | ((devfn) << 12) | (offset))

#define bdf2offset(bus, devfn) \
    ((bus) << 20 | (devfn) << 12)

static uint8_t qpci_generic_config_readb(QPCIBus *bus, int devfn, uint8_t offset)
{
    QPCIBusGen *s = container_of(bus, QPCIBusGen, bus);
    return readb(devfn2addr(s->base, devfn, offset));
}

static uint16_t qpci_generic_config_readw(QPCIBus *bus, int devfn, uint8_t offset)
{ 
    QPCIBusGen *s = container_of(bus, QPCIBusGen, bus);
    return readw(devfn2addr(s->base, devfn, offset));
}

static uint32_t qpci_generic_config_readl(QPCIBus *bus, int devfn, uint8_t offset)
{
    QPCIBusGen *s = container_of(bus, QPCIBusGen, bus);
    return readl(devfn2addr(s->base, devfn, offset));
}

static void qpci_generic_config_writeb(QPCIBus *bus, int devfn, uint8_t offset, uint8_t value)
{
    QPCIBusGen *s = container_of(bus, QPCIBusGen, bus);
    writeb(devfn2addr(s->base, devfn, offset), value);
}

static void qpci_generic_config_writew(QPCIBus *bus, int devfn, uint8_t offset, uint16_t value)
{
    QPCIBusGen *s = container_of(bus, QPCIBusGen, bus);
    writew(devfn2addr(s->base, devfn, offset), value);
}

static void qpci_generic_config_writel(QPCIBus *bus, int devfn, uint8_t offset, uint32_t value)
{
    QPCIBusGen *s = container_of(bus, QPCIBusGen, bus);
    writel(devfn2addr(s->base, devfn, offset), value);
}

static void *qpci_generic_iomap(QPCIBus *bus, QPCIDevice *dev, int barno, uint64_t *sizeptr)
{
    QPCIBusGen *s = container_of(bus, QPCIBusGen, bus);
    static const int bar_reg_map[] = {
        PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_2,
        PCI_BASE_ADDRESS_3, PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_5,
    };
    int bar_reg;
    uint32_t addr;
    uint64_t size;
    uint32_t io_type;

    g_assert(barno >= 0 && barno <= 5);
    bar_reg = bar_reg_map[barno];

    qpci_config_writel(dev, bar_reg, 0xFFFFFFFF);
    addr = qpci_config_readl(dev, bar_reg);

    io_type = addr & PCI_BASE_ADDRESS_SPACE;
    if (io_type == PCI_BASE_ADDRESS_SPACE_IO) {
        addr &= PCI_BASE_ADDRESS_IO_MASK;
    } else {
        addr &= PCI_BASE_ADDRESS_MEM_MASK;
    }

    size = (1ULL << ctzl(addr));
    if (size == 0) {
        return NULL;
    }
    if (sizeptr) {
        *sizeptr = size;
    }

    if (io_type == PCI_BASE_ADDRESS_SPACE_IO) {
        uint16_t loc;

        g_assert(QEMU_ALIGN_UP(s->pci_iohole_alloc, size) + size
                 <= s->pci_iohole_size);
        s->pci_iohole_alloc = QEMU_ALIGN_UP(s->pci_iohole_alloc, size);
        loc = s->pci_iohole_start + s->pci_iohole_alloc;
        s->pci_iohole_alloc += size;

        qpci_config_writel(dev, bar_reg, loc | PCI_BASE_ADDRESS_SPACE_IO);

        return (void *)(intptr_t)loc;
    } else {
        uint64_t loc;

        g_assert(QEMU_ALIGN_UP(s->pci_hole_alloc, size) + size
                 <= s->pci_hole_size);
        s->pci_hole_alloc = QEMU_ALIGN_UP(s->pci_hole_alloc, size);
        loc = s->pci_hole_start + s->pci_hole_alloc;
        s->pci_hole_alloc += size;
        printf("%s: hole_start:%x hole_alloc:%x\n", __func__,
               s->pci_hole_start, s->pci_hole_alloc);
        qpci_config_writel(dev, bar_reg, loc);

        return (void *)(intptr_t)loc;
    }
}

static void qpci_generic_iounmap(QPCIBus *bus, void *data)
{
    /* FIXME */
}

QPCIBus *qpci_init_generic(QPCIBusGen *conf)
{
    QPCIBusGen *ret;

    ret = g_malloc(sizeof(*ret));
    memcpy(ret, conf, sizeof(*ret));

    ret->bus.io_readb = qpci_generic_io_readb;
    ret->bus.io_readw = qpci_generic_io_readw;
    ret->bus.io_readl = qpci_generic_io_readl;

    ret->bus.io_writeb = qpci_generic_io_writeb;
    ret->bus.io_writew = qpci_generic_io_writew;
    ret->bus.io_writel = qpci_generic_io_writel;

    ret->bus.config_readb = qpci_generic_config_readb;
    ret->bus.config_readw = qpci_generic_config_readw;
    ret->bus.config_readl = qpci_generic_config_readl;

    ret->bus.config_writeb = qpci_generic_config_writeb;
    ret->bus.config_writew = qpci_generic_config_writew;
    ret->bus.config_writel = qpci_generic_config_writel;

    ret->bus.iomap = qpci_generic_iomap;
    ret->bus.iounmap = qpci_generic_iounmap;

    return &ret->bus;
}

void qpci_free_generic(QPCIBus *bus)
{
    QPCIBusGen *s = container_of(bus, QPCIBusGen, bus);

    g_free(s);
}
