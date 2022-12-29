/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "hw/net/e1000_regs.h"
#include "hw/pci/pci_ids.h"
#include "../libqtest.h"
#include "pci-pc.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "libqos-malloc.h"
#include "qgraph.h"
#include "igb.h"

#define IGB_IVAR_TEST_CFG0 \
    (((IGB_RX0_MSIX_VEC | 0x80)) << 0) | \
    (((IGB_TX0_MSIX_VEC | 0x80)) << 8)
#define IGB_IVAR_TEST_CFG1 \
    (((IGB_RX1_MSIX_VEC | 0x80)) << 0) | \
    (((IGB_TX1_MSIX_VEC | 0x80)) << 8)

#define IGB_RING_LEN (0x1000)

static void igb_macreg_write(QIGB *d, uint32_t reg, uint32_t val)
{
    QIGB_PCI *d_pci = container_of(d, QIGB_PCI, igb);
    qpci_io_writel(&d_pci->pci_dev, d_pci->mac_regs, reg, val);
}

static uint32_t igb_macreg_read(QIGB *d, uint32_t reg)
{
    QIGB_PCI *d_pci = container_of(d, QIGB_PCI, igb);
    return qpci_io_readl(&d_pci->pci_dev, d_pci->mac_regs, reg);
}

void igb_tx_ring_push(QIGB *d, void *descr, uint8_t queue_index)
{
    QIGB_PCI *d_pci = container_of(d, QIGB_PCI, igb);
    uint32_t tail = igb_macreg_read(d, E1000_TDT_REG(queue_index));
    uint32_t len = igb_macreg_read(d, E1000_TDLEN_REG(queue_index)) / E1000_RING_DESC_LEN;

    qtest_memwrite(d_pci->pci_dev.bus->qts,
                   d->tx_ring[queue_index] + tail * E1000_RING_DESC_LEN,
                   descr, E1000_RING_DESC_LEN);
    igb_macreg_write(d, E1000_TDT_REG(queue_index), (tail + 1) % len);

    /* Read WB data for the packet transmitted */
    qtest_memread(d_pci->pci_dev.bus->qts,
                  d->tx_ring[queue_index] + tail * E1000_RING_DESC_LEN,
                  descr, E1000_RING_DESC_LEN);
}

void igb_rx_ring_push(QIGB *d, void *descr, uint8_t queue_index)
{
    QIGB_PCI *d_pci = container_of(d, QIGB_PCI, igb);
    uint32_t tail = igb_macreg_read(d, E1000_RDT_REG(queue_index));
    uint32_t len = igb_macreg_read(d, E1000_RDLEN_REG(queue_index)) / E1000_RING_DESC_LEN;

    qtest_memwrite(d_pci->pci_dev.bus->qts,
                   d->rx_ring[queue_index] + tail * E1000_RING_DESC_LEN,
                   descr, E1000_RING_DESC_LEN);
    igb_macreg_write(d, E1000_RDT_REG(queue_index), (tail + 1) % len);

    /* Read WB data for the packet received */
    qtest_memread(d_pci->pci_dev.bus->qts,
                  d->rx_ring[queue_index] + tail * E1000_RING_DESC_LEN,
                  descr, E1000_RING_DESC_LEN);
}

void igb_wait_isr(QIGB *d, uint16_t msg_id)
{
    QIGB_PCI *d_pci = container_of(d, QIGB_PCI, igb);
    guint64 end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    do {
        if (qpci_msix_pending(&d_pci->pci_dev, msg_id)) {
            return;
        }
        qtest_clock_step(d_pci->pci_dev.bus->qts, 10000);
    } while (g_get_monotonic_time() < end_time);

    g_error("Timeout expired");
}

static void igb_pci_destructor(QOSGraphObject *obj)
{
    QIGB_PCI *epci = (QIGB_PCI *) obj;
    qpci_iounmap(&epci->pci_dev, epci->mac_regs);
    qpci_msix_disable(&epci->pci_dev);
}

static void igb_pci_start_hw(QOSGraphObject *obj)
{
    QIGB_PCI *d = (QIGB_PCI *) obj;
    uint32_t val;

    /* Enable the device */
    qpci_device_enable(&d->pci_dev);

    /* Reset the device */
    val = igb_macreg_read(&d->igb, E1000_CTRL);
    igb_macreg_write(&d->igb, E1000_CTRL, val | E1000_CTRL_RST | E1000_CTRL_SLU);

    /* Enable and configure MSI-X */
    qpci_msix_enable(&d->pci_dev);
    igb_macreg_write(&d->igb, E1000_IVAR_IGB, IGB_IVAR_TEST_CFG0);
    igb_macreg_write(&d->igb, E1000_IVAR_IGB + 4, IGB_IVAR_TEST_CFG1);

    /* Check the device status - link and speed */
    val = igb_macreg_read(&d->igb, E1000_STATUS);
    g_assert_cmphex(val & (E1000_STATUS_LU | E1000_STATUS_ASDV_1000),
        ==, E1000_STATUS_LU | E1000_STATUS_ASDV_1000);

    /* Initialize TX/RX logic */
    igb_macreg_write(&d->igb, E1000_RCTL, 0);
    igb_macreg_write(&d->igb, E1000_TCTL, 0);

    /* Notify the device that the driver is ready */
    val = igb_macreg_read(&d->igb, E1000_CTRL_EXT);
    igb_macreg_write(&d->igb, E1000_CTRL_EXT,
        val | E1000_CTRL_EXT_DRV_LOAD);

    for (int i = 0; i < IGB_NUM_QUEUES; i++) {
        igb_macreg_write(&d->igb, E1000_TDBAL_REG(i),
                            (uint32_t) d->igb.tx_ring[i]);
        igb_macreg_write(&d->igb, E1000_TDBAH_REG(0),
                            (uint32_t) (d->igb.tx_ring[i] >> 32));
        igb_macreg_write(&d->igb, E1000_TDLEN_REG(i), IGB_RING_LEN);
        igb_macreg_write(&d->igb, E1000_TDT_REG(i), 0);
        igb_macreg_write(&d->igb, E1000_TDH_REG(i), 0);
        igb_macreg_write(&d->igb, E1000_TXDCTL_REG(i), E1000_TXDCTL_QUEUE_ENABLE);
    }

    /* Enable transmit */
    igb_macreg_write(&d->igb, E1000_TCTL, E1000_TCTL_EN);

    for (int i = 0; i < IGB_NUM_QUEUES; i++) {
        igb_macreg_write(&d->igb, E1000_RDBAL_REG(i),
                            (uint32_t) d->igb.rx_ring[i]);
        igb_macreg_write(&d->igb, E1000_RDBAH_REG(0),
                            (uint32_t) (d->igb.rx_ring[i] >> 32));
        igb_macreg_write(&d->igb, E1000_RDLEN_REG(i), IGB_RING_LEN);
        igb_macreg_write(&d->igb, E1000_RDT_REG(i), 0);
        igb_macreg_write(&d->igb, E1000_RDH_REG(i), 0);
        igb_macreg_write(&d->igb, E1000_RXDCTL_REG(i), E1000_RXDCTL_QUEUE_ENABLE);
    }

    /* Enable receive */
    igb_macreg_write(&d->igb, E1000_RCTL, E1000_RCTL_EN  |
                                        E1000_RCTL_UPE |
                                        E1000_RCTL_MPE);

    /* Enable all interrupts */
    igb_macreg_write(&d->igb, E1000_IMS, 0xFFFFFFFF);
    igb_macreg_write(&d->igb, E1000_EIMS, 0xFFFFFFFF);
}

static void *igb_pci_get_driver(void *obj, const char *interface)
{
    QIGB_PCI *epci = obj;
    if (!g_strcmp0(interface, "igb-if")) {
        return &epci->igb;
    }

    /* implicit contains */
    if (!g_strcmp0(interface, "pci-device")) {
        return &epci->pci_dev;
    }

    fprintf(stderr, "%s not present in igb\n", interface);
    g_assert_not_reached();
}

static void igb_foreach_callback(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice *res = data;
    memcpy(res, dev, sizeof(QPCIDevice));
    g_free(dev);
}

static void *igb_pci_create(void *pci_bus, QGuestAllocator *alloc,
                               void *addr)
{
    QIGB_PCI *d = g_new0(QIGB_PCI, 1);
    QPCIBus *bus = pci_bus;
    QPCIAddress *address = addr;

    qpci_device_foreach(bus, address->vendor_id, address->device_id,
                        igb_foreach_callback, &d->pci_dev);

    /* Map BAR0 (mac registers) */
    d->mac_regs = qpci_iomap(&d->pci_dev, 0, NULL);

    for (int i = 0; i < IGB_NUM_QUEUES; i++) {
        /* Allocate and setup TX ring */
        d->igb.tx_ring[i] = guest_alloc(alloc, IGB_RING_LEN);
        g_assert(d->igb.tx_ring[i] != 0);
        /* Allocate and setup RX ring */
        d->igb.rx_ring[i] = guest_alloc(alloc, IGB_RING_LEN);
        g_assert(d->igb.rx_ring[i] != 0);
    }

    d->obj.get_driver = igb_pci_get_driver;
    d->obj.start_hw = igb_pci_start_hw;
    d->obj.destructor = igb_pci_destructor;

    return &d->obj;
}

static void igb_register_nodes(void)
{
    QPCIAddress addr = {
        .vendor_id = PCI_VENDOR_ID_INTEL,
        .device_id = E1000_DEV_ID_82576,
    };

    /* FIXME: every test using this node needs to setup a -netdev socket,id=hs0
     * otherwise QEMU is not going to start */
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "netdev=hs0",
    };
    add_qpci_address(&opts, &addr);

    qos_node_create_driver("igb", igb_pci_create);
    qos_node_consumes("igb", "pci-bus", &opts);
}

libqos_init(igb_register_nodes);
