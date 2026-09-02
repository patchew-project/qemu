/*
 * QEMU Intel 82576 SR/IOV VF Migration Support
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "igb_common.h"
#include "igb_migration.h"
#include "system/address-spaces.h"
#include "trace.h"

/*
 * Per-VF state serialization / deserialization
 */

static int igb_core_vf_save_state(IgbVfState *s, void *buf, size_t buf_size)
{
    int size = 0;

    trace_igbvf_mig_save_state(s->vfn, size);
    return size;
}

static int igb_core_vf_max_data_size(IgbVfState *s)
{
    return sizeof(s->mig.mig_data);
}

static int igb_core_vf_load_state(IgbVfState *s, const void *buf, size_t size)
{
    trace_igbvf_mig_load_state(s->vfn, (uint32_t)size);
    return 0;
}

static int igbvf_mig_load(IgbVfState *s, const void *buf, size_t size)
{
    int ret;

    ret = igb_core_vf_load_state(s, buf, size);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

/*
 * Migration command handlers
 */

static void igbvf_mig_update_data_size(IgbVfState *s, uint32_t size)
{
    IgbVfMigState *ms = &s->mig;

    ms->mig_data_size = size;
    pci_set_long(PCI_DEVICE(s)->config +
                 IGB_MIG_DVSEC_OFFSET + IGB_MIG_DATA_SIZE, size);
}

static uint8_t igbvf_mig_cmd_save(IgbVfState *s)
{
    IgbVfMigState *ms = &s->mig;
    MemTxResult r;
    int ret;

    if (ms->mig_state != IGB_MIG_STATE_STOP_COPY) {
        return IGB_MIG_ERR_BAD_STATE;
    }

    if (!ms->mig_data_buf_addr) {
        return IGB_MIG_ERR_NO_BUFFER;
    }

    ret = igb_core_vf_save_state(s, ms->mig_data, sizeof(ms->mig_data));
    if (ret < 0) {
        return -ret;
    }
    igbvf_mig_update_data_size(s, ret);

    r = address_space_write(&address_space_memory, ms->mig_data_buf_addr,
                            MEMTXATTRS_UNSPECIFIED,
                            ms->mig_data, ms->mig_data_size);
    if (r != MEMTX_OK) {
        return IGB_MIG_ERR_DMA_FAILED;
    }

    return 0;
}

static uint8_t igbvf_mig_cmd_load(IgbVfState *s)
{
    IgbVfMigState *ms = &s->mig;
    MemTxResult r;
    int ret;

    if (ms->mig_state != IGB_MIG_STATE_RESUMING) {
        return IGB_MIG_ERR_BAD_STATE;
    }

    if (!ms->mig_data_buf_addr) {
        return IGB_MIG_ERR_NO_BUFFER;
    }

    if (ms->mig_data_size == 0 ||
        ms->mig_data_size > sizeof(ms->mig_data)) {
        return IGB_MIG_ERR_BAD_SIZE;
    }

    r = address_space_read(&address_space_memory, ms->mig_data_buf_addr,
                           MEMTXATTRS_UNSPECIFIED,
                           ms->mig_data, ms->mig_data_size);
    if (r != MEMTX_OK) {
        return IGB_MIG_ERR_DMA_FAILED;
    }

    ret = igbvf_mig_load(s, ms->mig_data, ms->mig_data_size);
    if (ret < 0) {
        return -ret;
    }

    return 0;
}

static uint8_t igbvf_mig_set_state(IgbVfState *s, uint32_t new_state)
{
    IgbVfMigState *ms = &s->mig;
    uint32_t old = ms->mig_state;
    int ret;

    switch (new_state) {
    case IGB_MIG_STATE_STOP:
        if (old != IGB_MIG_STATE_RUNNING &&
            old != IGB_MIG_STATE_STOP_COPY &&
            old != IGB_MIG_STATE_RESUMING &&
            old != IGB_MIG_STATE_ERROR) {
            return IGB_MIG_ERR_BAD_STATE;
        }
        /* Restore DATA_SIZE to max, same as at reset */
        igbvf_mig_update_data_size(s, igb_core_vf_max_data_size(s));
        break;

    case IGB_MIG_STATE_RUNNING:
        if (old != IGB_MIG_STATE_STOP) {
            return IGB_MIG_ERR_BAD_STATE;
        }
        break;

    case IGB_MIG_STATE_STOP_COPY:
        if (old != IGB_MIG_STATE_STOP) {
            return IGB_MIG_ERR_BAD_STATE;
        }
        ret = igb_core_vf_save_state(s, ms->mig_data, sizeof(ms->mig_data));
        if (ret < 0) {
            return -ret;
        }
        igbvf_mig_update_data_size(s, ret);
        break;

    case IGB_MIG_STATE_RESUMING:
        if (old != IGB_MIG_STATE_STOP) {
            return IGB_MIG_ERR_BAD_STATE;
        }
        memset(ms->mig_data, 0, sizeof(ms->mig_data));
        igbvf_mig_update_data_size(s, 0);
        break;

    default:
        return IGB_MIG_ERR_BAD_STATE;
    }

    ms->mig_state = new_state;
    trace_igbvf_mig_set_state(s->vfn, old, new_state);
    return 0;
}

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

static void igbvf_mig_cmd_ctrl(IgbVfState *s, uint32_t val)
{
    uint32_t cmd = val & IGB_MIG_CTRL_CMD_MASK;
    uint32_t arg = val >> IGB_MIG_CTRL_ARG_SHIFT;
    uint8_t err = 0;

    switch (cmd) {
    case IGB_MIG_CMD_SET_STATE:
        err = igbvf_mig_set_state(s, arg);
        break;

    case IGB_MIG_CMD_SAVE:
        err = igbvf_mig_cmd_save(s);
        break;

    case IGB_MIG_CMD_LOAD:
        igbvf_mig_update_data_size(s, arg);
        err = igbvf_mig_cmd_load(s);
        break;

    default:
        err = IGB_MIG_ERR_UNK_CMD;
        break;
    }

    if (err) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "igbvf: VF%u CTRL cmd %u failed (error %u)\n",
                      s->vfn, cmd, err);
    }
    igbvf_mig_update_status(s, err);
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

    /* DATA_SIZE is set by igbvf_mig_state_reset() */

    return true;
}

uint32_t igbvf_mig_config_read(IgbVfState *s, uint32_t addr, int size)
{
    PCIDevice *dev = PCI_DEVICE(s);

    return pci_default_read_config(dev, addr, size);
}

static uint64_t igbvf_mig_get_buf_addr(IgbVfState *s)
{
    PCIDevice *dev = PCI_DEVICE(s);
    uint32_t lo, hi;

    lo = pci_get_long(dev->config + IGB_MIG_DVSEC_OFFSET + IGB_MIG_BUF_ADDR_LO);
    hi = pci_get_long(dev->config + IGB_MIG_DVSEC_OFFSET + IGB_MIG_BUF_ADDR_HI);
    return ((uint64_t)hi << 32) | lo;
}

bool igbvf_mig_config_write(IgbVfState *s, uint32_t addr, uint32_t val,
                            int size)
{
    PCIDevice *dev = PCI_DEVICE(s);
    uint32_t offset = addr - IGB_MIG_DVSEC_OFFSET;

    switch (offset) {
    case IGB_MIG_CTRL:
        s->mig.mig_data_buf_addr = igbvf_mig_get_buf_addr(s);
        igbvf_mig_cmd_ctrl(s, val);
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

    trace_igbvf_mig_reset(s->vfn);
    ms->mig_state = IGB_MIG_STATE_RUNNING;
    ms->mig_data_buf_addr = 0;
    igbvf_mig_update_data_size(s, igb_core_vf_max_data_size(s));
    memset(ms->mig_data, 0, sizeof(ms->mig_data));

    pci_set_long(PCI_DEVICE(s)->config +
                 IGB_MIG_DVSEC_OFFSET + IGB_MIG_BUF_ADDR_LO, 0);
    pci_set_long(PCI_DEVICE(s)->config +
                 IGB_MIG_DVSEC_OFFSET + IGB_MIG_BUF_ADDR_HI, 0);

    igbvf_mig_update_status(s, 0);
}
