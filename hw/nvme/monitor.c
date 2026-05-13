/*
 * QEMU NVMe Controller monitor commands
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/type-helpers.h"
#include "hw/pci/pci.h"

#include "nvme.h"

static int collect_one(Object *obj, void *opaque)
{
    GString *buf = opaque;
    NvmeCtrl *n;
    PCIDevice *pci;
    pcibus_t bar0;
    unsigned io_sq = 0, io_cq = 0;

    if (!object_dynamic_cast(obj, TYPE_NVME)) {
        return 0;
    }
    n = NVME(obj);
    pci = PCI_DEVICE(n);
    bar0 = pci_get_bar_addr(pci, 0);

    for (unsigned i = 1; i <= n->params.max_ioqpairs; i++) {
        if (n->sq && n->sq[i]) {
            io_sq++;
        }
        if (n->cq && n->cq[i]) {
            io_cq++;
        }
    }

    g_string_append_printf(buf, "%s\n", object_get_canonical_path(obj));
    g_string_append_printf(buf,
        "  PCI:    BDF %02x:%02x.%x  VID=%04x DID=%04x  ",
        pci_dev_bus_num(pci), PCI_SLOT(pci->devfn), PCI_FUNC(pci->devfn),
        pci_get_word(pci->config + PCI_VENDOR_ID),
        pci_get_word(pci->config + PCI_DEVICE_ID));
    if (bar0 == PCI_BAR_UNMAPPED) {
        g_string_append(buf, "BAR0=unmapped\n");
    } else {
        g_string_append_printf(buf, "BAR0=0x%016" PRIx64 "\n",
                               (uint64_t)bar0);
    }
    g_string_append_printf(buf,
        "  ID:     SN=%.20s  MN=%.40s  FR=%.8s  CNTLID=0x%04x\n",
        n->id_ctrl.sn, n->id_ctrl.mn, n->id_ctrl.fr, n->cntlid);
    g_string_append_printf(buf, "  CC:     0x%08x\n",
                           ldl_le_p(&n->bar.cc));
    g_string_append_printf(buf, "  CSTS:   0x%08x\n",
                           ldl_le_p(&n->bar.csts));
    g_string_append_printf(buf, "  AQA:    0x%08x\n",
                           ldl_le_p(&n->bar.aqa));
    g_string_append_printf(buf,
        "  Queues: 1 admin + %u IO SQ / %u IO CQ\n", io_sq, io_cq);
    return 0;
}

HumanReadableText *qmp_x_query_nvme(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    object_child_foreach_recursive(object_get_root(), collect_one, buf);

    if (buf->len == 0) {
        g_string_append(buf, "no NVMe controllers\n");
    }

    return human_readable_text_from_str(buf);
}
