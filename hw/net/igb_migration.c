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

typedef struct IgbMigTxCtx {
    uint32_t ctx_desc[8];         /* 2 × adv_tx_context_desc (4 dwords each) */
    uint32_t first_cmd_type_len;
    uint32_t first_olinfo_status;
    uint32_t first;
    uint32_t skip_cp;
} IgbMigTxCtx;

#define IGB_VF_MAX_FIXED_REGS     64
#define IGB_VF_MAX_RA_REGS        48  /* (16 + 8) RA entries × 2 (RAL+RAH) */

typedef struct IgbMigBlob {
    uint32_t magic;
    uint32_t version;
    uint32_t vfn;
    uint32_t num_regs;
    IgbMigRegPair regs[IGB_VF_MAX_FIXED_REGS];
    uint32_t num_ra;
    IgbMigRegPair ra[IGB_VF_MAX_RA_REGS];
    uint32_t num_tx_ctx;
    IgbMigTxCtx tx_ctx[2];
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
    offsets[n++] = E1000_PVTEIMS(vfn) >> 2;
    offsets[n++] = E1000_PVTEIMC(vfn) >> 2;
    offsets[n++] = E1000_PVTEIAC(vfn) >> 2;
    offsets[n++] = E1000_PVTEIAM(vfn) >> 2;
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

/*
 * Scan RA and RA2 arrays for receive address entries assigned to
 * this VF. The PF driver picks the RA slot, so we cannot use a
 * fixed index - instead check each entry's pool bits.
 */
static int igb_core_vf_save_ra(IGBCore *core, uint16_t vfn,
                               IgbMigRegPair *regs)
{
    uint32_t vf_pool_bit = E1000_RAH_POOL_1 << vfn;
    int n = 0;
    static const struct {
        uint32_t base;
        int count;
    } ra_banks[] = {
        { RA,  16 },
        { RA2,  8 },
    };

    for (int i = 0; i < ARRAY_SIZE(ra_banks); i++) {
        for (int j = 0; j < ra_banks[i].count; j++) {
            uint32_t ral_off = ra_banks[i].base + j * 2;
            uint32_t rah_off = ra_banks[i].base + j * 2 + 1;
            uint32_t rah_val = core->mac[rah_off];

            if ((rah_val & E1000_RAH_AV) && (rah_val & vf_pool_bit)) {
                regs[n].offset = cpu_to_le32(ral_off);
                regs[n].value = cpu_to_le32(core->mac[ral_off]);
                n++;
                regs[n].offset = cpu_to_le32(rah_off);
                regs[n].value = cpu_to_le32(rah_val);
                n++;
            }
        }
    }
    return n;
}

static void igb_core_vf_save_tx_ctx(IGBCore *core, int queue,
                                    IgbMigTxCtx *tx)
{
    struct igb_tx *src = &core->tx[queue];

    memcpy(tx->ctx_desc, src->ctx, sizeof(tx->ctx_desc));
    tx->first_cmd_type_len = cpu_to_le32(src->first_cmd_type_len);
    tx->first_olinfo_status = cpu_to_le32(src->first_olinfo_status);
    tx->first = cpu_to_le32(src->first);
    tx->skip_cp = cpu_to_le32(src->skip_cp);
}

static int igb_core_vf_save_state(IgbVfState *s, void *buf, size_t buf_size)
{
    int size = IGB_MIG_BLOB_SIZE;
    IGBCore *core = igbvf_get_core(s);
    IgbMigBlob *blob = buf;
    uint32_t offsets[IGB_VF_MAX_FIXED_REGS];
    int num_regs;
    int q0 = s->vfn;
    int q1 = s->vfn + IGB_NUM_VM_POOLS;

    /*
     * Save PVT shadow registers (PVTEIMS/PVTEIAC/PVTEIAM) instead of
     * extracting from PF aggregates - the L1 PF driver may have
     * transiently cleared EIMS via EIMC. The load path ORs them back.
     */
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

    blob->num_ra = cpu_to_le32(igb_core_vf_save_ra(core, s->vfn, blob->ra));

    blob->num_tx_ctx = cpu_to_le32(2);
    igb_core_vf_save_tx_ctx(core, q0, &blob->tx_ctx[0]);
    igb_core_vf_save_tx_ctx(core, q1, &blob->tx_ctx[1]);

    trace_igbvf_mig_save_state(s->vfn, size);
    return size;
}

static int igb_core_vf_max_data_size(IgbVfState *s)
{
    int size = igb_core_vf_save_state(s, NULL, 0);

    g_assert(size > 0 && size <= IGB_VF_STATE_MAX_SIZE);
    return size;
}

static void igb_core_vf_load_tx_ctx(IGBCore *core, int queue,
                                    const IgbMigTxCtx *tx)
{
    struct igb_tx *dst = &core->tx[queue];

    /*
     * Preserve the destination's tx_pkt - it's a host-side object,
     * not guest state
     */
    memcpy(dst->ctx, tx->ctx_desc, sizeof(dst->ctx));
    dst->first_cmd_type_len = le32_to_cpu(tx->first_cmd_type_len);
    dst->first_olinfo_status = le32_to_cpu(tx->first_olinfo_status);
    dst->first = le32_to_cpu(tx->first);
    dst->skip_cp = le32_to_cpu(tx->skip_cp);
}

static uint32_t igb_vf_relocate_offset(uint32_t offset,
                                       const uint32_t *src_offsets,
                                       const uint32_t *dst_offsets,
                                       int num_offsets)
{
    for (int i = 0; i < num_offsets; i++) {
        if (src_offsets[i] == offset) {
            return dst_offsets[i];
        }
    }
    return 0;
}

static int igb_core_vf_load_state(IgbVfState *s, const void *buf, size_t size)
{
    IGBCore *core = igbvf_get_core(s);
    uint32_t src_offsets[IGB_VF_MAX_FIXED_REGS];
    uint32_t dst_offsets[IGB_VF_MAX_FIXED_REGS];
    int q0 = s->vfn;
    int q1 = s->vfn + IGB_NUM_VM_POOLS;

    if (size < IGB_MIG_BLOB_SIZE) {
        return -IGB_MIG_ERR_BAD_SIZE;
    }

    const IgbMigBlob *blob = buf;

    uint32_t magic = le32_to_cpu(blob->magic);
    uint32_t version = le32_to_cpu(blob->version);
    uint32_t saved_vfn = le32_to_cpu(blob->vfn);
    uint32_t num_regs = le32_to_cpu(blob->num_regs);

    if (magic != IGB_MIG_BLOB_MAGIC) {
        return -IGB_MIG_ERR_BAD_MAGIC;
    }
    if (version != IGB_MIG_BLOB_VERSION) {
        return -IGB_MIG_ERR_BAD_VERSION;
    }
    if (num_regs > IGB_VF_MAX_FIXED_REGS) {
        return -IGB_MIG_ERR_BAD_SIZE;
    }

    uint32_t num_ra = le32_to_cpu(blob->num_ra);
    if (num_ra > IGB_VF_MAX_RA_REGS) {
        return -IGB_MIG_ERR_BAD_SIZE;
    }

    int num_offsets = igb_vf_reg_list(saved_vfn, src_offsets);
    igb_vf_reg_list(s->vfn, dst_offsets);

    for (uint32_t i = 0; i < num_regs; i++) {
        uint32_t src_off = le32_to_cpu(blob->regs[i].offset);
        uint32_t value = le32_to_cpu(blob->regs[i].value);
        uint32_t offset = igb_vf_relocate_offset(src_off,
            src_offsets, dst_offsets, num_offsets);
        if (!offset) {
            return -IGB_MIG_ERR_BAD_SIZE;
        }

        core->mac[offset] = value;

        /*
         * Sync EITR to eitr_guest_value[] shadow array, stripping
         * E1000_EITR_CNT_IGNR so guest register readback returns the
         * correct value.
         */
        if (offset >= EITR0 && offset < EITR0 + IGB_INTR_NUM) {
            core->eitr_guest_value[offset - EITR0] =
                value & ~E1000_EITR_CNT_IGNR;
        }
    }

    /*
     * MSI-X table/PBA is not saved - L1's VFIO reprograms it with
     * destination-specific IRTE references after migration.
     */

    uint32_t src_pool = E1000_RAH_POOL_1 << saved_vfn;
    uint32_t dst_pool = E1000_RAH_POOL_1 << s->vfn;

    for (uint32_t i = 0; i < num_ra; i++) {
        uint32_t offset = le32_to_cpu(blob->ra[i].offset);
        uint32_t value = le32_to_cpu(blob->ra[i].value);

        /* RAH entries: swap pool ownership bits */
        if (offset >= RA && offset < RA + 32 && (offset - RA) % 2 == 1) {
            value = (value & ~src_pool) | dst_pool;
        }
        if (offset >= RA2 && offset < RA2 + 16 && (offset - RA2) % 2 == 1) {
            value = (value & ~src_pool) | dst_pool;
        }

        core->mac[offset] = value;
    }

    uint32_t num_tx = le32_to_cpu(blob->num_tx_ctx);
    if (num_tx != 2) {
        return -IGB_MIG_ERR_BAD_SIZE;
    }

    igb_core_vf_load_tx_ctx(core, q0, &blob->tx_ctx[0]);
    igb_core_vf_load_tx_ctx(core, q1, &blob->tx_ctx[1]);

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
