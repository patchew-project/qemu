/*
 * libqos PCI bindings for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci-pc.h"
#include "qapi/qmp/qdict.h"
#include "hw/pci/pci_regs.h"

#include "qemu-common.h"

#define ACPI_PCIHP_ADDR         0xae00
#define PCI_EJ_BASE             0x0008

static uint8_t qpci_pc_pio_readb(QPCIBus *bus, uint32_t addr)
{
    return inb(addr);
}

static void qpci_pc_pio_writeb(QPCIBus *bus, uint32_t addr, uint8_t val)
{
    outb(addr, val);
}

static uint16_t qpci_pc_pio_readw(QPCIBus *bus, uint32_t addr)
{
    return inw(addr);
}

static void qpci_pc_pio_writew(QPCIBus *bus, uint32_t addr, uint16_t val)
{
    outw(addr, val);
}

static uint32_t qpci_pc_pio_readl(QPCIBus *bus, uint32_t addr)
{
    return inl(addr);
}

static void qpci_pc_pio_writel(QPCIBus *bus, uint32_t addr, uint32_t val)
{
    outl(addr, val);
}

static uint64_t qpci_pc_pio_readq(QPCIBus *bus, uint32_t addr)
{
    return (uint64_t)inl(addr) + ((uint64_t)inl(addr + 4) << 32);
}

static void qpci_pc_pio_writeq(QPCIBus *bus, uint32_t addr, uint64_t val)
{
    outl(addr, val & 0xffffffff);
    outl(addr + 4, val >> 32);
}

static void qpci_pc_memread(QPCIBus *bus, uint32_t addr, void *buf, size_t len)
{
    memread(addr, buf, len);
}

static void qpci_pc_memwrite(QPCIBus *bus, uint32_t addr,
                             const void *buf, size_t len)
{
    memwrite(addr, buf, len);
}

static uint8_t qpci_pc_config_readb(QPCIBus *bus, int devfn, uint8_t offset)
{
    outl(0xcf8, (1U << 31) | (devfn << 8) | offset);
    return inb(0xcfc);
}

static uint16_t qpci_pc_config_readw(QPCIBus *bus, int devfn, uint8_t offset)
{
    outl(0xcf8, (1U << 31) | (devfn << 8) | offset);
    return inw(0xcfc);
}

static uint32_t qpci_pc_config_readl(QPCIBus *bus, int devfn, uint8_t offset)
{
    outl(0xcf8, (1U << 31) | (devfn << 8) | offset);
    return inl(0xcfc);
}

static void qpci_pc_config_writeb(QPCIBus *bus, int devfn, uint8_t offset, uint8_t value)
{
    outl(0xcf8, (1U << 31) | (devfn << 8) | offset);
    outb(0xcfc, value);
}

static void qpci_pc_config_writew(QPCIBus *bus, int devfn, uint8_t offset, uint16_t value)
{
    outl(0xcf8, (1U << 31) | (devfn << 8) | offset);
    outw(0xcfc, value);
}

static void qpci_pc_config_writel(QPCIBus *bus, int devfn, uint8_t offset, uint32_t value)
{
    outl(0xcf8, (1U << 31) | (devfn << 8) | offset);
    outl(0xcfc, value);
}

static void *qpci_pc_get_driver(void *obj, const char *interface)
{
    QPCIBusPC *qpci = obj;
    if (!g_strcmp0(interface, "pci-bus")) {
        return &qpci->bus;
    }
    fprintf(stderr, "%s not present in pci-bus-pc\n", interface);
    g_assert_not_reached();
}

void qpci_init_pc(QPCIBusPC *qpci, QTestState *qts, QGuestAllocator *alloc)
{
    assert(qts);

    qpci->bus.pio_readb = qpci_pc_pio_readb;
    qpci->bus.pio_readw = qpci_pc_pio_readw;
    qpci->bus.pio_readl = qpci_pc_pio_readl;
    qpci->bus.pio_readq = qpci_pc_pio_readq;

    qpci->bus.pio_writeb = qpci_pc_pio_writeb;
    qpci->bus.pio_writew = qpci_pc_pio_writew;
    qpci->bus.pio_writel = qpci_pc_pio_writel;
    qpci->bus.pio_writeq = qpci_pc_pio_writeq;

    qpci->bus.memread = qpci_pc_memread;
    qpci->bus.memwrite = qpci_pc_memwrite;

    qpci->bus.config_readb = qpci_pc_config_readb;
    qpci->bus.config_readw = qpci_pc_config_readw;
    qpci->bus.config_readl = qpci_pc_config_readl;

    qpci->bus.config_writeb = qpci_pc_config_writeb;
    qpci->bus.config_writew = qpci_pc_config_writew;
    qpci->bus.config_writel = qpci_pc_config_writel;

    qpci->bus.qts = qts;
    qpci->bus.pio_alloc_ptr = 0xc000;
    qpci->bus.mmio_alloc_ptr = 0xE0000000;
    qpci->bus.mmio_limit = 0x100000000ULL;

    qpci->obj.get_driver = qpci_pc_get_driver;
}

QPCIBus *qpci_new_pc(QTestState *qts, QGuestAllocator *alloc)
{
    QPCIBusPC *qpci = g_new0(QPCIBusPC, 1);
    qpci_init_pc(qpci, qts, alloc);

    return &qpci->bus;
}

void qpci_free_pc(QPCIBus *bus)
{
    QPCIBusPC *s;

    if (!bus) {
        return;
    }
    s = container_of(bus, QPCIBusPC, bus);

    g_free(s);
}

void qpci_unplug_acpi_device_test(const char *id, uint8_t slot)
{
    QDict *response;

    response = qmp("{'execute': 'device_del', 'arguments': {'id': %s}}",
                   id);
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    qobject_unref(response);

    outb(ACPI_PCIHP_ADDR + PCI_EJ_BASE, 1 << slot);

    qmp_eventwait("DEVICE_DELETED");
}

static void qpci_pc_register_nodes(void)
{
    qos_node_create_driver("pci-bus-pc", NULL);
    qos_node_produces("pci-bus-pc", "pci-bus");
}

libqos_init(qpci_pc_register_nodes);
