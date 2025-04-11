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
#include "libqos/libqos-pc.h"
#include "libqtest-single.h"
#include "libqos/usb.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"
#include "hw/usb/hcd-xhci.h"

/*** Test Setup & Teardown ***/
typedef struct XHCIQSlotState {
    /* In-memory arrays */
    uint64_t device_context;
    uint64_t transfer_ring;

    uint32_t tr_trb_entries;
    uint32_t tr_trb_idx;
    uint32_t tr_trb_c;
} XHCIQSlotState;

typedef struct XHCIQState {
    /* QEMU PCI variables */
    QOSState *parent;
    QPCIDevice *dev;
    QPCIBar bar;
    uint64_t barsize;
    uint32_t fingerprint;

    /* In-memory arrays */
    uint64_t dc_base_array;
    uint64_t command_ring;
    uint64_t event_ring_seg;
    uint64_t event_ring;

    uint32_t cr_trb_entries;
    uint32_t cr_trb_idx;
    uint32_t cr_trb_c;
    uint32_t er_trb_entries;
    uint32_t er_trb_idx;
    uint32_t er_trb_c;

    /* Host controller properties */
    uint32_t rtoff, dboff;
    uint32_t maxports, maxslots, maxintrs;

    XHCIQSlotState slots[32];
} XHCIQState;

#define XHCI_NEC_ID (PCI_DEVICE_ID_NEC_UPD720200 << 16 | \
                     PCI_VENDOR_ID_NEC)

/**
 * Locate, verify, and return a handle to the XHCI device.
 */
static QPCIDevice *get_xhci_device(QTestState *qts, uint32_t *fingerprint)
{
    QPCIDevice *xhci;
    uint32_t xhci_fingerprint;
    QPCIBus *pcibus;

    pcibus = qpci_new_pc(qts, NULL);

    /* Find the XHCI PCI device and verify it's the right one. */
    xhci = qpci_device_find(pcibus, QPCI_DEVFN(0x1D, 0x0));
    g_assert(xhci != NULL);

    xhci_fingerprint = qpci_config_readl(xhci, PCI_VENDOR_ID);
    switch (xhci_fingerprint) {
    case XHCI_NEC_ID:
        break;
    default:
        /* Unknown device. */
        g_assert_not_reached();
    }

    if (fingerprint) {
        *fingerprint = xhci_fingerprint;
    }
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
    s->dev = get_xhci_device(s->parent->qts, &s->fingerprint);
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

    if (cli) {
        va_start(ap, cli);
        s = xhci_vboot(cli, ap);
        va_end(ap);
    } else {
        s = xhci_boot("-M q35 "
                      "-device nec-usb-xhci,id=xhci,bus=pcie.0,addr=1d.0 "
                      "-drive id=drive0,if=none,file=null-co://,"
                          "file.read-zeroes=on,format=raw");
    }

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

static void test_xhci_hotplug(void)
{
    XHCIQState *s;
    QTestState *qts;

    s = xhci_boot(NULL);
    qts = s->parent->qts;

    usb_test_hotplug(qts, "xhci", "1", NULL);

    xhci_shutdown(s);
}

static void test_usb_uas_hotplug(void)
{
    XHCIQState *s;
    QTestState *qts;

    s = xhci_boot(NULL);
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

static void test_usb_ccid_hotplug(void)
{
    XHCIQState *s;
    QTestState *qts;

    s = xhci_boot(NULL);
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
    char mem[0x1000];
    uint64_t ret;

    g_assert(size <= 0x1000);

    memset(mem, 0, size);

    ret = guest_alloc(&s->parent->alloc, size);
    qtest_memwrite(s->parent->qts, ret, mem, size);

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

static void wait_event_trb(XHCIQState *s, XHCITRB *trb)
{
    XHCITRB t;
    uint64_t er_addr = s->event_ring + s->er_trb_idx * TRB_SIZE;
    uint32_t value;
    guint64 end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    /* Wait for event interrupt  */

    do {
        if (g_get_monotonic_time() >= end_time) {
            g_error("Timeout expired");
        }
        qtest_clock_step(s->parent->qts, 10000);

        value = xhci_op_readl(s, XHCI_OPER_REG_USBSTS);
    } while (!(value & XHCI_USBSTS_EINT));

    value = xhci_intr_readl(s, 0, XHCI_INTR_REG_IMAN);

    /* With MSI-X enabled, IMAN IP is cleared after raising the interrupt */
    g_assert(!(value & XHCI_IMAN_IP));

    /* Ensure MSI-X interrupt is pending. */
    /* XXX: this is never cleared so it doesn't verify multiple interrupts.
     * should enable the msix vector like e1000e */
    assert(qpci_msix_pending(s->dev, 0));

    xhci_op_writel(s, XHCI_OPER_REG_USBSTS, XHCI_USBSTS_EINT); /* clear EINT */

    qtest_memread(s->parent->qts, er_addr, &t, TRB_SIZE);

    trb->parameter = le64_to_cpu(t.parameter);
    trb->status = le32_to_cpu(t.status);
    trb->control = le32_to_cpu(t.control);

    g_assert((trb->status >> 24) == CC_SUCCESS);
    g_assert((trb->control & TRB_C) == s->er_trb_c); /* C bit has been set */

    s->er_trb_idx++;
    if (s->er_trb_idx == s->er_trb_entries) {
        s->er_trb_idx = 0;
        s->er_trb_c ^= 1;
    }
    /* Update ERDP to processed TRB addr and EHB bit, which clears EHB */
    er_addr = s->event_ring + s->er_trb_idx * TRB_SIZE;
    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERDP_LO,
                     (er_addr & 0xffffffff) | XHCI_ERDP_EHB);
}

static void set_link_trb(XHCIQState *s, uint64_t ring, uint32_t c,
                         uint32_t entries)
{
    XHCITRB trb;

    g_assert(entries > 1);

    memset(&trb, 0, TRB_SIZE);
    trb.parameter = cpu_to_le64(ring);
    trb.control = cpu_to_le32(c | /* C */
                              (TR_LINK << TRB_TYPE_SHIFT) |
                              TRB_LK_TC);
    qtest_memwrite(s->parent->qts, ring + TRB_SIZE * (entries - 1),
                   &trb, TRB_SIZE);
}

static void submit_cr_trb(XHCIQState *s, XHCITRB *trb)
{
    uint64_t cr_addr = s->command_ring + s->cr_trb_idx * TRB_SIZE;
    XHCITRB t;

    trb->control |= s->cr_trb_c; /* C */

    t.parameter = cpu_to_le64(trb->parameter);
    t.status = cpu_to_le32(trb->status);
    t.control = cpu_to_le32(trb->control);

    qtest_memwrite(s->parent->qts, cr_addr, &t, TRB_SIZE);
    s->cr_trb_idx++;
    /* Last entry contains the link, so wrap back */
    if (s->cr_trb_idx == s->cr_trb_entries - 1) {
        set_link_trb(s, s->command_ring, s->cr_trb_c, s->cr_trb_entries);
        s->cr_trb_idx = 0;
        s->cr_trb_c ^= 1;
    }
    xhci_db_writel(s, 0, 0); /* doorbell 0 */
}

/*
 * This test brings up an endpoint and runs some noops through its command
 * ring and gets responses back on the event ring.
 *
 * This could be librified in future (like AHCI0 to have a way to bring up
 * an endpoint to test device protocols.
 */
static void pci_xhci_stress_rings(void)
{
    XHCIQState *s;
    uint32_t value;
    uint64_t input_context;
    XHCIEvRingSeg ev_seg;
    XHCITRB trb;
    uint32_t hcsparams1;
    uint32_t slotid;
    g_autofree void *mem = g_malloc0(0x1000); /* buffer for writing to guest */
    int i;

    s = xhci_boot("-M q35 "
            "-device nec-usb-xhci,id=xhci,bus=pcie.0,addr=1d.0 "
            "-device usb-storage,bus=xhci.0,drive=drive0 "
            "-drive id=drive0,if=none,file=null-co://,"
                "file.read-zeroes=on,format=raw "
            );

    hcsparams1 = xhci_cap_readl(s, XHCI_HCCAP_REG_HCSPARAMS1);
    s->maxports = (hcsparams1 >> 24) & 0xff;
    s->maxintrs = (hcsparams1 >> 8) & 0x3ff;
    s->maxslots = hcsparams1 & 0xff;

    s->dboff = xhci_cap_readl(s, XHCI_HCCAP_REG_DBOFF);
    s->rtoff = xhci_cap_readl(s, XHCI_HCCAP_REG_RTSOFF);

    s->dc_base_array = xhci_guest_zalloc(s, 0x800);
    s->command_ring = xhci_guest_zalloc(s, 0x1000);
    s->event_ring = xhci_guest_zalloc(s, 0x1000);
    s->event_ring_seg = xhci_guest_zalloc(s, 0x100);

    /* Arbitrary small sizes so we can make them wrap */
    s->cr_trb_entries = 0x20;
    s->cr_trb_c = 1;
    s->er_trb_entries = 0x10;
    s->er_trb_c = 1;

    ev_seg.addr_low = cpu_to_le32(s->event_ring & 0xffffffff);
    ev_seg.addr_high = cpu_to_le32(s->event_ring >> 32);
    ev_seg.size = cpu_to_le32(0x10);
    ev_seg.rsvd = 0;
    qtest_memwrite(s->parent->qts, s->event_ring_seg, &ev_seg, sizeof(ev_seg));

    xhci_op_writel(s, XHCI_OPER_REG_USBCMD, XHCI_USBCMD_HCRST);
    do {
        value = xhci_op_readl(s, XHCI_OPER_REG_USBSTS);
    } while (value & XHCI_USBSTS_CNR);

    xhci_op_writel(s, XHCI_OPER_REG_CONFIG, s->maxslots);

    xhci_op_writel(s, XHCI_OPER_REG_DCBAAP_LO, s->dc_base_array & 0xffffffff);
    xhci_op_writel(s, XHCI_OPER_REG_DCBAAP_HI, s->dc_base_array >> 32);

    xhci_op_writel(s, XHCI_OPER_REG_CRCR_LO,
                   (s->command_ring & 0xffffffff) | s->cr_trb_c);
    xhci_op_writel(s, XHCI_OPER_REG_CRCR_HI, s->command_ring >> 32);

    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERSTSZ, 1);

    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERSTBA_LO,
                     s->event_ring_seg & 0xffffffff);
    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERSTBA_HI,
                     s->event_ring_seg >> 32);

    /* ERDP */
    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERDP_LO, s->event_ring & 0xffffffff);
    xhci_intr_writel(s, 0, XHCI_INTR_REG_ERDP_HI, s->event_ring >> 32);

    qpci_msix_enable(s->dev);
    xhci_op_writel(s, XHCI_OPER_REG_USBCMD, XHCI_USBCMD_RS | XHCI_USBCMD_INTE);

    /* Enable interrupts on ER IMAN */
    xhci_intr_writel(s, 0, XHCI_INTR_REG_IMAN, XHCI_IMAN_IE);

    assert(!qpci_msix_pending(s->dev, 0));

    /* Wrap the command and event rings with no-ops a few times */
    for (i = 0; i < 100; i++) {
        /* Issue a command ring no-op */
        memset(&trb, 0, TRB_SIZE);
        trb.control |= CR_NOOP << TRB_TYPE_SHIFT;
        trb.control |= TRB_TR_IOC;
        submit_cr_trb(s, &trb);
        wait_event_trb(s, &trb);
    }

    /* Query ports */
    for (i = 0; i < s->maxports; i++) {
        value = xhci_port_readl(s, i, 0); /* PORTSC */

        /* Only first port should be attached and enabled */
        if (i == 0) {
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

    /* Issue a command ring enable slot */
    memset(&trb, 0, TRB_SIZE);
    trb.control |= CR_ENABLE_SLOT << TRB_TYPE_SHIFT;
    trb.control |= TRB_TR_IOC;
    submit_cr_trb(s, &trb);
    wait_event_trb(s, &trb);
    slotid = (trb.control >> TRB_CR_SLOTID_SHIFT) & 0xff;

    s->slots[slotid].transfer_ring = xhci_guest_zalloc(s, 0x1000);
    s->slots[slotid].tr_trb_entries = 0x10;
    s->slots[slotid].tr_trb_c = 1;

    /* 32-byte input context size, should check HCCPARAMS1 for 64-byte size */
    input_context = xhci_guest_zalloc(s, 0x420);

    /* Set input control context */
    ((uint32_t *)mem)[1] = cpu_to_le32(0x3); /* Add device contexts 0 and 1 */
    ((uint32_t *)mem)[8] = cpu_to_le32(1 << 27); /* 1 context entry */
    ((uint32_t *)mem)[9] = cpu_to_le32(1 << 16); /* 1 port number */

    /* Set endpoint 0 context */
    ((uint32_t *)mem)[16] = 0;
    ((uint32_t *)mem)[17] = cpu_to_le32((ET_CONTROL << EP_TYPE_SHIFT) |
                                        (0x200 << 16)); /* max packet sz XXX? */
    ((uint32_t *)mem)[18] = cpu_to_le32((s->slots[slotid].transfer_ring &
                                         0xffffffff) | 1); /* DCS=1 */
    ((uint32_t *)mem)[19] = cpu_to_le32(s->slots[slotid].transfer_ring >> 32);
    ((uint32_t *)mem)[20] = cpu_to_le32(0x200); /* Average TRB length */
    qtest_memwrite(s->parent->qts, input_context, mem, 0x420);

    s->slots[slotid].device_context = xhci_guest_zalloc(s, 0x400);

    ((uint64_t *)mem)[0] = cpu_to_le64(s->slots[slotid].device_context);
    qtest_memwrite(s->parent->qts, s->dc_base_array + 8 * slotid, mem, 8);

    /* Issue a command ring address device */
    memset(&trb, 0, TRB_SIZE);
    trb.parameter = input_context;
    trb.control |= CR_ADDRESS_DEVICE << TRB_TYPE_SHIFT;
    trb.control |= slotid << TRB_CR_SLOTID_SHIFT;
    submit_cr_trb(s, &trb);
    wait_event_trb(s, &trb);

    /* XXX: Could check EP state is running */

    /* Shut it down */
    qpci_msix_disable(s->dev);

    guest_free(&s->parent->alloc, s->slots[slotid].device_context);
    guest_free(&s->parent->alloc, s->slots[slotid].transfer_ring);
    guest_free(&s->parent->alloc, input_context);
    guest_free(&s->parent->alloc, s->event_ring);
    guest_free(&s->parent->alloc, s->event_ring_seg);
    guest_free(&s->parent->alloc, s->command_ring);
    guest_free(&s->parent->alloc, s->dc_base_array);

    xhci_shutdown(s);
}

/* tests */
int main(int argc, char **argv)
{
    int ret;
    const char *arch;

    g_test_init(&argc, &argv, NULL);

    /* Check architecture */
    arch = qtest_get_arch();
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86");
        return 0;
    }

    if (!qtest_has_device("nec-usb-xhci")) {
        return 0;
    }

    qtest_add_func("/xhci/pci/hotplug", test_xhci_hotplug);
    if (qtest_has_device("usb-uas")) {
        qtest_add_func("/xhci/pci/hotplug/usb-uas", test_usb_uas_hotplug);
    }
    if (qtest_has_device("usb-ccid")) {
        qtest_add_func("/xhci/pci/hotplug/usb-ccid", test_usb_ccid_hotplug);
    }
    if (qtest_has_device("usb-storage")) {
        qtest_add_func("/xhci/pci/xhci-stress-rings", pci_xhci_stress_rings);
    }

    ret = g_test_run();

    qtest_end();

    return ret;
}
