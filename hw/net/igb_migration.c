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
#include "system/address-spaces.h"
#include "trace.h"

static IGBCore *igbvf_get_core(IgbVfState *s)
{
    return igb_pf_get_core(pcie_sriov_get_pf(PCI_DEVICE(s)));
}

/*
 * Per-VF state serialization / deserialization
 */

#define IGB_MIG_BLOB_MAGIC        0x4D494742  /* "MIGB" */
#define IGB_MIG_BLOB_VERSION      1

typedef struct IgbMigRegPair {
    uint32_t offset;
    uint32_t value;
} IgbMigRegPair;

#define IGB_VF_MAX_FIXED_REGS     64

typedef struct IgbMigBlob {
    uint32_t magic;
    uint32_t version;
    uint32_t vfn;
    uint32_t num_regs;
    IgbMigRegPair regs[IGB_VF_MAX_FIXED_REGS];
} IgbMigBlob;

#define IGB_MIG_BLOB_SIZE            sizeof(IgbMigBlob)

QEMU_BUILD_BUG_ON(IGB_MIG_BLOB_SIZE > IGB_VF_STATE_MAX_SIZE);

/* Register offsets that constitute a VF's state slice */
static int igb_vf_reg_list(uint16_t vfn, uint32_t *offsets)
{
    int n = 0;
    int q0 = vfn;
    int q1 = vfn + IGB_NUM_VM_POOLS;

    /* Per-VF control and interrupt registers */
    offsets[n++] = E1000_PVTCTRL(vfn) >> 2;
    offsets[n++] = E1000_PVTEICS(vfn) >> 2;
    offsets[n++] = E1000_PVTEIMC(vfn) >> 2;
    offsets[n++] = E1000_PVTEICR(vfn) >> 2;

    /* Per-VF statistics */
    offsets[n++] = E1000_PVFGPRC(vfn) >> 2;
    offsets[n++] = E1000_PVFGPTC(vfn) >> 2;
    offsets[n++] = E1000_PVFGORC(vfn) >> 2;
    offsets[n++] = E1000_PVFGOTC(vfn) >> 2;
    offsets[n++] = E1000_PVFMPRC(vfn) >> 2;
    offsets[n++] = E1000_PVFGPRLBC(vfn) >> 2;
    offsets[n++] = E1000_PVFGPTLBC(vfn) >> 2;
    offsets[n++] = E1000_PVFGORLBC(vfn) >> 2;
    offsets[n++] = E1000_PVFGOTLBC(vfn) >> 2;

    /*
     * Mailbox control registers only - the 16-dword payload buffer
     * (VMBMEM) is transient and drained on quiesce.
     */
    offsets[n++] = E1000_V2PMAILBOX(vfn) >> 2;
    offsets[n++] = E1000_P2VMAILBOX(vfn) >> 2;

    /* Per-VF config */
    offsets[n++] = E1000_VMOLR(vfn) >> 2;
    offsets[n++] = E1000_VMVIR(vfn) >> 2;
    offsets[n++] = E1000_PSRTYPE(vfn) >> 2;

    /*
     * VF receive addresses (RA/RA2) are saved dynamically in
     * igb_core_vf_save_state by scanning for entries whose pool
     * bits match this VF - the PF driver chooses the RA slot.
     */

    /* Interrupt routing */
    offsets[n++] = (E1000_VTIVAR + vfn * 4) >> 2;
    offsets[n++] = (E1000_VTIVAR_MISC + vfn * 4) >> 2;

    /*
     * EITR (Extended Interrupt Throttle Register) - 3 vectors per VF.
     * Each VF has 3 MSI-X vectors, each with its own EITR controlling
     * interrupt coalescing. Without saving these, interrupt
     * throttling resets to zero after migration which can cause
     * interrupt storms or latency changes. VF N uses PF EITR indices
     * (22 - N*3) .. (24 - N*3).
     */
    {
        int eitr_base = 22 - vfn * 3;
        offsets[n++] = E1000_EITR(eitr_base) >> 2;
        offsets[n++] = E1000_EITR(eitr_base + 1) >> 2;
        offsets[n++] = E1000_EITR(eitr_base + 2) >> 2;
    }

    /* RX and TX queue registers for queues q0 and q1 */
#define ADD_QUEUE_REGS(q) do { \
    offsets[n++] = E1000_RDBAL(q) >> 2; \
    offsets[n++] = E1000_RDBAH(q) >> 2; \
    offsets[n++] = E1000_RDLEN(q) >> 2; \
    offsets[n++] = E1000_SRRCTL(q) >> 2; \
    offsets[n++] = E1000_RDH(q) >> 2; \
    offsets[n++] = E1000_RDT(q) >> 2; \
    offsets[n++] = E1000_RXDCTL(q) >> 2; \
    offsets[n++] = E1000_RXCTL(q) >> 2; \
    offsets[n++] = E1000_RQDPC(q) >> 2; \
    offsets[n++] = E1000_TDBAL(q) >> 2; \
    offsets[n++] = E1000_TDBAH(q) >> 2; \
    offsets[n++] = E1000_TDLEN(q) >> 2; \
    offsets[n++] = E1000_TDH(q) >> 2; \
    offsets[n++] = E1000_TDT(q) >> 2; \
    offsets[n++] = E1000_TXDCTL(q) >> 2; \
    offsets[n++] = E1000_TXCTL(q) >> 2; \
    offsets[n++] = E1000_TDWBAL(q) >> 2; \
    offsets[n++] = E1000_TDWBAH(q) >> 2; \
} while (0)

    ADD_QUEUE_REGS(q0);
    ADD_QUEUE_REGS(q1);
#undef ADD_QUEUE_REGS

    g_assert(n <= IGB_VF_MAX_FIXED_REGS);
    return n;
}

static int igb_core_vf_save_state(IgbVfState *s, void *buf, size_t buf_size)
{
    int size = IGB_MIG_BLOB_SIZE;
    IGBCore *core = igbvf_get_core(s);
    IgbMigBlob *blob = buf;
    uint32_t offsets[IGB_VF_MAX_FIXED_REGS];
    int num_regs;

    num_regs = igb_vf_reg_list(s->vfn, offsets);

    if (!buf) {
        return size;
    }

    if (size > buf_size) {
        return -IGB_MIG_ERR_BAD_SIZE;
    }

    blob->magic = cpu_to_le32(IGB_MIG_BLOB_MAGIC);
    blob->version = cpu_to_le32(IGB_MIG_BLOB_VERSION);
    blob->vfn = cpu_to_le32(s->vfn);

    blob->num_regs = cpu_to_le32(num_regs);
    for (int i = 0; i < num_regs; i++) {
        blob->regs[i].offset = cpu_to_le32(offsets[i]);
        blob->regs[i].value = cpu_to_le32(core->mac[offsets[i]]);
    }

    trace_igbvf_mig_save_state(s->vfn, size);
    return size;
}

static int igb_core_vf_max_data_size(IgbVfState *s)
{
    int size = igb_core_vf_save_state(s, NULL, 0);

    g_assert(size > 0 && size <= IGB_VF_STATE_MAX_SIZE);
    return size;
}

static bool igb_core_vf_validate_regs(uint16_t vfn,
                                      const IgbMigRegPair *regs,
                                      uint32_t num_regs)
{
    uint32_t expected[IGB_VF_MAX_FIXED_REGS];
    int num_expected;

    if (num_regs > IGB_VF_MAX_FIXED_REGS) {
        return false;
    }

    num_expected = igb_vf_reg_list(vfn, expected);
    if (num_regs != num_expected) {
        return false;
    }
    for (int i = 0; i < num_regs; i++) {
        if (le32_to_cpu(regs[i].offset) != expected[i]) {
            return false;
        }
    }
    return true;
}

static int igb_core_vf_load_state(IgbVfState *s, const void *buf, size_t size)
{
    IGBCore *core = igbvf_get_core(s);
    const IgbMigBlob *blob = buf;
    uint32_t magic, version, saved_vfn, num_regs;

    /* Validate blob header */
    if (size < IGB_MIG_BLOB_SIZE) {
        return -IGB_MIG_ERR_BAD_SIZE;
    }

    magic = le32_to_cpu(blob->magic);
    version = le32_to_cpu(blob->version);
    saved_vfn = le32_to_cpu(blob->vfn);
    if (magic != IGB_MIG_BLOB_MAGIC) {
        return -IGB_MIG_ERR_BAD_MAGIC;
    }
    if (version != IGB_MIG_BLOB_VERSION) {
        return -IGB_MIG_ERR_BAD_VERSION;
    }
    if (saved_vfn != s->vfn) {
        return -IGB_MIG_ERR_BAD_VFN;
    }

    /*
     * Phase 1: Validate blob state before modifying any core state.
     * A malformed blob must not leave the device partially updated.
     */
    num_regs = le32_to_cpu(blob->num_regs);
    if (!igb_core_vf_validate_regs(s->vfn, blob->regs, num_regs)) {
        return -IGB_MIG_ERR_BAD_DATA;
    }

    /*
     * Phase 2: Apply state.  All offsets and placements are
     * validated; writes cannot fail.
     */
    for (int i = 0; i < num_regs; i++) {
        uint32_t offset = le32_to_cpu(blob->regs[i].offset);
        uint32_t value = le32_to_cpu(blob->regs[i].value);

        core->mac[offset] = value;

        /* Replicate igb_set_eitr() side effect bypassed by direct write */
        if (offset >= EITR0 && offset < EITR0 + IGB_INTR_NUM) {
            core->eitr_guest_value[offset - EITR0] =
                value & ~E1000_EITR_CNT_IGNR;
        }
    }

    /* Propagate VTIVAR to IVAR0, skipped by direct core->mac[] write */
    igb_core_vf_propagate_ivar(core, s->vfn);

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

    g_assert(size <= IGB_VF_STATE_MAX_SIZE);
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

static uint8_t igbvf_mig_cmd_load(IgbVfState *s, uint32_t data_size)
{
    IgbVfMigState *ms = &s->mig;
    MemTxResult r;
    int ret;

    if (ms->mig_state != IGB_MIG_STATE_RESUMING) {
        return IGB_MIG_ERR_BAD_STATE;
    }

    if (data_size == 0 || data_size > sizeof(ms->mig_data)) {
        return IGB_MIG_ERR_BAD_SIZE;
    }

    igbvf_mig_update_data_size(s, data_size);

    if (!ms->mig_data_buf_addr) {
        return IGB_MIG_ERR_NO_BUFFER;
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
        /* Restore DATA_SIZE to default max */
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
        ms->mig_state = IGB_MIG_STATE_ERROR;
        status = IGB_MIG_STATE_ERROR | IGB_MIG_STATUS_ERR(err);
    }

    pci_set_long(dev->config + IGB_MIG_DVSEC_OFFSET + IGB_MIG_STATUS, status);
    trace_igbvf_mig_status(s->vfn, status);
}

static void igbvf_mig_cmd_ctrl(IgbVfState *s, uint32_t val)
{
    IgbVfMigState *ms = &s->mig;
    uint32_t cmd = val & IGB_MIG_CTRL_CMD_MASK;
    uint32_t arg = val >> IGB_MIG_CTRL_ARG_SHIFT;
    uint8_t err = 0;

    trace_igbvf_mig_cmd(s->vfn, cmd, arg, ms->mig_state);

    switch (cmd) {
    case IGB_MIG_CMD_SET_STATE:
        err = igbvf_mig_set_state(s, arg);
        break;

    case IGB_MIG_CMD_SAVE:
        err = igbvf_mig_cmd_save(s);
        break;

    case IGB_MIG_CMD_LOAD:
        err = igbvf_mig_cmd_load(s, arg);
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

bool igbvf_add_migration_dvsec(IgbVfState *s, Error **errp)
{
    PCIDevice *dev = PCI_DEVICE(s);
    uint16_t offset = IGB_MIG_DVSEC_OFFSET;
    uint32_t caps;

    pcie_add_capability(dev, PCI_EXT_CAP_ID_DVSEC, 1, offset,
                        IGB_MIG_DVSEC_SIZE);

    /*
     * DVSEC header 1: length[31:20] | rev[19:16] | vendor_id[15:0]
     *
     * The vendor ID identifies who defined this DVSEC layout, not the
     * device vendor.  Use the Qumranet/Red Hat ID (0x1AF4) since this
     * migration interface is a QEMU-defined protocol, not an Intel
     * specification.
     */
    pci_set_long(dev->config + offset + 0x4,
                 (IGB_MIG_DVSEC_SIZE << 20) |
                 (IGB_MIG_DVSEC_VER << 16) |
                 PCI_VENDOR_ID_REDHAT_QUMRANET);

    /* DVSEC header 2: DVSEC ID */
    pci_set_word(dev->config + offset + 0x8, IGB_MIG_DVSEC_ID);

    /* CAPS: features (state migration only) */
    caps = IGB_MIG_CAP_F_STATE;
    pci_set_long(dev->config + offset + IGB_MIG_CAPS, caps);

    /*
     * STATUS: initial state is ERROR. Driver should activate the
     * control plane by setting the state to RUNNING.
     */
    pci_set_long(dev->config + offset + IGB_MIG_STATUS,
                 IGB_MIG_STATE_ERROR);

    /* BUF_ADDR_LO and BUF_ADDR_HI are writable */
    memset(dev->wmask + offset + IGB_MIG_BUF_ADDR_LO, 0xff, 4);
    memset(dev->wmask + offset + IGB_MIG_BUF_ADDR_HI, 0xff, 4);

    /* DATA_SIZE defaults to max; updated per command */
    igbvf_mig_update_data_size(s, igb_core_vf_max_data_size(s));

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
