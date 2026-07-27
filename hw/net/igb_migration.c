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

static IGBCore *igbvf_get_core(IgbVfState *s)
{
    return igb_pf_get_core(pcie_sriov_get_pf(PCI_DEVICE(s)));
}

/*
 * =====================================================================
 * Per-VF state serialization / deserialization
 * =====================================================================
 *
 * Wire format:
 *   uint32_t  magic        (IGB_MIG_CAP_MAGIC)
 *   uint32_t  version      (1)
 *   uint32_t  vfn          (VF number)
 *   uint32_t  num_regs     (total register pairs, fixed + RA)
 *   { uint32_t offset; uint32_t value; } regs[num_regs]
 *   uint32_t  num_tx_ctx   (number of TX queue context blocks)
 *   { raw struct igb_tx data } tx_ctx[num_tx_ctx]
 */

/* Maximum number of registers in the VF state slice */
#define IGB_VF_MAX_REGS 128

/* Register offsets that constitute a VF's state slice */
static void igb_vf_reg_list(uint16_t vfn, uint32_t *offsets, int *count)
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

    /* Mailbox */
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

    g_assert(n <= IGB_VF_MAX_REGS);
    *count = n;
}

/*
 * Scan RA and RA2 arrays for receive address entries assigned to
 * this VF. The PF driver picks the RA slot, so we cannot use a
 * fixed index - instead check each entry's pool bits.
 */
static uint32_t *igb_core_vf_save_ra(IGBCore *core, uint16_t vfn,
                                     uint32_t *p, int *total_regs)
{
    uint32_t vf_pool_bit = E1000_RAH_POOL_1 << vfn;
    static const struct {
        uint32_t base;
        int count;
    } ra_banks[] = {
        { RA,  16 },
        { RA2,  8 },
    };
    int i, j;

    for (i = 0; i < ARRAY_SIZE(ra_banks); i++) {
        for (j = 0; j < ra_banks[i].count; j++) {
            uint32_t ral_off = ra_banks[i].base + j * 2;
            uint32_t rah_off = ra_banks[i].base + j * 2 + 1;
            uint32_t rah_val = core->mac[rah_off];

            if ((rah_val & E1000_RAH_AV) && (rah_val & vf_pool_bit)) {
                *p++ = cpu_to_le32(ral_off);
                *p++ = cpu_to_le32(core->mac[ral_off]);
                *p++ = cpu_to_le32(rah_off);
                *p++ = cpu_to_le32(rah_val);
                *total_regs += 2;
            }
        }
    }
    return p;
}

static uint32_t *igb_core_vf_save_tx_ctx(IGBCore *core, int queue,
                                         uint32_t *p)
{
    memcpy(p, &core->tx[queue], sizeof(struct igb_tx));
    return (uint32_t *)((uint8_t *)p + sizeof(struct igb_tx));
}

static size_t igb_core_vf_state_max_size(int num_fixed_regs)
{
    int max_ra_entries = 16 + 8; /* RA bank (16) + RA2 bank (8) */
    int max_ra_regs = max_ra_entries * 2; /* RAL + RAH per entry */

    return 4 * sizeof(uint32_t)                    /* header */
         + num_fixed_regs * 2 * sizeof(uint32_t)   /* fixed reg pairs */
         + max_ra_regs * 2 * sizeof(uint32_t)      /* RA reg pairs */
         + sizeof(uint32_t)                        /* num_tx_ctx */
         + 2 * sizeof(struct igb_tx);              /* TX context */
}

static int igb_core_vf_save_state(IgbVfState *s,
                                  void *buf, size_t buf_size)
{
    IGBCore *core = igbvf_get_core(s);
    uint32_t offsets[IGB_VF_MAX_REGS];
    int num_regs, total_regs;
    uint32_t *p = buf;
    uint32_t *num_regs_p;
    int i, size;
    int q0 = s->vfn;
    int q1 = s->vfn + IGB_NUM_VM_POOLS;

    /*
     * Save PVT shadow registers (PVTEIMS/PVTEIAC/PVTEIAM) instead of
     * extracting from PF aggregates - the L1 PF driver may have
     * transiently cleared EIMS via EIMC. The load path ORs them back.
     */
    igb_vf_reg_list(s->vfn, offsets, &num_regs);

    if (!buf) {
        return igb_core_vf_state_max_size(num_regs);
    }

    if (igb_core_vf_state_max_size(num_regs) > buf_size) {
        return -IGB_MIG_ERR_BAD_SIZE;
    }

    /* Header: magic, version, vfn, num_regs (updated below) */
    *p++ = cpu_to_le32(IGB_MIG_CAP_MAGIC);
    *p++ = cpu_to_le32(1); /* version */
    *p++ = cpu_to_le32(s->vfn);
    num_regs_p = p;
    *p++ = cpu_to_le32(num_regs);

    for (i = 0; i < num_regs; i++) {
        *p++ = cpu_to_le32(offsets[i]);
        *p++ = cpu_to_le32(core->mac[offsets[i]]);
    }

    total_regs = num_regs;

    p = igb_core_vf_save_ra(core, s->vfn, p, &total_regs);

    *num_regs_p = cpu_to_le32(total_regs);

    /* TX context descriptors for this VF's two queues */
    *p++ = cpu_to_le32(2); /* num_tx_ctx */
    p = igb_core_vf_save_tx_ctx(core, q0, p);
    p = igb_core_vf_save_tx_ctx(core, q1, p);

    size = (uint8_t *)p - (uint8_t *)buf;

    trace_igbvf_mig_save_state(s->vfn, size);
    return size;
}

static int igb_core_vf_max_data_size(IgbVfState *s)
{
    int size = igb_core_vf_save_state(s, NULL, 0);

    g_assert(size > 0 && size <= IGB_VF_STATE_MAX_SIZE);
    return size;
}

static const void *igb_core_vf_load_tx_ctx(IGBCore *core, int queue,
                                           const void *data)
{
    struct NetTxPkt *saved_pkt = core->tx[queue].tx_pkt;

    memcpy(&core->tx[queue], data, sizeof(struct igb_tx));
    core->tx[queue].tx_pkt = saved_pkt;
    return (const uint8_t *)data + sizeof(struct igb_tx);
}

static int igb_core_vf_load_state(IgbVfState *s,
                                  const void *buf, size_t size)
{
    IGBCore *core = igbvf_get_core(s);
    const uint32_t *p = buf;
    uint32_t magic, version, saved_vfn, num_regs, num_tx;
    int i;
    int q0 = s->vfn;
    int q1 = s->vfn + IGB_NUM_VM_POOLS;

    magic = le32_to_cpu(*p++);
    version = le32_to_cpu(*p++);
    saved_vfn = le32_to_cpu(*p++);
    num_regs = le32_to_cpu(*p++);

    if (magic != IGB_MIG_CAP_MAGIC) {
        return -IGB_MIG_ERR_BAD_MAGIC;
    }
    if (version != IGB_MIG_CAP_VERSION) {
        return -IGB_MIG_ERR_BAD_VERSION;
    }
    if (saved_vfn != s->vfn) {
        return -IGB_MIG_ERR_BAD_VFN;
    }
    if (num_regs > IGB_VF_MAX_REGS) {
        return -IGB_MIG_ERR_BAD_SIZE;
    }

    for (i = 0; i < num_regs; i++) {
        uint32_t offset = le32_to_cpu(*p++);
        uint32_t value = le32_to_cpu(*p++);

        if (offset < E1000E_MAC_SIZE) {
            core->mac[offset] = value;

            /*
             * Sync EITR to eitr_guest_value[] shadow array, stripping
             * E1000_EITR_CNT_IGNR so guest register readback returns
             * the correct value.
             */
            if (offset >= EITR0 && offset < EITR0 + IGB_INTR_NUM) {
                core->eitr_guest_value[offset - EITR0] =
                    value & ~E1000_EITR_CNT_IGNR;
            }
        }
    }

    num_tx = le32_to_cpu(*p++);
    if (num_tx == 2) {
        p = igb_core_vf_load_tx_ctx(core, q0, p);
        p = igb_core_vf_load_tx_ctx(core, q1, p);
    }

    /*
     * MSI-X table/PBA is not saved - L1's VFIO reprograms it with
     * destination-specific IRTE references after migration.
     */

    trace_igbvf_mig_load_state(s->vfn, (uint32_t)size);
    return 0;
}

static int igbvf_mig_load(IgbVfState *s, const void *buf, size_t size)
{
    IGBCore *core = igbvf_get_core(s);
    int ret;

    ret = igb_core_vf_load_state(s, buf, size);
    if (ret < 0) {
        return ret;
    }

    /*
     * Post-load: sync VF interrupt and routing state to PF aggregates
     */
    igb_core_vf_propagate_irqs(core, s->vfn);
    igb_core_vf_propagate_ivar(core, s->vfn);

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
