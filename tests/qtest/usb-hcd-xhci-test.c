/*
 * QTest testcase for USB xHCI controller
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest.h"
#include "libqtest-single.h"
#include "libqos/libqos.h"
#include "libqos/libqos-pc.h"
#include "libqos/usb.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"
#include "hw/usb/hcd-xhci.h"

typedef struct TestData {
    const char *device;
    uint32_t fingerprint;
} TestData;

/*** Test Setup & Teardown ***/

/* Transfer-Ring state */
typedef struct XHCIQTRState {
    uint64_t addr; /* In-memory ring */

    uint32_t trb_entries;
    uint32_t trb_idx;
    uint32_t trb_c;
} XHCIQTRState;

typedef struct XHCIQSlotState {
    /* In-memory device context array */
    uint64_t device_context;
    XHCIQTRState transfer_ring[31]; /* 1 for each EP */
} XHCIQSlotState;

typedef struct XHCIQState {
    /* QEMU PCI variables */
    QOSState *parent;
    QPCIDevice *dev;
    QPCIBar bar;
    uint64_t barsize;
    uint32_t fingerprint;
    uint64_t guest_msix_addr;
    uint32_t msix_data;

    /* In-memory arrays */
    uint64_t dc_base_array;
    uint64_t event_ring_seg;
    XHCIQTRState command_ring;
    XHCIQTRState event_ring;

    /* Host controller properties */
    uint32_t rtoff, dboff;
    uint32_t maxports, maxslots, maxintrs;

    /* Current properties */
    uint32_t slotid; /* enabled slot id (only enable one) */

    XHCIQSlotState slots[32];
} XHCIQState;

#define XHCI_QEMU_ID (PCI_DEVICE_ID_REDHAT_XHCI << 16 | \
                      PCI_VENDOR_ID_REDHAT)
#define XHCI_NEC_ID (PCI_DEVICE_ID_NEC_UPD720200 << 16 | \
                     PCI_VENDOR_ID_NEC)
#define XHCI_TI_ID  (PCI_DEVICE_ID_TI_TUSB73X0 << 16 | \
                     PCI_VENDOR_ID_TI)

/**
 * Locate, verify, and return a handle to the XHCI device.
 */
static QPCIDevice *get_xhci_device(QTestState *qts)
{
    QPCIDevice *xhci;
    QPCIBus *pcibus;

    pcibus = qpci_new_pc(qts, NULL);

    /* Find the XHCI PCI device and verify it's the right one. */
    xhci = qpci_device_find(pcibus, QPCI_DEVFN(0x1D, 0x0));
    g_assert(xhci != NULL);

    return xhci;
}

static void free_xhci_device(QPCIDevice *dev)
{
    QPCIBus *pcibus = dev ? dev->bus : NULL;

    /* libqos doesn't have a function for this, so free it manually */
    g_free(dev);
    qpci_free_pc(pcibus);
}

/**
 * Start a Q35 machine and bookmark a handle to the XHCI device.
 */
G_GNUC_PRINTF(1, 0)
static XHCIQState *xhci_vboot(const char *cli, va_list ap)
{
    XHCIQState *s;

    s = g_new0(XHCIQState, 1);
    s->parent = qtest_pc_vboot(cli, ap);
    alloc_set_flags(&s->parent->alloc, ALLOC_LEAK_ASSERT);

    /* Verify that we have an XHCI device present. */
    s->dev = get_xhci_device(s->parent->qts);
    s->fingerprint = qpci_config_readl(s->dev, PCI_VENDOR_ID);
    s->bar = qpci_iomap(s->dev, 0, &s->barsize);
    /* turns on pci.cmd.iose, pci.cmd.mse and pci.cmd.bme */
    qpci_device_enable(s->dev);

    return s;
}

/**
 * Start a Q35 machine and bookmark a handle to the XHCI device.
 */
G_GNUC_PRINTF(1, 2)
static XHCIQState *xhci_boot(const char *cli, ...)
{
    XHCIQState *s;
    va_list ap;

    va_start(ap, cli);
    s = xhci_vboot(cli, ap);
    va_end(ap);

    return s;
}

static XHCIQState *xhci_boot_dev(const char *device, uint32_t fingerprint)
{
    XHCIQState *s;

    s = xhci_boot("-M q35 "
                  "-device %s,id=xhci,bus=pcie.0,addr=1d.0 "
                  "-drive id=drive0,if=none,file=null-co://,"
                         "file.read-zeroes=on,format=raw", device);
    g_assert_cmphex(s->fingerprint, ==, fingerprint);

    return s;
}

/**
 * Clean up the PCI device, then terminate the QEMU instance.
 */
static void xhci_shutdown(XHCIQState *xhci)
{
    QOSState *qs = xhci->parent;

    free_xhci_device(xhci->dev);
    g_free(xhci);
    qtest_shutdown(qs);
}

/*** tests ***/

static void test_xhci_hotplug(const void *arg)
{
    const TestData *td = arg;
    XHCIQState *s;
    QTestState *qts;

    s = xhci_boot_dev(td->device, td->fingerprint);
    qts = s->parent->qts;

    usb_test_hotplug(qts, "xhci", "1", NULL);

    xhci_shutdown(s);
}

static void test_usb_uas_hotplug(const void *arg)
{
    const TestData *td = arg;
    XHCIQState *s;
    QTestState *qts;

    s = xhci_boot_dev(td->device, td->fingerprint);
    qts = s->parent->qts;

    qtest_qmp_device_add(qts, "usb-uas", "uas", "{}");
    qtest_qmp_device_add(qts, "scsi-hd", "scsihd", "{'drive': 'drive0'}");

    /* TODO:
        UAS HBA driver in libqos, to check that
        added disk is visible after BUS rescan
    */

    qtest_qmp_device_del(qts, "scsihd");
    qtest_qmp_device_del(qts, "uas");

    xhci_shutdown(s);
}

static void test_usb_ccid_hotplug(const void *arg)
{
    const TestData *td = arg;
    XHCIQState *s;
    QTestState *qts;

    s = xhci_boot_dev(td->device, td->fingerprint);
    qts = s->parent->qts;

    qtest_qmp_device_add(qts, "usb-ccid", "ccid", "{}");
    qtest_qmp_device_del(qts, "ccid");
    /* check the device can be added again */
    qtest_qmp_device_add(qts, "usb-ccid", "ccid", "{}");
    qtest_qmp_device_del(qts, "ccid");

    xhci_shutdown(s);
}

static uint64_t xhci_guest_zalloc(XHCIQState *s, uint64_t size)
{
    uint64_t ret;

    ret = guest_alloc(&s->parent->alloc, size);
    g_assert(ret);
    qtest_memset(s->parent->qts, ret, 0, size);

    return ret;
}

static uint32_t xhci_cap_readl(XHCIQState *s, uint64_t addr)
{
    return qpci_io_readl(s->dev, s->bar, XHCI_REGS_OFFSET_CAP + addr);
}

static uint32_t xhci_op_readl(XHCIQState *s, uint64_t addr)
{
    return qpci_io_readl(s->dev, s->bar, XHCI_REGS_OFFSET_OPER + addr);
}

static void xhci_op_writel(XHCIQState *s, uint64_t addr, uint32_t value)
{
    qpci_io_writel(s->dev, s->bar, XHCI_REGS_OFFSET_OPER + addr, value);
}

static uint32_t xhci_port_readl(XHCIQState *s, uint32_t port, uint64_t addr)
{
    return qpci_io_readl(s->dev, s->bar,
                         XHCI_REGS_OFFSET_PORT + port * XHCI_PORT_PR_SZ + addr);
}

static uint32_t xhci_rt_readl(XHCIQState *s, uint64_t addr)
{
    return qpci_io_readl(s->dev, s->bar, s->rtoff + addr);
}

static void xhci_rt_writel(XHCIQState *s, uint64_t addr, uint32_t value)
{
    qpci_io_writel(s->dev, s->bar, s->rtoff + addr, value);
}

static uint32_t xhci_intr_readl(XHCIQState *s, uint32_t intr, uint64_t addr)
{
    return xhci_rt_readl(s, XHCI_INTR_REG_IR0 +
                            intr * XHCI_INTR_IR_SZ + addr);
}


static void xhci_intr_writel(XHCIQState *s, uint32_t intr, uint64_t addr,
                             uint32_t value)
{
    xhci_rt_writel(s, XHCI_INTR_REG_IR0 +
                      intr * XHCI_INTR_IR_SZ + addr, value);
}

static void xhci_db_writel(XHCIQState *s, uint32_t db, uint32_t value)
{
    qpci_io_writel(s->dev, s->bar, s->dboff + db * XHCI_DBELL_DB_SZ, value);
}

static bool xhci_test_isr(XHCIQState *s)
{
    return qpci_msix_test_interrupt(s->dev, 0,
                                    s->guest_msix_addr, s->msix_data);
}

static bool check_event_trb(XHCIQState *s, XHCITRB *trb)
{
    XHCIQTRState *tr = &s->event_ring;
    uint64_t er_addr = tr->addr + tr->trb_idx * TRB_SIZE;
    XHCITRB t;

    qtest_memread(s->parent->qts, er_addr, &t, TRB_SIZE);
    trb->parameter = le64_to_cpu(t.parameter);
    trb->status = le32_to_cpu(t.status);
    trb->control = le32_to_cpu(t.control);

    return ((trb->control & TRB_C) == tr->trb_c);
}

static void consume_event(XHCIQState *s)
{
    XHCIQTRState *tr = &s->event_ring;
    uint64_t er_addr = tr->addr + tr->trb_idx * TRB_SIZE;

    tr->trb_idx++;
    if (tr->trb_idx == tr->trb_entries) {
        tr->trb_idx = 0;
        tr->trb_c ^= 1;
    }
    /* Update ERDP to processed TRB addr and EHB bit, which clears EHB */
    er_addr = tr->addr + tr->trb_idx * TRB_SIZE;
    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERDP_LO,
                     (er_addr & 0xffffffff) | XHCI_ERDP_EHB);
}

static bool try_get_event_trb(XHCIQState *s, XHCITRB *trb)
{
    if (check_event_trb(s, trb)) {
        consume_event(s);
        return true;
    }
    return false;
}

static void wait_event_trb(XHCIQState *s, XHCITRB *trb)
{
    XHCIQTRState *tr = &s->event_ring;
    uint32_t value;
    guint64 end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    /* Wait for event interrupt  */
    while (!xhci_test_isr(s)) {
        if (g_get_monotonic_time() >= end_time) {
            g_error("Timeout expired");
        }
        qtest_clock_step(s->parent->qts, 10000);
    }

    value = xhci_op_readl(s, XHCI_OPER_REG_USBSTS);
    g_assert(value & XHCI_USBSTS_EINT);

    if (0) {
        /*
         * With MSI-X enabled, IMAN IP is cleared after raising the interrupt,
         * but if concurrent events may be occurring, it could be set again.
         */
        value = xhci_intr_readl(s, 0, XHCI_INTR_REG_IMAN);
        g_assert(!(value & XHCI_IMAN_IP));
    }

    if (!check_event_trb(s, trb)) {
        g_assert_not_reached();
    }
    g_assert_cmpint((trb->status >> 24), ==, CC_SUCCESS);
    g_assert((trb->control & TRB_C) == tr->trb_c); /* C bit has been set */

    xhci_op_writel(s, XHCI_OPER_REG_USBSTS, XHCI_USBSTS_EINT); /* clear EINT */

    consume_event(s);
}

static void set_link_trb(XHCIQState *s, uint64_t ring, uint32_t c,
                         uint32_t entries)
{
    XHCITRB trb;

    g_assert(entries > 1);

    memset(&trb, 0, TRB_SIZE);
    trb.parameter = ring;
    trb.control = cpu_to_le32(c | /* C */
                              (TR_LINK << TRB_TYPE_SHIFT) |
                              TRB_LK_TC);
    qtest_memwrite(s->parent->qts, ring + TRB_SIZE * (entries - 1),
                   &trb, TRB_SIZE);
}

static uint64_t queue_trb(XHCIQState *s, XHCIQTRState *tr, const XHCITRB *trb)
{
    uint64_t tr_addr = tr->addr + tr->trb_idx * TRB_SIZE;
    XHCITRB t;

    t.parameter = cpu_to_le64(trb->parameter);
    t.status = cpu_to_le32(trb->status);
    t.control = cpu_to_le32(trb->control | tr->trb_c);

    qtest_memwrite(s->parent->qts, tr_addr, &t, TRB_SIZE);
    tr->trb_idx++;
    /* Last entry contains the link, so wrap back */
    if (tr->trb_idx == tr->trb_entries - 1) {
        set_link_trb(s, tr->addr, tr->trb_c, tr->trb_entries);
        tr->trb_idx = 0;
        tr->trb_c ^= 1;
    }

    return tr_addr;
}

static uint64_t submit_cr_trb(XHCIQState *s, const XHCITRB *trb)
{
    XHCIQTRState *tr = &s->command_ring;
    uint64_t ret;

    ret = queue_trb(s, tr, trb);

    xhci_db_writel(s, 0, 0); /* doorbell host, doorbell 0 (command) */

    return ret;
}

static uint64_t submit_tr_trb(XHCIQState *s, int slot, int ep,
                              const XHCITRB *trb)
{
    XHCIQSlotState *sl = &s->slots[slot];
    XHCIQTRState *tr = &sl->transfer_ring[ep];
    uint64_t ret;

    ret = queue_trb(s, tr, trb);

    xhci_db_writel(s, slot, 1 + ep); /* doorbell slot, EP<ep> target */

    return ret;
}

static void xhci_enable_device(XHCIQState *s)
{
    XHCIQTRState *tr;
    XHCIEvRingSeg ev_seg;
    uint32_t hcsparams1;
    uint32_t value;
    int i;

    s->guest_msix_addr = xhci_guest_zalloc(s, 4);
    s->msix_data = 0x1234abcd;

    qpci_msix_enable(s->dev);
    qpci_msix_set_entry(s->dev, 0, s->guest_msix_addr, s->msix_data);
    qpci_msix_set_masked(s->dev, 0, false);

    hcsparams1 = xhci_cap_readl(s, XHCI_HCCAP_REG_HCSPARAMS1);
    s->maxports = (hcsparams1 >> 24) & 0xff;
    s->maxintrs = (hcsparams1 >> 8) & 0x3ff;
    s->maxslots = hcsparams1 & 0xff;

    s->dboff = xhci_cap_readl(s, XHCI_HCCAP_REG_DBOFF);
    s->rtoff = xhci_cap_readl(s, XHCI_HCCAP_REG_RTSOFF);

    s->dc_base_array = xhci_guest_zalloc(s, 0x800);
    s->event_ring_seg = xhci_guest_zalloc(s, 0x100);

    /* Arbitrary small sizes so we can make them wrap */
    tr = &s->command_ring;
    tr->addr = xhci_guest_zalloc(s, 0x1000);
    tr->trb_entries = 0x20;
    tr->trb_c = 1;

    tr = &s->event_ring;
    tr->addr = xhci_guest_zalloc(s, 0x1000);
    tr->trb_entries = 0x10;
    tr->trb_c = 1;

    tr = &s->event_ring;
    ev_seg.addr_low = cpu_to_le32(tr->addr & 0xffffffff);
    ev_seg.addr_high = cpu_to_le32(tr->addr >> 32);
    ev_seg.size = cpu_to_le32(tr->trb_entries);
    ev_seg.rsvd = 0;
    qtest_memwrite(s->parent->qts, s->event_ring_seg, &ev_seg, sizeof(ev_seg));

    xhci_op_writel(s, XHCI_OPER_REG_USBCMD, XHCI_USBCMD_HCRST);
    do {
        value = xhci_op_readl(s, XHCI_OPER_REG_USBSTS);
    } while (value & XHCI_USBSTS_CNR);

    xhci_op_writel(s, XHCI_OPER_REG_CONFIG, s->maxslots);

    xhci_op_writel(s, XHCI_OPER_REG_DCBAAP_LO, s->dc_base_array & 0xffffffff);
    xhci_op_writel(s, XHCI_OPER_REG_DCBAAP_HI, s->dc_base_array >> 32);

    tr = &s->command_ring;
    xhci_op_writel(s, XHCI_OPER_REG_CRCR_LO,
                   (tr->addr & 0xffffffff) | tr->trb_c);
    xhci_op_writel(s, XHCI_OPER_REG_CRCR_HI, tr->addr >> 32);

    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERSTSZ, 1);

    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERSTBA_LO,
                     s->event_ring_seg & 0xffffffff);
    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERSTBA_HI,
                     s->event_ring_seg >> 32);

    /* ERDP */
    tr = &s->event_ring;
    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERDP_LO, tr->addr & 0xffffffff);
    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERDP_HI, tr->addr >> 32);

    xhci_op_writel(s, XHCI_OPER_REG_USBCMD, XHCI_USBCMD_RS | XHCI_USBCMD_INTE);

    /* Enable interrupts on ER IMAN */
    xhci_intr_writel(s, 0, XHCI_INTR_REG_IMAN, XHCI_IMAN_IE);

    /* Ensure there is no interrupt pending */
    g_assert(!xhci_test_isr(s));

    /* Query ports */
    for (i = 0; i < s->maxports; i++) {
        value = xhci_port_readl(s, i, 0); /* PORTSC */

        /* First port should be attached and enabled if we have usb-storage */
        if (qtest_has_device("usb-storage") && i == 0) {
            g_assert(value & XHCI_PORTSC_CCS);
            g_assert(value & XHCI_PORTSC_PED);
            /* Port Speed must be identified (non-zero) */
            g_assert(((value >> XHCI_PORTSC_SPEED_SHIFT) &
                      XHCI_PORTSC_SPEED_MASK) != 0);
        } else {
            g_assert(!(value & XHCI_PORTSC_CCS));
            g_assert(!(value & XHCI_PORTSC_PED));
            g_assert(((value >> XHCI_PORTSC_PLS_SHIFT) &
                      XHCI_PORTSC_PLS_MASK) == 5);
        }
    }
}

/* XXX: what should these values be? */
#define TRB_MAX_PACKET_SIZE 0x200
#define TRB_AVERAGE_LENGTH  0x200

static void xhci_enable_slot(XHCIQState *s)
{
    XHCIQTRState *tr;
    uint64_t input_context;
    XHCITRB trb;
    uint64_t tag;
    g_autofree void *mem = g_malloc0(0x1000); /* buffer for writing to guest */
    uint32_t *dc; /* device context */

    /* Issue a command ring enable slot */
    memset(&trb, 0, TRB_SIZE);
    trb.control |= CR_ENABLE_SLOT << TRB_TYPE_SHIFT;
    trb.control |= TRB_TR_IOC;
    tag = submit_cr_trb(s, &trb);
    wait_event_trb(s, &trb);
    g_assert_cmphex(trb.parameter , ==, tag);
    g_assert_cmpint(TRB_TYPE(trb), ==, ER_COMMAND_COMPLETE);
    s->slotid = (trb.control >> TRB_CR_SLOTID_SHIFT) & 0xff;

    /* 32-byte input context size, should check HCCPARAMS1 for 64-byte size */
    input_context = xhci_guest_zalloc(s, 0x420);

    /* Set input control context */
    memset(mem, 0, 0x420);
    ((uint32_t *)mem)[1] = cpu_to_le32(0x3); /* Add device contexts 0 and 1 */

    /* Slot context */
    dc = mem + 1 * 0x20;
    dc[0] = cpu_to_le32(1 << 27); /* 1 context entry */
    dc[1] = cpu_to_le32(1 << 16); /* 1 port number */

    /* Endpoint 0 context */
    tr = &s->slots[s->slotid].transfer_ring[0];
    tr->addr = xhci_guest_zalloc(s, 0x1000);
    tr->trb_entries = 0x10;
    tr->trb_c = 1;

    dc = mem + 2 * 0x20;
    dc[0] = 0;
    dc[1] = cpu_to_le32((ET_CONTROL << EP_TYPE_SHIFT) |
                        (TRB_MAX_PACKET_SIZE << 16));
    dc[2] = cpu_to_le32((tr->addr & 0xffffffff) | 1); /* DCS=1 */
    dc[3] = cpu_to_le32(tr->addr >> 32);
    dc[4] = cpu_to_le32(TRB_AVERAGE_LENGTH);
    qtest_memwrite(s->parent->qts, input_context, mem, 0x420);

    s->slots[s->slotid].device_context = xhci_guest_zalloc(s, 0x400);

    ((uint64_t *)mem)[0] = cpu_to_le64(s->slots[s->slotid].device_context);
    qtest_memwrite(s->parent->qts, s->dc_base_array + 8 * s->slotid, mem, 8);

    /* Issue a command ring address device */
    memset(&trb, 0, TRB_SIZE);
    trb.parameter = input_context;
    trb.control |= CR_ADDRESS_DEVICE << TRB_TYPE_SHIFT;
    trb.control |= s->slotid << TRB_CR_SLOTID_SHIFT;
    tag = submit_cr_trb(s, &trb);
    wait_event_trb(s, &trb);
    g_assert_cmphex(trb.parameter , ==, tag);
    g_assert_cmpint(TRB_TYPE(trb), ==, ER_COMMAND_COMPLETE);

    guest_free(&s->parent->alloc, input_context);

    /* Check EP0 is running */
    qtest_memread(s->parent->qts, s->slots[s->slotid].device_context, mem, 0x400);
    g_assert((((uint32_t *)mem)[8] & 0x3) == EP_RUNNING);
}

static void xhci_enable_msd_bulk_endpoints(XHCIQState *s)
{
    XHCIQTRState *tr;
    uint64_t input_context;
    XHCITRB trb;
    uint64_t tag;
    g_autofree void *mem = g_malloc0(0x1000); /* buffer for writing to guest */
    uint32_t *dc; /* device context */

    /* Configure 2 more endpoints */

    /* 32-byte input context size, should check HCCPARAMS1 for 64-byte size */
    input_context = xhci_guest_zalloc(s, 0x420);

    /* Set input control context */
    memset(mem, 0, 0x420);
    ((uint32_t *)mem)[1] = cpu_to_le32(0x19); /* Add device contexts 0, 3, 4 */

    /* Slot context */
    dc = mem + 1 * 0x20;
    dc[0] = cpu_to_le32(1 << 27); /* 1 context entry */
    dc[1] = cpu_to_le32(1 << 16); /* 1 port number */

    /* Endpoint 1 (IN) context */
    tr = &s->slots[s->slotid].transfer_ring[2];
    tr->addr = xhci_guest_zalloc(s, 0x1000);
    tr->trb_entries = 0x10;
    tr->trb_c = 1;

    dc = mem + 4 * 0x20;
    dc[0] = 0;
    dc[1] = cpu_to_le32((ET_BULK_IN << EP_TYPE_SHIFT) |
                        (TRB_MAX_PACKET_SIZE << 16));
    dc[2] = cpu_to_le32((tr->addr & 0xffffffff) | 1); /* DCS=1 */
    dc[3] = cpu_to_le32(tr->addr >> 32);
    dc[4] = cpu_to_le32(TRB_AVERAGE_LENGTH);

    /* Endpoint 2 (OUT) context */
    tr = &s->slots[s->slotid].transfer_ring[3];
    tr->addr = xhci_guest_zalloc(s, 0x1000);
    tr->trb_entries = 0x10;
    tr->trb_c = 1;

    dc = mem + 5 * 0x20;
    dc[0] = 0;
    dc[1] = cpu_to_le32((ET_BULK_OUT << EP_TYPE_SHIFT) |
                        (TRB_MAX_PACKET_SIZE << 16));
    dc[2] = cpu_to_le32((tr->addr & 0xffffffff) | 1); /* DCS=1 */
    dc[3] = cpu_to_le32(tr->addr >> 32);
    dc[4] = cpu_to_le32(TRB_AVERAGE_LENGTH);
    qtest_memwrite(s->parent->qts, input_context, mem, 0x420);

    /* Issue a command ring configure endpoint */
    memset(&trb, 0, TRB_SIZE);
    trb.parameter = input_context;
    trb.control |= CR_CONFIGURE_ENDPOINT << TRB_TYPE_SHIFT;
    trb.control |= s->slotid << TRB_CR_SLOTID_SHIFT;
    tag = submit_cr_trb(s, &trb);
    wait_event_trb(s, &trb);
    g_assert_cmphex(trb.parameter , ==, tag);
    g_assert_cmpint(TRB_TYPE(trb), ==, ER_COMMAND_COMPLETE);

    guest_free(&s->parent->alloc, input_context);

    /* Check EPs are running */
    qtest_memread(s->parent->qts, s->slots[s->slotid].device_context, mem, 0x400);
    g_assert((((uint32_t *)mem)[1*8] & 0x3) == EP_RUNNING);
    g_assert((((uint32_t *)mem)[3*8] & 0x3) == EP_RUNNING);
    g_assert((((uint32_t *)mem)[4*8] & 0x3) == EP_RUNNING);
}

static void xhci_disable_device(XHCIQState *s)
{
    int i;

    /* Shut it down */
    qpci_msix_disable(s->dev);

    guest_free(&s->parent->alloc, s->slots[s->slotid].device_context);
    for (i = 0; i < 31; i++) {
        guest_free(&s->parent->alloc,
                   s->slots[s->slotid].transfer_ring[i].addr);
    }
    guest_free(&s->parent->alloc, s->event_ring.addr);
    guest_free(&s->parent->alloc, s->command_ring.addr);
    guest_free(&s->parent->alloc, s->event_ring_seg);
    guest_free(&s->parent->alloc, s->dc_base_array);
    guest_free(&s->parent->alloc, s->guest_msix_addr);
}

struct QEMU_PACKED usb_msd_cbw {
    uint32_t sig;
    uint32_t tag;
    uint32_t data_len;
    uint8_t flags;
    uint8_t lun;
    uint8_t cmd_len;
    uint8_t cmd[16];
};

struct QEMU_PACKED usb_msd_csw {
    uint32_t sig;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
};

static ssize_t xhci_submit_scsi_cmd(XHCIQState *s,
                                    const uint8_t *cmd, uint8_t cmd_len,
                                    void *data, uint32_t data_len,
                                    bool data_in)
{
    struct usb_msd_cbw cbw;
    struct usb_msd_csw csw;
    uint64_t trb_data;
    XHCITRB trb;
    uint64_t tag;

    /* TRB data payload */
    trb_data = xhci_guest_zalloc(s, data_len > sizeof(cbw) ? data_len : sizeof(cbw));

    memset(&cbw, 0, sizeof(cbw));
    cbw.sig = cpu_to_le32(0x43425355);
    cbw.tag = cpu_to_le32(0);
    cbw.data_len = cpu_to_le32(data_len);
    cbw.flags = data_in ? 0x80 : 0x00;
    cbw.lun = 0;
    cbw.cmd_len = cmd_len; /* cmd len */
    memcpy(cbw.cmd, cmd, cmd_len);
    qtest_memwrite(s->parent->qts, trb_data, &cbw, sizeof(cbw));

    /* Issue a transfer ring ep 3 data (out) */
    memset(&trb, 0, TRB_SIZE);
    trb.parameter = trb_data;
    trb.status = sizeof(cbw);
    trb.control |= TR_NORMAL << TRB_TYPE_SHIFT;
    trb.control |= TRB_TR_IOC;
    tag = submit_tr_trb(s, s->slotid, 3, &trb);
    wait_event_trb(s, &trb);
    g_assert_cmphex(trb.parameter, ==, tag);
    g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);

    if (data_in) {
        g_assert(data_len);

        /* Issue a transfer ring ep 2 data (in) */
        memset(&trb, 0, TRB_SIZE);
        trb.parameter = trb_data;
        trb.status = data_len; /* data_len bytes, no more packets */
        trb.control |= TR_NORMAL << TRB_TYPE_SHIFT;
        trb.control |= TRB_TR_IOC;
        tag = submit_tr_trb(s, s->slotid, 2, &trb);
        wait_event_trb(s, &trb);
        g_assert_cmphex(trb.parameter, ==, tag);
        g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);

        qtest_memread(s->parent->qts, trb_data, data, data_len);
    } else if (data_len) {
        qtest_memwrite(s->parent->qts, trb_data, data, data_len);

        /* Issue a transfer ring ep 3 data (out) */
        memset(&trb, 0, TRB_SIZE);
        trb.parameter = trb_data;
        trb.status = data_len; /* data_len bytes, no more packets */
        trb.control |= TR_NORMAL << TRB_TYPE_SHIFT;
        trb.control |= TRB_TR_IOC;
        tag = submit_tr_trb(s, s->slotid, 3, &trb);
        wait_event_trb(s, &trb);
        g_assert_cmphex(trb.parameter, ==, tag);
        g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);
    } else {
        /* No data */
    }

    /* Issue a transfer ring ep 2 data (in) */
    memset(&trb, 0, TRB_SIZE);
    trb.parameter = trb_data;
    trb.status = sizeof(csw);
    trb.control |= TR_NORMAL << TRB_TYPE_SHIFT;
    trb.control |= TRB_TR_IOC;
    tag = submit_tr_trb(s, s->slotid, 2, &trb);
    wait_event_trb(s, &trb);
    g_assert_cmphex(trb.parameter, ==, tag);
    g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);

    qtest_memread(s->parent->qts, trb_data, &csw, sizeof(csw));

    guest_free(&s->parent->alloc, trb_data);

    g_assert(csw.sig == cpu_to_le32(0x53425355));
    g_assert(csw.tag == cpu_to_le32(0));
    if (csw.status) {
        return -1;
    }
    return data_len - le32_to_cpu(csw.residue); /* bytes copied */
}

/*
 * Submit command with CSW sent ahead of CBW.
 * Can only be no-data or data-out commands (because a data-in command
 * would interpret the CSW as a data-in).
 */
static ssize_t xhci_submit_out_of_order_scsi_cmd(XHCIQState *s,
                                    const uint8_t *cmd, uint8_t cmd_len,
                                    void *data, uint32_t data_len)
{
    struct usb_msd_cbw cbw;
    struct usb_msd_csw csw;
    uint64_t trb_data, csw_data;
    XHCITRB trb, csw_trb;
    uint64_t tag, csw_tag;
    bool got_csw = false;

    /* TRB data payload */
    trb_data = xhci_guest_zalloc(s, data_len > sizeof(cbw) ? data_len : sizeof(cbw));
    csw_data = xhci_guest_zalloc(s, sizeof(csw));

    /* Issue a transfer ring ep 2 data (in) */
    memset(&csw_trb, 0, TRB_SIZE);
    csw_trb.parameter = csw_data;
    csw_trb.status = sizeof(csw);
    csw_trb.control |= TR_NORMAL << TRB_TYPE_SHIFT;
    csw_trb.control |= TRB_TR_IOC;
    csw_tag = submit_tr_trb(s, s->slotid, 2, &csw_trb);

    memset(&cbw, 0, sizeof(cbw));
    cbw.sig = cpu_to_le32(0x43425355);
    cbw.tag = cpu_to_le32(0);
    cbw.data_len = cpu_to_le32(data_len);
    cbw.flags = 0x00;
    cbw.lun = 0;
    cbw.cmd_len = cmd_len; /* cmd len */
    memcpy(cbw.cmd, cmd, cmd_len);
    qtest_memwrite(s->parent->qts, trb_data, &cbw, sizeof(cbw));

    /* Issue a transfer ring ep 3 data (out) */
    memset(&trb, 0, TRB_SIZE);
    trb.parameter = trb_data;
    trb.status = sizeof(cbw);
    trb.control |= TR_NORMAL << TRB_TYPE_SHIFT;
    trb.control |= TRB_TR_IOC;
    tag = submit_tr_trb(s, s->slotid, 3, &trb);

    wait_event_trb(s, &trb);
    if (trb.parameter == csw_tag) {
        g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);
        got_csw = true;
        if (!try_get_event_trb(s, &trb)) {
            wait_event_trb(s, &trb);
        }
    }
    g_assert_cmphex(trb.parameter, ==, tag);
    g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);

    if (data_len) {
        qtest_memwrite(s->parent->qts, trb_data, data, data_len);

        /* Issue a transfer ring ep 3 data (out) */
        memset(&trb, 0, TRB_SIZE);
        trb.parameter = trb_data;
        trb.status = data_len; /* data_len bytes, no more packets */
        trb.control |= TR_NORMAL << TRB_TYPE_SHIFT;
        trb.control |= TRB_TR_IOC;
        tag = submit_tr_trb(s, s->slotid, 3, &trb);
        wait_event_trb(s, &trb);
        if (trb.parameter == csw_tag) {
            g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);
            got_csw = true;
            if (!try_get_event_trb(s, &trb)) {
                wait_event_trb(s, &trb);
            }
        }
        g_assert_cmphex(trb.parameter, ==, tag);
        g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);
    } else {
        /* No data */
    }

    if (!got_csw) {
        wait_event_trb(s, &csw_trb);
        g_assert_cmphex(csw_trb.parameter, ==, csw_tag);
        g_assert_cmpint(TRB_TYPE(csw_trb), ==, ER_TRANSFER);
    }

    qtest_memread(s->parent->qts, csw_data, &csw, sizeof(csw));

    guest_free(&s->parent->alloc, trb_data);
    guest_free(&s->parent->alloc, csw_data);

    g_assert(csw.sig == cpu_to_le32(0x53425355));
    g_assert(csw.tag == cpu_to_le32(0));
    if (csw.status) {
        return -1;
    }
    return data_len - le32_to_cpu(csw.residue); /* bytes copied */
}

#include "scsi/constants.h"

static void xhci_test_msd(XHCIQState *s)
{
    XHCITRB trb;
    uint64_t tag;
    uint8_t scsi_cmd[16];
    g_autofree void *mem = g_malloc0(0x1000); /* buffer for writing to guest */

    /* Issue a transfer ring ep 2 noop */
    memset(&trb, 0, TRB_SIZE);
    trb.control |= TR_NOOP << TRB_TYPE_SHIFT;
    trb.control |= TRB_TR_IOC;
    tag = submit_tr_trb(s, s->slotid, 2, &trb);
    wait_event_trb(s, &trb);
    g_assert_cmphex(trb.parameter, ==, tag);
    g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);

    /* Issue a transfer ring ep 3 noop */
    memset(&trb, 0, TRB_SIZE);
    trb.control |= TR_NOOP << TRB_TYPE_SHIFT;
    trb.control |= TRB_TR_IOC;
    tag = submit_tr_trb(s, s->slotid, 3, &trb);
    wait_event_trb(s, &trb);
    g_assert_cmphex(trb.parameter, ==, tag);
    g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);

    /* Clear SENSE data */
    memset(scsi_cmd, 0, sizeof(scsi_cmd));
    scsi_cmd[0] = INQUIRY;
    if (xhci_submit_scsi_cmd(s, scsi_cmd, 6, mem, 0, false) < 0) {
        g_assert_not_reached();
    }

    /* Try an "out of order" command */
    if (xhci_submit_out_of_order_scsi_cmd(s, scsi_cmd, 6, mem, 0) < 0) {
        g_assert_not_reached();
    }

    /* Report LUNs */
    memset(scsi_cmd, 0, sizeof(scsi_cmd));
    scsi_cmd[0] = REPORT_LUNS;
    /* length in big-endian */
    scsi_cmd[6] = 0x00;
    scsi_cmd[7] = 0x00;
    scsi_cmd[8] = 0x01;
    scsi_cmd[9] = 0x00;

    if (xhci_submit_scsi_cmd(s, scsi_cmd, 16, mem, 0x100, true) < 0) {
        g_assert_not_reached();
    }

    /* Check REPORT_LUNS data found 1 LUN */
    g_assert(((uint32_t *)mem)[0] == cpu_to_be32(8)); /* LUN List Length */
}

/*
 * This test brings up an endpoint and runs some noops through its command
 * ring and gets responses back on the event ring, then brings up a device
 * context and runs some noops through its transfer ring (if available).
 */
static void test_xhci_stress_rings(const void *arg)
{
    const TestData *td = arg;
    XHCIQState *s;
    XHCITRB trb;
    uint64_t tag;
    int i;

    if (qtest_has_device("usb-storage")) {
        s = xhci_boot("-M q35 "
                "-device %s,id=xhci,bus=pcie.0,addr=1d.0 "
                "-device usb-storage,bus=xhci.0,drive=drive0 "
                "-drive id=drive0,if=none,file=null-co://,"
                    "file.read-zeroes=on,format=raw ",
                td->device);
    } else {
        s = xhci_boot("-M q35 "
                "-device %s,id=xhci,bus=pcie.0,addr=1d.0 ",
                td->device);
    }
    g_assert_cmphex(s->fingerprint, ==, td->fingerprint);

    xhci_enable_device(s);

    /* Wrap the command and event rings with no-ops a few times */
    for (i = 0; i < 100; i++) {
        /* Issue a command ring no-op */
        memset(&trb, 0, TRB_SIZE);
        trb.control |= CR_NOOP << TRB_TYPE_SHIFT;
        trb.control |= TRB_TR_IOC;
        tag = submit_cr_trb(s, &trb);
        wait_event_trb(s, &trb);
        g_assert_cmphex(trb.parameter , ==, tag);
        g_assert_cmpint(TRB_TYPE(trb), ==, ER_COMMAND_COMPLETE);
    }

    if (qtest_has_device("usb-storage")) {
        xhci_enable_slot(s);

        /* Wrap the transfer ring a few times */
        for (i = 0; i < 100; i++) {
            /* Issue a transfer ring ep 0 noop */
            memset(&trb, 0, TRB_SIZE);
            trb.control |= TR_NOOP << TRB_TYPE_SHIFT;
            trb.control |= TRB_TR_IOC;
            tag = submit_tr_trb(s, s->slotid, 0, &trb);
            wait_event_trb(s, &trb);
            g_assert_cmphex(trb.parameter, ==, tag);
            g_assert_cmpint(TRB_TYPE(trb), ==, ER_TRANSFER);
        }
    }

    xhci_disable_device(s);
    xhci_shutdown(s);
}

/*
 * This test brings up a USB MSD endpoint and runs MSD (SCSI) commands.
 */
static void test_usb_msd(const void *arg)
{
    const TestData *td = arg;
    XHCIQState *s;

    s = xhci_boot("-M q35 "
            "-device %s,id=xhci,bus=pcie.0,addr=1d.0 "
            "-device usb-storage,bus=xhci.0,drive=drive0 "
            "-drive id=drive0,if=none,file=null-co://,"
                "file.read-zeroes=on,format=raw ",
            td->device);
    g_assert_cmphex(s->fingerprint, ==, td->fingerprint);

    xhci_enable_device(s);

    xhci_enable_slot(s);

    xhci_enable_msd_bulk_endpoints(s);

    xhci_test_msd(s);

    xhci_disable_device(s);
    xhci_shutdown(s);
}

static void add_test(const char *name, TestData *td, void (*fn)(const void *))
{
    g_autofree char *full_name = g_strdup_printf(
            "/xhci/pci/%s/%s", td->device, name);
    qtest_add_data_func(full_name, td, fn);
}

static void add_tests(TestData *td)
{
    add_test("hotplug", td, test_xhci_hotplug);
    if (qtest_has_device("usb-uas")) {
        add_test("usb-uas", td, test_usb_uas_hotplug);
    }
    if (qtest_has_device("usb-ccid")) {
        add_test("usb-ccid", td, test_usb_ccid_hotplug);
    }
    add_test("xhci-stress-rings", td, test_xhci_stress_rings);
    if (qtest_has_device("usb-storage")) {
        add_test("usb-msd", td, test_usb_msd);
    }
}

/* tests */
int main(int argc, char **argv)
{
    int ret;
    const char *arch;
    int i;
    TestData td[] = {
        { .device = "qemu-xhci", .fingerprint = XHCI_QEMU_ID, },
        { .device = "nec-usb-xhci", .fingerprint = XHCI_NEC_ID, },
        { .device = "ti-usb-xhci", .fingerprint = XHCI_TI_ID, },
    };

    g_test_init(&argc, &argv, NULL);

    /* Check architecture */
    arch = qtest_get_arch();
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86");
        return 0;
    }

    for (i = 0; i < ARRAY_SIZE(td); i++) {
        if (qtest_has_device(td[i].device)) {
            add_tests(&td[i]);
        }
    }

    ret = g_test_run();
    qtest_end();

    return ret;
}
