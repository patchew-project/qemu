/*
 * libqos PCI bindings for ARM
 *
 * Copyright Red Hat Inc., 2021
 *
 * Authors:
 *  Eric Auger   <eric.auger@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "pci-arm.h"
#include "qapi/qmp/qdict.h"
#include "hw/pci/pci_regs.h"

#include "qemu/module.h"

static uint8_t qpci_arm_pio_readb(QPCIBus *bus, uint32_t addr)
{
    QPCIBusARM *s = container_of(bus, QPCIBusARM, bus);

    return qtest_readb(bus->qts, s->gpex_pio_base + addr);
}

static void qpci_arm_pio_writeb(QPCIBus *bus, uint32_t addr, uint8_t val)
{
    QPCIBusARM *s = container_of(bus, QPCIBusARM, bus);

    qtest_writeb(bus->qts, s->gpex_pio_base + addr,  val);
}

static uint16_t qpci_arm_pio_readw(QPCIBus *bus, uint32_t addr)
{
    QPCIBusARM *s = container_of(bus, QPCIBusARM, bus);

    return qtest_readw(bus->qts, s->gpex_pio_base + addr);
}

static void qpci_arm_pio_writew(QPCIBus *bus, uint32_t addr, uint16_t val)
{
    QPCIBusARM *s = container_of(bus, QPCIBusARM, bus);

    qtest_writew(bus->qts, s->gpex_pio_base + addr, val);
}

static uint32_t qpci_arm_pio_readl(QPCIBus *bus, uint32_t addr)
{
    QPCIBusARM *s = container_of(bus, QPCIBusARM, bus);

    return qtest_readl(bus->qts, s->gpex_pio_base + addr);
}

static void qpci_arm_pio_writel(QPCIBus *bus, uint32_t addr, uint32_t val)
{
    QPCIBusARM *s = container_of(bus, QPCIBusARM, bus);

    qtest_writel(bus->qts, s->gpex_pio_base + addr, val);
}

static uint64_t qpci_arm_pio_readq(QPCIBus *bus, uint32_t addr)
{
    QPCIBusARM *s = container_of(bus, QPCIBusARM, bus);

    return qtest_readq(bus->qts, s->gpex_pio_base + addr);
}

static void qpci_arm_pio_writeq(QPCIBus *bus, uint32_t addr, uint64_t val)
{
    QPCIBusARM *s = container_of(bus, QPCIBusARM, bus);

    qtest_writeq(bus->qts, s->gpex_pio_base + addr, val);
}

static void qpci_arm_memread(QPCIBus *bus, uint32_t addr, void *buf, size_t len)
{
    qtest_memread(bus->qts, addr, buf, len);
}

static void qpci_arm_memwrite(QPCIBus *bus, uint32_t addr,
                             const void *buf, size_t len)
{
    qtest_memwrite(bus->qts, addr, buf, len);
}

static uint8_t qpci_arm_config_readb(QPCIBus *bus, int devfn, uint8_t offset)
{
    uint64_t addr = bus->ecam_alloc_ptr + ((0 << 20) | (devfn << 12) | offset);
    uint8_t val;

    qtest_memread(bus->qts, addr, &val, 1);
    return val;
}

static uint16_t qpci_arm_config_readw(QPCIBus *bus, int devfn, uint8_t offset)
{
    uint64_t addr = bus->ecam_alloc_ptr + ((0 << 20) | (devfn << 12) | offset);
    uint16_t val;

    qtest_memread(bus->qts, addr, &val, 2);
    return val;
}

static uint32_t qpci_arm_config_readl(QPCIBus *bus, int devfn, uint8_t offset)
{
    uint64_t addr = bus->ecam_alloc_ptr + ((0 << 20) | (devfn << 12) | offset);
    uint32_t val;

    qtest_memread(bus->qts, addr, &val, 4);
    return val;
}

static void
qpci_arm_config_writeb(QPCIBus *bus, int devfn, uint8_t offset, uint8_t value)
{
    uint64_t addr = bus->ecam_alloc_ptr + ((0 << 20) | (devfn << 12) | offset);
    uint32_t val = value;

    qtest_memwrite(bus->qts, addr, &val, 1);
}

static void
qpci_arm_config_writew(QPCIBus *bus, int devfn, uint8_t offset, uint16_t value)
{
    uint64_t addr = bus->ecam_alloc_ptr + ((0 << 20) | (devfn << 12) | offset);
    uint32_t val = value;

    qtest_memwrite(bus->qts, addr, &val, 2);
}

static void
qpci_arm_config_writel(QPCIBus *bus, int devfn, uint8_t offset, uint32_t value)
{
    uint64_t addr = bus->ecam_alloc_ptr + ((0 << 20) | (devfn << 12) | offset);
    uint32_t val = value;

    qtest_memwrite(bus->qts, addr, &val, 4);
}

static void *qpci_arm_get_driver(void *obj, const char *interface)
{
    QPCIBusARM *qpci = obj;
    if (!g_strcmp0(interface, "pci-bus")) {
        return &qpci->bus;
    }
    fprintf(stderr, "%s not present in pci-bus-arm\n", interface);
    g_assert_not_reached();
}

void qpci_init_arm(QPCIBusARM *qpci, QTestState *qts,
                   QGuestAllocator *alloc, bool hotpluggable)
{
    assert(qts);

    qpci->gpex_pio_base = 0x3eff0000;
    qpci->bus.not_hotpluggable = !hotpluggable;
    qpci->bus.has_buggy_msi = false;

    qpci->bus.pio_readb = qpci_arm_pio_readb;
    qpci->bus.pio_readw = qpci_arm_pio_readw;
    qpci->bus.pio_readl = qpci_arm_pio_readl;
    qpci->bus.pio_readq = qpci_arm_pio_readq;

    qpci->bus.pio_writeb = qpci_arm_pio_writeb;
    qpci->bus.pio_writew = qpci_arm_pio_writew;
    qpci->bus.pio_writel = qpci_arm_pio_writel;
    qpci->bus.pio_writeq = qpci_arm_pio_writeq;

    qpci->bus.memread = qpci_arm_memread;
    qpci->bus.memwrite = qpci_arm_memwrite;

    qpci->bus.config_readb = qpci_arm_config_readb;
    qpci->bus.config_readw = qpci_arm_config_readw;
    qpci->bus.config_readl = qpci_arm_config_readl;

    qpci->bus.config_writeb = qpci_arm_config_writeb;
    qpci->bus.config_writew = qpci_arm_config_writew;
    qpci->bus.config_writel = qpci_arm_config_writel;

    qpci->bus.qts = qts;
    qpci->bus.pio_alloc_ptr = 0x0000;
    qpci->bus.pio_limit = 0x10000;
    qpci->bus.mmio_alloc_ptr = 0x10000000;
    qpci->bus.mmio_limit = 0x2eff0000;
    qpci->bus.ecam_alloc_ptr = 0x4010000000;

    qpci->obj.get_driver = qpci_arm_get_driver;
}

QPCIBus *qpci_new_arm(QTestState *qts, QGuestAllocator *alloc,
                      bool hotpluggable)
{
    QPCIBusARM *qpci = g_new0(QPCIBusARM, 1);
    qpci_init_arm(qpci, qts, alloc, hotpluggable);

    return &qpci->bus;
}

void qpci_free_arm(QPCIBus *bus)
{
    QPCIBusARM *s;

    if (!bus) {
        return;
    }
    s = container_of(bus, QPCIBusARM, bus);

    g_free(s);
}

static void qpci_arm_register_nodes(void)
{
    qos_node_create_driver("pci-bus-arm", NULL);
    qos_node_produces_opts("pci-bus-arm", "pci-bus", NULL);
}

libqos_init(qpci_arm_register_nodes);
