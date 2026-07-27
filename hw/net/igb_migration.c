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
#include "net/eth.h"
#include "net/net.h"
#include "igb_common.h"
#include "igb_core.h"
#include "igb_migration.h"
#include "trace.h"



/*
 * 32-bit prefetchable BAR. A 64-bit BAR2 would consume BAR2+BAR3, but
 * BAR3 is already used for MSI-X (IGBVF_MSIX_BAR_IDX = 3).
 *
 *   BAR   Index       Type            Size   Purpose
 *   BAR0  0+1    64-bit prefetchable  16 KB  MMIO registers
 *   BAR2  2      32-bit prefetchable  64 KB  Migration
 *   BAR3  3+4    64-bit prefetchable  16 KB  MSI-X table + PBA
 *   BAR5  5      -                    -      Unused
 */
void igb_pf_init_migration_bar(PCIDevice *dev)
{
    pcie_sriov_pf_init_vf_bar(dev, IGB_MIG_BAR_IDX,
                              PCI_BASE_ADDRESS_MEM_PREFETCH,
                              IGB_MIG_BAR_SIZE);
}

/*
 * Add vendor-specific PCI capability that the variant driver probes for.
 *
 * Layout (16 bytes):
 *   [0]  cap_id      (PCI_CAP_ID_VNDR = 0x09)
 *   [1]  next_cap
 *   [2]  cap_len     (16)
 *   [3]  version     (IGB_MIG_CAP_VERSION)
 *   [4-7]  magic     (IGB_MIG_CAP_MAGIC, little-endian)
 *   [8-11] bar_id    (IGB_MIG_BAR_IDX, little-endian)
 *   [12-15] flags    (feature flags, little-endian)
 */
bool igbvf_add_migration_cap(PCIDevice *dev, Error **errp)
{
    int offset;

    offset = pci_add_capability(dev, PCI_CAP_ID_VNDR, 0,
                                IGB_MIG_CAP_SIZE, errp);
    if (offset < 0) {
        return false;
    }

    /* Length and version in the standard cap flags word */
    pci_set_byte(dev->config + offset + PCI_CAP_FLAGS,
                 IGB_MIG_CAP_SIZE);
    pci_set_byte(dev->config + offset + PCI_CAP_FLAGS + 1,
                 IGB_MIG_CAP_VERSION);

    pci_set_long(dev->config + offset + IGB_MIG_CAP_OFF_MAGIC,
                 IGB_MIG_CAP_MAGIC);
    pci_set_long(dev->config + offset + IGB_MIG_CAP_OFF_BARID,
                 IGB_MIG_BAR_IDX);
    pci_set_long(dev->config + offset + IGB_MIG_CAP_OFF_FLAGS,
                 IGB_MIG_CAP_F_STATE);

    trace_igbvf_mig_cap_add(pcie_sriov_vf_number(dev), offset);
    return true;
}

/*
 * =====================================================================
 * Per-VF state serialization / deserialization
 * =====================================================================
 */

static int igb_core_vf_save_state(IgbVfState *s,
                                  void *buf, size_t buf_size)
{
    int size = 0;

    trace_igbvf_mig_save_state(s->vfn, size);
    return size;
}

static int igb_core_vf_max_data_size(IgbVfState *s)
{
    int size = igb_core_vf_save_state(s, NULL, 0);

    g_assert(size > 0 && size <= IGB_VF_STATE_MAX_SIZE);
    return size;
}

static int igb_core_vf_load_state(IgbVfState *s,
                                  const void *buf, size_t size)
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

/* ================================================================
 * Migration BAR register read/write handlers
 * ================================================================ */

static bool igbvf_mig_set_state(IgbVfState *s, uint32_t new_state)
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
            return false;
        }
        /* Restore DATA_SIZE to max, same as at reset */
        ms->mig_data_size = igb_core_vf_max_data_size(s);
        break;

    case IGB_MIG_STATE_RUNNING:
        if (old != IGB_MIG_STATE_STOP) {
            return false;
        }
        break;

    case IGB_MIG_STATE_STOP_COPY:
        if (old != IGB_MIG_STATE_STOP) {
            return false;
        }
        ret = igb_core_vf_save_state(s, ms->mig_data, sizeof(ms->mig_data));
        if (ret < 0) {
            ms->mig_error = -ret;
            ms->mig_state = IGB_MIG_STATE_ERROR;
            return false;
        }
        ms->mig_data_size = ret;
        break;

    case IGB_MIG_STATE_RESUMING:
        if (old != IGB_MIG_STATE_STOP) {
            return false;
        }
        memset(ms->mig_data, 0, sizeof(ms->mig_data));
        ms->mig_data_size = 0;
        break;

    default:
        trace_igbvf_mig_set_state_err(s->vfn, old, new_state);
        return false;
    }

    ms->mig_state = new_state;
    trace_igbvf_mig_set_state(s->vfn, old, new_state);
    return true;
}

static uint32_t igbvf_mig_get_status(IgbVfState *s)
{
    IgbVfMigState *ms = &s->mig;
    uint32_t status = 0;

    if (ms->mig_state == IGB_MIG_STATE_ERROR) {
        status |= IGB_MIG_STATUS_ERR(ms->mig_error);
    }
    if (ms->mig_state == IGB_MIG_STATE_STOP_COPY && ms->mig_data_size > 0) {
        status |= IGB_MIG_STATUS_DATA_AVAIL;
    }

    return status;
}

static void igbvf_mig_data_xfer(IgbVfState *s, uint32_t val)
{
    IgbVfMigState *ms = &s->mig;
    MemTxResult r;
    int ret;

    if (!ms->mig_data_buf_addr) {
        ms->mig_error = IGB_MIG_ERR_NO_BUFFER;
        ms->mig_state = IGB_MIG_STATE_ERROR;
        return;
    }

    switch (ms->mig_state) {
    case IGB_MIG_STATE_STOP_COPY:
        /* Save: DMA-write serialized state to driver buffer */
        r = pci_dma_write(pcie_sriov_get_pf(PCI_DEVICE(s)),
                          ms->mig_data_buf_addr,
                          ms->mig_data, ms->mig_data_size);
        if (r != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "igbvf: VF%u state write failed at 0x%" PRIx64 "\n",
                          s->vfn, ms->mig_data_buf_addr);
            ms->mig_error = IGB_MIG_ERR_DMA_FAILED;
            ms->mig_state = IGB_MIG_STATE_ERROR;
        }
        break;

    case IGB_MIG_STATE_RESUMING:
        /* Restore: DMA-read state from driver buffer and deserialize */
        if (ms->mig_data_size == 0 ||
            ms->mig_data_size > sizeof(ms->mig_data)) {
            ms->mig_error = IGB_MIG_ERR_BAD_SIZE;
            ms->mig_state = IGB_MIG_STATE_ERROR;
            break;
        }

        r = pci_dma_read(pcie_sriov_get_pf(PCI_DEVICE(s)),
                         ms->mig_data_buf_addr,
                         ms->mig_data, ms->mig_data_size);
        if (r != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "igbvf: VF%u state read failed at 0x%" PRIx64 "\n",
                          s->vfn, ms->mig_data_buf_addr);
            ms->mig_error = IGB_MIG_ERR_DMA_FAILED;
            ms->mig_state = IGB_MIG_STATE_ERROR;
            break;
        }

        ret = igbvf_mig_load(s, ms->mig_data, ms->mig_data_size);
        if (ret < 0) {
            ms->mig_error = -ret;
            ms->mig_state = IGB_MIG_STATE_ERROR;
        }
        break;

    default:
        break;
    }
}

static uint64_t igbvf_mig_read(void *opaque, hwaddr addr, unsigned size)
{
    IgbVfState *s = opaque;
    IgbVfMigState *ms = &s->mig;
    uint64_t val = 0;

    switch (addr) {
    case IGB_MIG_DEVICE_STATE:
        val = ms->mig_state;
        break;
    case IGB_MIG_STATUS:
        val = igbvf_mig_get_status(s);
        break;
    case IGB_MIG_CAPS:
        val = IGB_MIG_CAP_F_STATE;
        break;
    case IGB_MIG_VERSION:
        val = IGB_MIG_CAP_VERSION;
        break;
    case IGB_MIG_DATA_SIZE:
        val = ms->mig_data_size;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "igbvf: VF%u bad migration BAR read at 0x%"
                      HWADDR_PRIx "\n", s->vfn, addr);
        break;
    }

    trace_igbvf_mig_bar_read(s->vfn, addr, val);

    return val;
}

static void igbvf_mig_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    IgbVfState *s = opaque;
    IgbVfMigState *ms = &s->mig;

    trace_igbvf_mig_bar_write(s->vfn, addr, val);

    switch (addr) {
    case IGB_MIG_DEVICE_STATE:
        igbvf_mig_set_state(s, (uint32_t)val);
        break;
    case IGB_MIG_DATA_SIZE:
        if (val <= sizeof(ms->mig_data)) {
            ms->mig_data_size = (uint32_t)val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "igbvf: VF%u DATA_SIZE %" PRIu64 " exceeds max %zu\n",
                          s->vfn, val, sizeof(ms->mig_data));
        }
        break;
    case IGB_MIG_DATA_XFER:
        igbvf_mig_data_xfer(s, (uint32_t)val);
        break;
    case IGB_MIG_DATA_BUF_ADDR_LO:
        ms->mig_data_buf_addr =
            deposit64(ms->mig_data_buf_addr, 0, 32, val);
        break;
    case IGB_MIG_DATA_BUF_ADDR_HI:
        ms->mig_data_buf_addr =
            deposit64(ms->mig_data_buf_addr, 32, 32, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "igbvf: VF%u bad migration BAR write at 0x%"
                      HWADDR_PRIx "\n", s->vfn, addr);
        break;
    }
}

static const MemoryRegionOps mig_bar_ops = {
    .read = igbvf_mig_read,
    .write = igbvf_mig_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * Use the QEM-internal PCI_BASE_ADDRESS_MEM_ALWAYS_ON BAR type flag
 * to keep the memory BAR always mapped.
 */
void igbvf_mig_bar_init(IgbVfState *s)
{
    IgbVfMigState *ms = &s->mig;

    memory_region_init_io(&ms->mig_bar, OBJECT(s), &mig_bar_ops, s,
                          "igbvf-mig", IGB_MIG_BAR_SIZE);
    pci_register_bar(PCI_DEVICE(s), IGB_MIG_BAR_IDX,
                     PCI_BASE_ADDRESS_MEM_PREFETCH |
                     PCI_BASE_ADDRESS_MEM_ALWAYS_ON,
                     &ms->mig_bar);
    trace_igbvf_mig_bar_init(s->vfn);
}

void igbvf_mig_state_reset(IgbVfState *s)
{
    IgbVfMigState *ms = &s->mig;

    ms->mig_state = IGB_MIG_STATE_RUNNING;
    ms->mig_error = 0;
    ms->mig_data_size = igb_core_vf_max_data_size(s);
    ms->mig_data_buf_addr = 0;
    memset(ms->mig_data, 0, sizeof(ms->mig_data));
    trace_igbvf_mig_reset(s->vfn);
}
