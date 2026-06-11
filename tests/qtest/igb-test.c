/*
 * QTest testcase for igb NIC
 *
 * Copyright (c) 2022-2023 Red Hat, Inc.
 * Copyright (c) 2015 Ravello Systems LTD (http://ravellosystems.com)
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Akihiko Odaki <akihiko.odaki@daynix.com>
 * Dmitry Fleytman <dmitry@daynix.com>
 * Leonid Bloch <leonid@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */


#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "libqos/pci-pc.h"
#include "net/eth.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "libqos/libqos-malloc.h"
#include "libqos/e1000e.h"
#include "hw/net/igb_regs.h"
#include "hw/pci/pci_regs.h"

#ifndef _WIN32

static const struct eth_header packet = {
    .h_dest = E1000E_ADDRESS,
    .h_source = E1000E_ADDRESS,
};

/*
 * Calculate TRL rate factor for a given target rate.
 *
 * Rate Factor = 1 Gbps / Target Rate.
 *
 * The hardware TRLRC register stores the rate factor as a fixed-point number
 * with 14 fractional bits. Scale 1Gbps by 2^14 before dividing to preserve
 * precision in integer arithmetic.
 */
static uint32_t igb_trl_get_rate_factor(uint64_t rate_bytes_s)
{
    return (E1000_LINK_RATE_1GBPS << 14) / rate_bytes_s;
}

static void igb_trl_enable(QE1000E *d, uint64_t rate_bytes_s)
{
    /* Select Queue 0 */
    e1000e_macreg_write(d, E1000_TRLDQSEL, 0);
    /* Enable TRL with target rate */
    e1000e_macreg_write(d, E1000_TRLRC,
                    E1000_TRLRC_RS_ENA | igb_trl_get_rate_factor(rate_bytes_s));
}

static void igb_trl_disable(QE1000E *d)
{
    /* Select Queue 0 */
    e1000e_macreg_write(d, E1000_TRLDQSEL, 0);
    /* Disable TRL */
    e1000e_macreg_write(d, E1000_TRLRC, 0);
}

static void igb_msix_clear_pending(QPCIDevice *dev, uint16_t entry)
{
    uint64_t vector_offset =
        dev->msix_table_off + (entry * PCI_MSIX_ENTRY_SIZE);
    uint32_t val = qpci_io_readl(dev, dev->msix_table_bar,
                                vector_offset + PCI_MSIX_ENTRY_VECTOR_CTRL);

    /* Unmask (clears pending bit in QEMU) */
    qpci_io_writel(dev, dev->msix_table_bar,
                    vector_offset + PCI_MSIX_ENTRY_VECTOR_CTRL,
                    val & ~PCI_MSIX_ENTRY_CTRL_MASKBIT);

    /* Mask again */
    qpci_io_writel(dev, dev->msix_table_bar,
                    vector_offset + PCI_MSIX_ENTRY_VECTOR_CTRL,
                    val | PCI_MSIX_ENTRY_CTRL_MASKBIT);
}

static void igb_read_tx_tail(QE1000E *d, void *descr)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    uint32_t last_entry;
    uint32_t tail;
    uint32_t len;

    tail = e1000e_macreg_read(d, E1000_TDT_A(0));
    len = e1000e_macreg_read(d, E1000_TDLEN_A(0)) / E1000_RING_DESC_LEN;
    last_entry = (tail + len - 1) % len;

    qtest_memread(d_pci->pci_dev.bus->qts,
                    d->tx_ring + last_entry * E1000_RING_DESC_LEN, descr,
                    E1000_RING_DESC_LEN);
}

static void igb_send_verify(QE1000E *d, int *test_sockets, QGuestAllocator *alloc)
{
    union e1000_adv_tx_desc descr;
    char buffer[64];
    int ret;
    uint32_t recv_len;

    /* Prepare test data buffer */
    uint64_t data = guest_alloc(alloc, sizeof(buffer));
    memwrite(data, &packet, sizeof(packet));

    /* Prepare TX descriptor */
    memset(&descr, 0, sizeof(descr));
    descr.read.buffer_addr = cpu_to_le64(data);
    descr.read.cmd_type_len = cpu_to_le32(E1000_TXD_CMD_RS   |
                                          E1000_TXD_CMD_EOP  |
                                          E1000_TXD_DTYP_D   |
                                          sizeof(buffer));

    /* Put descriptor to the ring */
    e1000e_tx_ring_push(d, &descr);

    /* Wait for TX WB interrupt */
    e1000e_wait_isr(d, E1000E_TX0_MSG_ID);

    /*
     * Read the descriptor back from guest memory again, now that WB has
     * occurred. This is required because if the packet was delayed by the
     * trasmit rate limiter, e1000e_tx_ring_push read it back before QEMU
     * actually processed it.
     */
    igb_read_tx_tail(d, &descr);

    /* Check DD bit */
    g_assert_cmphex(le32_to_cpu(descr.wb.status) & E1000_TXD_STAT_DD, ==,
                    E1000_TXD_STAT_DD);

    /* Check data sent to the backend */
    ret = recv(test_sockets[0], &recv_len, sizeof(recv_len), 0);
    g_assert_cmpint(ret, == , sizeof(recv_len));
    ret = recv(test_sockets[0], buffer, sizeof(buffer), 0);
    g_assert_cmpint(ret, ==, sizeof(buffer));
    g_assert_false(memcmp(buffer, &packet, sizeof(packet)));

    /* Free test data buffer */
    guest_free(alloc, data);
}

static void igb_receive_verify(QE1000E *d, int *test_sockets, QGuestAllocator *alloc)
{
    union e1000_adv_rx_desc descr;

    struct eth_header test_iov = packet;
    int len = htonl(sizeof(packet));
    struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len = sizeof(len),
        },{
            .iov_base = &test_iov,
            .iov_len = sizeof(packet),
        },
    };

    char buffer[64];
    int ret;

    /* Send a dummy packet to device's socket*/
    ret = iov_send(test_sockets[0], iov, 2, 0, sizeof(len) + sizeof(packet));
    g_assert_cmpint(ret, == , sizeof(packet) + sizeof(len));

    /* Prepare test data buffer */
    uint64_t data = guest_alloc(alloc, sizeof(buffer));

    /* Prepare RX descriptor */
    memset(&descr, 0, sizeof(descr));
    descr.read.pkt_addr = cpu_to_le64(data);

    /* Put descriptor to the ring */
    e1000e_rx_ring_push(d, &descr);

    /* Wait for TX WB interrupt */
    e1000e_wait_isr(d, E1000E_RX0_MSG_ID);

    /* Check DD bit */
    g_assert_cmphex(le32_to_cpu(descr.wb.upper.status_error) &
        E1000_RXD_STAT_DD, ==, E1000_RXD_STAT_DD);

    /* Check data sent to the backend */
    memread(data, buffer, sizeof(buffer));
    g_assert_false(memcmp(buffer, &packet, sizeof(packet)));

    /* Free test data buffer */
    guest_free(alloc, data);
}

/* Returns the amount of microseconds it takes to transmit a 64-byte packet.*/
static uint64_t igb_send_and_measure(QE1000E *d, int *test_sockets,
                                     QGuestAllocator *alloc)
{
    QE1000E_PCI *d_pci = container_of(d, QE1000E_PCI, e1000e);
    QPCIDevice *pdev = &d_pci->pci_dev;
    QTestState *qts = global_qtest;
    uint64_t start_time, end_time;

    /* Clear any stale pending interrupt for this vector before we start */
    igb_msix_clear_pending(pdev, E1000E_TX0_MSG_ID);

    /* Clear EICR to allow new interrupts to be raised in MSI-X mode */
    e1000e_macreg_write(d, E1000_EICR, 0xFFFFFFFF);

    /* Send a test packet and measure the time it takes. */
    start_time = qtest_clock_step(qts, 1);
    igb_send_verify(d, test_sockets, alloc);
    end_time = qtest_clock_step(qts, 1);

    return end_time - start_time;
}

static void test_e1000e_init(void *obj, void *data, QGuestAllocator * alloc)
{
    /* init does nothing */
}

static void test_igb_tx(void *obj, void *data, QGuestAllocator * alloc)
{
    QE1000E_PCI *e1000e = obj;
    QE1000E *d = &e1000e->e1000e;
    QOSGraphObject *e_object = obj;
    QPCIDevice *dev = e_object->get_driver(e_object, "pci-device");

    /* FIXME: add spapr support */
    if (qpci_check_buggy_msi(dev)) {
        return;
    }

    igb_send_verify(d, data, alloc);
}

static void test_igb_rx(void *obj, void *data, QGuestAllocator * alloc)
{
    QE1000E_PCI *e1000e = obj;
    QE1000E *d = &e1000e->e1000e;
    QOSGraphObject *e_object = obj;
    QPCIDevice *dev = e_object->get_driver(e_object, "pci-device");

    /* FIXME: add spapr support */
    if (qpci_check_buggy_msi(dev)) {
        return;
    }

    igb_receive_verify(d, data, alloc);
}

static void test_igb_multiple_transfers(void *obj, void *data,
                                        QGuestAllocator *alloc)
{
    static const long iterations = 4 * 1024;
    long i;

    QE1000E_PCI *e1000e = obj;
    QE1000E *d = &e1000e->e1000e;
    QOSGraphObject *e_object = obj;
    QPCIDevice *dev = e_object->get_driver(e_object, "pci-device");

    /* FIXME: add spapr support */
    if (qpci_check_buggy_msi(dev)) {
        return;
    }

    for (i = 0; i < iterations; i++) {
        igb_send_verify(d, data, alloc);
        igb_receive_verify(d, data, alloc);
    }

}

static void test_igb_transmit_rate_limiter(void *obj, void *data,
                                           QGuestAllocator *alloc)
{
    QOSGraphObject *e_object = obj;
    QE1000E_PCI *e1000e = obj;
    QPCIDevice *dev = e_object->get_driver(e_object, "pci-device");
    QE1000E *d = &e1000e->e1000e;
    const uint64_t expected_baseline_elapsed = 10 * SCALE_US;
    const uint64_t expected_max_elapsed = 530 * SCALE_US;
    const uint64_t expected_min_elapsed = 500 * SCALE_US;
    uint64_t elapsed;

    if (qpci_check_buggy_msi(dev)) {
        return;
    }

    /* Send a test packet to verify there's no rate limit. */
    elapsed = igb_send_and_measure(d, data, alloc);

    /*
     * QEMU doesn't emulate the hardware line rate of the device, so the packet
     * is processed instantly (requiring 0 clock steps in wait_isr, which steps
     * by 10us). Give a 10us margin for error.
     */
    g_assert_cmpint(elapsed, <, expected_baseline_elapsed);

    /*
     * Enable TRL and set the target rate to:
     * (E1000_LINK_RATE_1GBPS / 1000) = 125 KB/s.
     */
    igb_trl_enable(d, E1000_LINK_RATE_1GBPS / 1000);

    /*
     * Send Packet 1 (no delay)
     * The first packet triggers rate limiting for subsequent transmissions.
     */
    elapsed = igb_send_and_measure(d, data, alloc);
    g_assert_cmpint(elapsed, <, expected_baseline_elapsed);

    /* Send Packet 2 (should be delayed) */
    elapsed = igb_send_and_measure(d, data, alloc);
    /*
     * Expected delay: 64 byte packet / 125 KB/s = 512 us.
     * Since QEMU steps the clock by 10 us on each wait_isr iteration,
     * the delay is rounded up to the next 10 us step, which is 520 us.
     */
    g_assert_cmpint(elapsed, >=, expected_min_elapsed);
    g_assert_cmpint(elapsed, <=, expected_max_elapsed);

    /* Send Packet 3 (should also be delayed) */
    elapsed = igb_send_and_measure(d, data, alloc);
    g_assert_cmpint(elapsed, >=, expected_min_elapsed);
    g_assert_cmpint(elapsed, <=, expected_max_elapsed);

    igb_trl_disable(d);

    /* Send Packet 4 (no delay since TRL is disabled) */
    elapsed = igb_send_and_measure(d, data, alloc);
    g_assert_cmpint(elapsed, <, expected_baseline_elapsed);
}

static void data_test_clear(void *sockets)
{
    int *test_sockets = sockets;

    close(test_sockets[0]);
    qos_invalidate_command_line();
    close(test_sockets[1]);
    g_free(test_sockets);
}

static void *data_test_init(GString *cmd_line, void *arg)
{
    int *test_sockets = g_new(int, 2);
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, test_sockets);
    g_assert_cmpint(ret, != , -1);

    g_string_append_printf(cmd_line, " -netdev socket,fd=%d,id=hs0 ",
                           test_sockets[1]);

    g_test_queue_destroy(data_test_clear, test_sockets);
    return test_sockets;
}

#endif

static void *data_test_init_no_socket(GString *cmd_line, void *arg)
{
    g_string_append(cmd_line, " -netdev hubport,hubid=0,id=hs0 ");
    return arg;
}

static void test_igb_hotplug(void *obj, void *data, QGuestAllocator * alloc)
{
    QTestState *qts = global_qtest;  /* TODO: get rid of global_qtest here */
    QE1000E_PCI *dev = obj;

    if (dev->pci_dev.bus->not_hotpluggable) {
        g_test_skip("pci bus does not support hotplug");
        return;
    }

    qtest_qmp_device_add(qts, "igb", "igb_net", "{'addr': '0x06'}");
    qpci_unplug_acpi_device_test(qts, "igb_net", 0x06);
}

static void register_igb_test(void)
{
    QOSGraphTestOptions opts = { 0 };

#ifndef _WIN32
    opts.before = data_test_init,
    qos_add_test("init", "igb", test_e1000e_init, &opts);
    qos_add_test("tx", "igb", test_igb_tx, &opts);
    qos_add_test("rx", "igb", test_igb_rx, &opts);
    qos_add_test("multiple_transfers", "igb",
                 test_igb_multiple_transfers, &opts);
    qos_add_test("transmit_rate_limiter", "igb",
                 test_igb_transmit_rate_limiter, &opts);
#endif

    opts.before = data_test_init_no_socket;
    qos_add_test("hotplug", "igb", test_igb_hotplug, &opts);
}

libqos_init(register_igb_test);
