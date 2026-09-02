/*
 * QEMU Intel 82576 SR/IOV VF Migration Support
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "igb_common.h"
#include "igb_migration.h"

static void igbvf_mig_update_status(IgbVfState *s, uint8_t err)
{
    IgbVfMigState *ms = &s->mig;
    PCIDevice *dev = PCI_DEVICE(s);
    uint32_t status;

    status = ms->mig_state & IGB_MIG_STATUS_STATE_MASK;

    if (err) {
        status = IGB_MIG_STATE_ERROR | IGB_MIG_STATUS_ERR(err);
    }

    pci_set_long(dev->config + IGB_MIG_DVSEC_OFFSET + IGB_MIG_STATUS, status);
}


bool igbvf_add_migration_dvsec(PCIDevice *dev, Error **errp)
{
    uint16_t offset = IGB_MIG_DVSEC_OFFSET;
    uint32_t caps;

    pcie_add_capability(dev, PCI_EXT_CAP_ID_DVSEC, 1, offset,
                        IGB_MIG_DVSEC_SIZE);

    /* DVSEC header 1: length[31:20] | rev[19:16] | vendor_id[15:0] */
    pci_set_long(dev->config + offset + 0x4,
                 (IGB_MIG_DVSEC_SIZE << 20) |
                 (IGB_MIG_DVSEC_VER << 16) |
                 PCI_VENDOR_ID_INTEL);

    /* DVSEC header 2: DVSEC ID */
    pci_set_word(dev->config + offset + 0x8, IGB_MIG_DVSEC_ID);

    /* CAPS: features (state migration only) */
    caps = IGB_MIG_CAP_F_STATE;
    pci_set_long(dev->config + offset + IGB_MIG_CAPS, caps);

    /* STATUS: initial state is RUNNING */
    pci_set_long(dev->config + offset + IGB_MIG_STATUS,
                 IGB_MIG_STATE_RUNNING);

    /* BUF_ADDR_LO and BUF_ADDR_HI are writable */
    memset(dev->wmask + offset + IGB_MIG_BUF_ADDR_LO, 0xff, 4);
    memset(dev->wmask + offset + IGB_MIG_BUF_ADDR_HI, 0xff, 4);

    return true;
}

uint32_t igbvf_mig_config_read(IgbVfState *s, uint32_t addr, int size)
{
    PCIDevice *dev = PCI_DEVICE(s);

    return pci_default_read_config(dev, addr, size);
}

bool igbvf_mig_config_write(IgbVfState *s, uint32_t addr, uint32_t val,
                            int size)
{
    PCIDevice *dev = PCI_DEVICE(s);
    uint32_t offset = addr - IGB_MIG_DVSEC_OFFSET;

    switch (offset) {
    case IGB_MIG_CTRL:
        /* Command handling will be added in a later commit */
        break;

    case IGB_MIG_BUF_ADDR_LO:
    case IGB_MIG_BUF_ADDR_HI:
        pci_default_write_config(dev, addr, val, size);
        break;

    default:
        break;
    }

    return true;
}

void igbvf_mig_state_reset(IgbVfState *s)
{
    IgbVfMigState *ms = &s->mig;

    ms->mig_state = IGB_MIG_STATE_RUNNING;
    ms->mig_data_buf_addr = 0;

    pci_set_long(PCI_DEVICE(s)->config +
                 IGB_MIG_DVSEC_OFFSET + IGB_MIG_BUF_ADDR_LO, 0);
    pci_set_long(PCI_DEVICE(s)->config +
                 IGB_MIG_DVSEC_OFFSET + IGB_MIG_BUF_ADDR_HI, 0);

    igbvf_mig_update_status(s, 0);
}
