/*
 * QTest testcase for USB EHCI
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "libqos/pci-pc.h"
#include "hw/usb/uhci-regs.h"
#include "hw/usb/ehci-regs.h"
#include "hw/usb/hcd-ehci.h"
#include "libqos/usb.h"

static QPCIBus *pcibus;
static struct qhc uhci1;
static struct qhc uhci2;
static struct qhc uhci3;
static struct qhc ehci1;

/* helpers */

#if 0
static void uhci_port_update(struct qhc *hc, int port,
                             uint16_t set, uint16_t clear)
{
    void *addr = hc->base + 0x10 + 2 * port;
    uint16_t value;

    value = qpci_io_readw(hc->dev, addr);
    value |= set;
    value &= ~clear;
    qpci_io_writew(hc->dev, addr, value);
}
#endif

static void ehci_port_test(struct qhc *hc, int port, uint32_t expect)
{
    uint32_t value = qpci_io_readl(hc->dev, hc->bar, 0x64 + 4 * port);
    uint16_t mask = ~(PORTSC_CSC | PORTSC_PEDC | PORTSC_OCC);

#if 0
    fprintf(stderr, "%s: %d, have 0x%08x, want 0x%08x\n",
            __func__, port, value & mask, expect & mask);
#endif
    g_assert((value & mask) == (expect & mask));
}

/* tests */

static void test_init(void)
{
    pcibus = qpci_new_pc(global_qtest, NULL);
    g_assert(pcibus != NULL);

    qusb_pci_init_one(pcibus, &uhci1, QPCI_DEVFN(0x1d, 0), 4);
    qusb_pci_init_one(pcibus, &uhci2, QPCI_DEVFN(0x1d, 1), 4);
    qusb_pci_init_one(pcibus, &uhci3, QPCI_DEVFN(0x1d, 2), 4);
    qusb_pci_init_one(pcibus, &ehci1, QPCI_DEVFN(0x1d, 7), 0);
}

static void test_deinit(void)
{
    uhci_deinit(&uhci1);
    uhci_deinit(&uhci2);
    uhci_deinit(&uhci3);
    uhci_deinit(&ehci1);
    qpci_free_pc(pcibus);
}

static void pci_uhci_port_1(void)
{
    g_assert(pcibus != NULL);

    uhci_port_test(&uhci1, 0, UHCI_PORT_CCS); /* usb-tablet  */
    uhci_port_test(&uhci1, 1, UHCI_PORT_CCS); /* usb-storage */
    uhci_port_test(&uhci2, 0, 0);
    uhci_port_test(&uhci2, 1, 0);
    uhci_port_test(&uhci3, 0, 0);
    uhci_port_test(&uhci3, 1, 0);
}

static void pci_ehci_port_1(void)
{
    int i;

    g_assert(pcibus != NULL);

    for (i = 0; i < 6; i++) {
        ehci_port_test(&ehci1, i, PORTSC_POWNER | PORTSC_PPOWER);
    }
}

static void pci_ehci_config(void)
{
    /* hands over all ports from companion uhci to ehci */
    qpci_io_writel(ehci1.dev, ehci1.bar, 0x60, 1);
}

static void pci_uhci_port_2(void)
{
    g_assert(pcibus != NULL);

    uhci_port_test(&uhci1, 0, 0); /* usb-tablet,  @ehci */
    uhci_port_test(&uhci1, 1, 0); /* usb-storage, @ehci */
    uhci_port_test(&uhci2, 0, 0);
    uhci_port_test(&uhci2, 1, 0);
    uhci_port_test(&uhci3, 0, 0);
    uhci_port_test(&uhci3, 1, 0);
}

static void pci_ehci_port_2(void)
{
    static uint32_t expect[] = {
        PORTSC_PPOWER | PORTSC_CONNECT, /* usb-tablet  */
        PORTSC_PPOWER | PORTSC_CONNECT, /* usb-storage */
        PORTSC_PPOWER,
        PORTSC_PPOWER,
        PORTSC_PPOWER,
        PORTSC_PPOWER,
    };
    int i;

    g_assert(pcibus != NULL);

    for (i = 0; i < 6; i++) {
        ehci_port_test(&ehci1, i, expect[i]);
    }
}

static void pci_ehci_port_3_hotplug(void)
{
    /* check for presence of hotplugged usb-tablet */
    g_assert(pcibus != NULL);
    ehci_port_test(&ehci1, 2, PORTSC_PPOWER | PORTSC_CONNECT);
}

static void pci_ehci_port_hotplug(void)
{
    usb_test_hotplug(global_qtest, "ich9-ehci-1", "3", pci_ehci_port_3_hotplug);
}

/* operational registers, relative to the BAR */
#define EHCI_USBCMD             0x20
#define EHCI_USBSTS             0x24
#define EHCI_PERIODICLISTBASE   0x34
#define EHCI_ASYNCLISTADDR      0x38

#define NLPTR_TYPE_QH           (1 << 1)
#define NLPTR_TYPE_SITD         (2 << 1)
#define NLPTR_T                 1

#define QH_ADDR                 0x100000    /* in the default 128 MiB of RAM */
#define FRAME_LIST_ADDR         0x101000
#define BAD_ADDR                0x40000000  /* above RAM, nothing mapped */
#define FRAME_NS                1000000

static uint32_t ehci_readl(uint32_t off)
{
    return qpci_io_readl(ehci1.dev, ehci1.bar, off);
}

static void ehci_writel(uint32_t off, uint32_t val)
{
    qpci_io_writel(ehci1.dev, ehci1.bar, off, val);
}

static void ehci_hcreset(void)
{
    ehci_writel(EHCI_USBCMD, USBCMD_HCRESET);
    g_assert_cmphex(ehci_readl(EHCI_USBCMD) & USBCMD_HCRESET, ==, 0);
    g_assert_cmphex(ehci_readl(EHCI_USBSTS) & USBSTS_HALT, ==, USBSTS_HALT);
}

static void ehci_write_qh(uint32_t next, uint32_t next_qtd)
{
    uint32_t qh[16] = {
        next,
        QH_EPCHAR_H | (64 << QH_EPCHAR_MPLEN_SH) |
            (EHCI_QH_EPS_HIGH << QH_EPCHAR_EPS_SH),
        1 << QH_EPCAP_MULT_SH,
        0,                                      /* current qTD */
        next_qtd,
        NLPTR_T,                                /* alternate next qTD */
        0,                                      /* token: inactive */
    };

    memwrite(QH_ADDR, qh, sizeof(qh));
}

/* Run a schedule into a descriptor that cannot be read. */
static void ehci_run_into_dma_error(uint32_t usbcmd)
{
    uint32_t sts;
    int i;

    ehci_writel(EHCI_USBCMD, usbcmd);
    for (i = 0; i < 16; i++) {
        clock_step(FRAME_NS);
        if (ehci_readl(EHCI_USBSTS) & USBSTS_HSE) {
            break;
        }
    }
    g_assert_cmphex(ehci_readl(EHCI_USBSTS) & USBSTS_HSE, ==, USBSTS_HSE);
    g_assert_cmphex(ehci_readl(EHCI_USBCMD) & USBCMD_RUNSTOP, ==, 0);

    for (i = 0; i < 4; i++) {
        clock_step(FRAME_NS);
    }
    sts = ehci_readl(EHCI_USBSTS);
    g_assert_cmphex(sts & USBSTS_HALT, ==, USBSTS_HALT);
    g_assert_cmphex(sts & (USBSTS_ASS | USBSTS_PSS), ==, 0);

    /* a reset must bring the controller back */
    ehci_hcreset();
    ehci_write_qh(QH_ADDR | NLPTR_TYPE_QH, NLPTR_T);
    ehci_writel(EHCI_ASYNCLISTADDR, QH_ADDR);
    ehci_writel(EHCI_USBCMD, USBCMD_RUNSTOP | USBCMD_ASE);
    for (i = 0; i < 4; i++) {
        clock_step(FRAME_NS);
    }
    g_assert_cmphex(ehci_readl(EHCI_USBSTS) & (USBSTS_HSE | USBSTS_HALT),
                    ==, 0);
    g_assert_cmphex(ehci_readl(EHCI_USBCMD) & USBCMD_RUNSTOP, ==,
                    USBCMD_RUNSTOP);
    ehci_hcreset();
}

static void pci_ehci_dma_error(void)
{
    uint32_t entries[32];
    int i;

    g_assert(pcibus != NULL);
    ehci_hcreset();

    /* async schedule: the list head itself is unreadable */
    ehci_writel(EHCI_ASYNCLISTADDR, BAD_ADDR);
    ehci_run_into_dma_error(USBCMD_RUNSTOP | USBCMD_ASE);

    /* async schedule: the head QH links to an unreadable QH */
    ehci_write_qh(BAD_ADDR | NLPTR_TYPE_QH, NLPTR_T);
    ehci_writel(EHCI_ASYNCLISTADDR, QH_ADDR);
    ehci_run_into_dma_error(USBCMD_RUNSTOP | USBCMD_ASE);

    /* async schedule: a readable QH points at an unreadable qTD */
    ehci_write_qh(QH_ADDR | NLPTR_TYPE_QH, BAD_ADDR);
    ehci_writel(EHCI_ASYNCLISTADDR, QH_ADDR);
    ehci_run_into_dma_error(USBCMD_RUNSTOP | USBCMD_ASE);

    /* periodic schedule: the frame list points at an unreadable siTD */
    for (i = 0; i < ARRAY_SIZE(entries); i++) {
        entries[i] = BAD_ADDR | NLPTR_TYPE_SITD;
    }
    memwrite(FRAME_LIST_ADDR, entries, sizeof(entries));
    ehci_writel(EHCI_PERIODICLISTBASE, FRAME_LIST_ADDR);
    ehci_run_into_dma_error(USBCMD_RUNSTOP | USBCMD_PSE);
}


int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_device("ich9-usb-ehci1") ||
        !qtest_has_device("ich9-usb-uhci1")) {
        return 0;
    }

    qtest_add_func("/ehci/pci/uhci-port-1", pci_uhci_port_1);
    qtest_add_func("/ehci/pci/ehci-port-1", pci_ehci_port_1);
    qtest_add_func("/ehci/pci/ehci-config", pci_ehci_config);
    qtest_add_func("/ehci/pci/uhci-port-2", pci_uhci_port_2);
    qtest_add_func("/ehci/pci/ehci-port-2", pci_ehci_port_2);
    qtest_add_func("/ehci/pci/ehci-port-3-hotplug", pci_ehci_port_hotplug);
    qtest_add_func("/ehci/pci/ehci-dma-error", pci_ehci_dma_error);

    qtest_start("-machine q35 -device ich9-usb-ehci1,bus=pcie.0,addr=1d.7,"
                "multifunction=on,id=ich9-ehci-1 "
                "-device ich9-usb-uhci1,bus=pcie.0,addr=1d.0,"
                "multifunction=on,masterbus=ich9-ehci-1.0,firstport=0 "
                "-device ich9-usb-uhci2,bus=pcie.0,addr=1d.1,"
                "multifunction=on,masterbus=ich9-ehci-1.0,firstport=2 "
                "-device ich9-usb-uhci3,bus=pcie.0,addr=1d.2,"
                "multifunction=on,masterbus=ich9-ehci-1.0,firstport=4 "
                "-drive if=none,id=usbcdrom,media=cdrom "
                "-device usb-tablet,bus=ich9-ehci-1.0,port=1,usb_version=1 "
                "-device usb-storage,bus=ich9-ehci-1.0,port=2,drive=usbcdrom ");

    test_init();
    ret = g_test_run();
    test_deinit();

    qtest_end();

    return ret;
}
