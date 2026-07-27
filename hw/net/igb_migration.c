/*
 * QEMU Intel 82576 SR/IOV VF Migration Support
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/bitmap.h"
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
                 IGB_MIG_CAP_F_STATE | IGB_MIG_CAP_F_DIRTY);

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
 *   uint32_t  vfre_vfte    (VFRE in [7:0], VFTE in [15:8])
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

static uint32_t *igb_core_vf_save_vfre_vfte(uint32_t *p,
                                            bool vfre, bool vfte)
{
    *p++ = cpu_to_le32((uint32_t)vfre | ((uint32_t)vfte << 8));
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
         + 2 * sizeof(struct igb_tx)               /* TX context */
         + sizeof(uint32_t);                        /* VFRE + VFTE */
}

static int igb_core_vf_save_state(IgbVfState *s,
                                  void *buf, size_t buf_size)
{
    IgbVfMigState *ms = &s->mig;
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

    /* Per-VF VFRE/VFTE enable bits */
    p = igb_core_vf_save_vfre_vfte(p, ms->mig_saved_vfre,
                                   ms->mig_saved_vfte);

    size = (uint8_t *)p - (uint8_t *)buf;

    trace_igbvf_mig_save_state(s->vfn, size, ms->mig_saved_vfre,
                               ms->mig_saved_vfte,
                               core->mac[VFRE]);
    return size;
}

static int igb_core_vf_max_data_size(IgbVfState *s)
{
    int size = igb_core_vf_save_state(s, NULL, 0);

    g_assert(size > 0 && size <= IGB_VF_STATE_MAX_SIZE);
    return size;
}

static const void *igb_core_vf_load_vfre_vfte(const void *p,
                                              bool *out_vfre, bool *out_vfte)
{
    uint32_t val = le32_to_cpu(*(const uint32_t *)p);

    *out_vfre = !!(val & 0xff);
    *out_vfte = !!((val >> 8) & 0xff);
    return (const uint8_t *)p + sizeof(uint32_t);
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
    IgbVfMigState *ms = &s->mig;
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

    /* Per-VF VFRE/VFTE enable bits */
    p = igb_core_vf_load_vfre_vfte(p, &ms->mig_saved_vfre,
                                   &ms->mig_saved_vfte);

    trace_igbvf_mig_load_state(s->vfn, (uint32_t)size,
                               ms->mig_saved_vfre,
                               ms->mig_saved_vfte);
    return 0;
}

static int igbvf_mig_load(IgbVfState *s, const void *buf, size_t size)
{
    IGBCore *core = igbvf_get_core(s);
    int ret;

    /*
     * Pre-load: Clear the VFLRE bit before restoring state so the PF
     * watchdog does not overwrite what we are about to load.
     */
    core->mac[VFLRE] &= ~BIT(s->vfn);

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

/*
 * =====================================================================
 * Per-VF dirty page tracking
 * =====================================================================
 *
 * All VF DMA writes in igb_core.c go through igb_pci_dma_write(),
 * which calls igb_core_dirty_track_dma() to mark the target page in a
 * per-range bitmap before performing the actual DMA.
 *
 * The IGBCore::vf_dirty[] bitmaps live in IGBCore so they are easily
 * accessible from the core TX and RX paths without reaching back into
 * VF state.
 */

void igb_core_dirty_track_dma(IGBCore *core, int vfn,
                              dma_addr_t addr, dma_addr_t len)
{
    IGBVfDirtyState *ds = &core->vf_dirty[vfn];
    bool matched = false;
    uint32_t i;

    if (!ds->num_ranges) {
        return;
    }

    trace_igb_core_dirty_track_dma(vfn, addr, len);

    for (i = 0; i < ds->num_ranges; i++) {
        IGBVfDirtyRange *r = &ds->ranges[i];
        uint64_t r_end = r->iova + r->size;
        uint64_t dma_end = addr + len;
        uint64_t start, end, start_page, end_page, page;

        if (addr >= r_end || dma_end <= r->iova) {
            continue;
        }

        matched = true;
        start = MAX(addr, r->iova);
        end = MIN(dma_end, r_end);

        start_page = (start - r->iova) / r->page_size;
        end_page = (end - 1 - r->iova) / r->page_size;

        for (page = start_page; page <= end_page; page++) {
            if (page < r->nbits) {
                set_bit(page, r->bitmap);
            }
        }
    }

    if (!matched) {
        trace_igb_core_dirty_track_dma_drop(vfn, addr, len);
    }
}

static IGBVfDirtyState *igb_core_vf_dirty_state(IgbVfState *s)
{
    IGBCore *core = igbvf_get_core(s);
    return &core->vf_dirty[s->vfn];
}

#define IGB_MIG_DIRTY_MAX_PAGES      ((256 * GiB) / (4 * KiB))

static uint32_t igb_core_vf_dirty_enable(IgbVfState *s, uint64_t pgsize,
                                  uint64_t range_iova, uint64_t range_size)
{
    IGBVfDirtyState *ds = igb_core_vf_dirty_state(s);
    IGBVfDirtyRange *r;

    if (ds->num_ranges >= IGB_MIG_CAPS_MAX_RANGES) {
        return IGB_MIG_DIRTY_STATUS_TOO_MANY_RANGES;
    }

    if (!range_size) {
        return IGB_MIG_DIRTY_STATUS_BAD_RANGE;
    }

    if (!pgsize || (range_iova % pgsize) || (range_size % pgsize)) {
        return IGB_MIG_DIRTY_STATUS_BAD_PGSIZE;
    }

    if (range_size / pgsize > IGB_MIG_DIRTY_MAX_PAGES) {
        return IGB_MIG_DIRTY_STATUS_BAD_RANGE;
    }

    r = &ds->ranges[ds->num_ranges];
    r->iova = range_iova;
    r->size = range_size;
    r->page_size = pgsize;
    r->nbits = range_size / pgsize;
    r->bitmap = bitmap_new(r->nbits);
    ds->num_ranges++;
    return IGB_MIG_DIRTY_STATUS_OK;
}

void igb_core_vf_dirty_disable(IgbVfState *s)
{
    IGBVfDirtyState *ds = igb_core_vf_dirty_state(s);
    uint32_t i;

    for (i = 0; i < ds->num_ranges; i++) {
        IGBVfDirtyRange *r = &ds->ranges[i];

        g_free(r->bitmap);
        r->bitmap = NULL;
        r->nbits = 0;
    }
    ds->num_ranges = 0;
}

static bool igb_core_vf_dirty_enabled(IgbVfState *s)
{
    IGBVfDirtyState *ds = igb_core_vf_dirty_state(s);
    return !!ds->num_ranges;
}

static bool igb_core_vf_dirty_query(IgbVfState *s,
                             void *buf, size_t buf_size, size_t *out_size,
                             uint64_t range_iova, uint64_t range_size)
{
    IGBVfDirtyState *ds = igb_core_vf_dirty_state(s);
    uint32_t i;

    for (i = 0; i < ds->num_ranges; i++) {
        IGBVfDirtyRange *r = &ds->ranges[i];
        uint64_t start_page, range_pages, count;

        if (range_iova < r->iova ||
            range_iova + range_size > r->iova + r->size) {
            continue;
        }

        start_page = (range_iova - r->iova) / r->page_size;
        range_pages = (uint64_t)range_size / r->page_size;
        count = MIN(range_pages, (uint64_t)buf_size * 8);

        memset(buf, 0, buf_size);

        if (start_page < r->nbits) {
            uint64_t avail = r->nbits - start_page;
            uint64_t n = MIN(count, avail);

            bitmap_copy_with_src_offset(buf, r->bitmap, start_page, n);
            bitmap_clear(r->bitmap, start_page, n);
        }

        *out_size = bitmap_empty(buf, count) ? 0 : DIV_ROUND_UP(count, 8);
        return true;
    }

    *out_size = 0;
    return false;
}

/* Quiesce a VF by disabling its RX and TX at the PF level. */
static void igb_core_vf_quiesce(IgbVfState *s)
{
    IgbVfMigState *ms = &s->mig;
    IGBCore *core = igbvf_get_core(s);

    ms->mig_saved_vfre = !!(core->mac[VFRE] & BIT(s->vfn));
    ms->mig_saved_vfte = !!(core->mac[VFTE] & BIT(s->vfn));

    core->mac[VFRE] &= ~BIT(s->vfn);
    core->mac[VFTE] &= ~BIT(s->vfn);
    trace_igbvf_mig_quiesce(s->vfn, core->mac[VFRE], core->mac[VFTE]);
}

/*
 * Send a RARP broadcast so the network bridge relearns which port
 * carries this VF's MAC after migration.
 */
static void igb_core_vf_send_rarp(IGBCore *core, uint16_t vfn)
{
    uint8_t mac[ETH_ALEN];
    uint8_t buf[60];

    if (!igb_core_vf_get_mac(core, vfn, mac)) {
        trace_igbvf_mig_no_mac(vfn);
        return;
    }

    trace_igbvf_mig_send_rarp(vfn, mac[0], mac[1], mac[2],
                              mac[3], mac[4], mac[5]);

    memset(buf, 0xff, ETH_ALEN);
    memcpy(buf + 6, mac, ETH_ALEN);
    stw_be_p(buf + 12, 0x8035);     /* ETH_P_RARP */
    stw_be_p(buf + 14, 1);          /* hw addr space: ethernet */
    stw_be_p(buf + 16, ETH_P_IP);   /* protocol addr space */
    buf[18] = 6; buf[19] = 4;       /* hw/proto addr lengths */
    stw_be_p(buf + 20, 3);          /* opcode: RARP request */
    memcpy(buf + 22, mac, ETH_ALEN);
    memset(buf + 28, 0, 32);

    qemu_send_packet_raw(qemu_get_queue(core->owner_nic), buf, sizeof(buf));
}

static void igb_core_vf_unquiesce(IgbVfState *s)
{
    IgbVfMigState *ms = &s->mig;
    IGBCore *core = igbvf_get_core(s);
    bool re = ms->mig_saved_vfre;
    bool te = ms->mig_saved_vfte;

    if (re) {
        core->mac[VFRE] |= BIT(s->vfn);
    } else {
        core->mac[VFRE] &= ~BIT(s->vfn);
    }
    if (te) {
        core->mac[VFTE] |= BIT(s->vfn);
    } else {
        core->mac[VFTE] &= ~BIT(s->vfn);
    }

    trace_igbvf_mig_unquiesce(s->vfn, core->mac[VFRE], core->mac[VFTE]);

    if (re) {
        igb_core_vf_rearm_irqs(core, s->vfn);
    }

    /* TODO : RARP should be sent only if resumed */
    igb_core_vf_send_rarp(core, s->vfn);
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
    case IGB_MIG_STATE_PRE_COPY:
        if (old != IGB_MIG_STATE_RUNNING) {
            return false;
        }
        /*
         * Take an initial snapshot so DATA_SIZE reflects the actual
         * state size and DATA_AVAIL is set for the driver.
         */
        ret = igb_core_vf_save_state(s, ms->mig_data, sizeof(ms->mig_data));
        if (ret < 0) {
            ms->mig_error = -ret;
            ms->mig_state = IGB_MIG_STATE_ERROR;
            return false;
        }
        ms->mig_data_size = ret;
        break;

    case IGB_MIG_STATE_STOP:
        if (old != IGB_MIG_STATE_RUNNING &&
            old != IGB_MIG_STATE_STOP_COPY &&
            old != IGB_MIG_STATE_PRE_COPY &&
            old != IGB_MIG_STATE_RESUMING &&
            old != IGB_MIG_STATE_ERROR) {
            return false;
        }
        /* Restore DATA_SIZE to max, same as at reset */
        ms->mig_data_size = igb_core_vf_max_data_size(s);
        if (old == IGB_MIG_STATE_PRE_COPY ||
            old == IGB_MIG_STATE_STOP_COPY ||
            old == IGB_MIG_STATE_ERROR) {
            igb_core_vf_dirty_disable(s);
        }
        if (old == IGB_MIG_STATE_RUNNING ||
            old == IGB_MIG_STATE_PRE_COPY) {
            igb_core_vf_quiesce(s);
        }
        break;

    case IGB_MIG_STATE_RUNNING:
        if (old != IGB_MIG_STATE_STOP &&
            old != IGB_MIG_STATE_PRE_COPY) {
            return false;
        }
        if (old == IGB_MIG_STATE_PRE_COPY) {
            igb_core_vf_dirty_disable(s);
        }
        if (old == IGB_MIG_STATE_STOP) {
            igb_core_vf_unquiesce(s);
        }
        break;

    case IGB_MIG_STATE_STOP_COPY:
        if (old != IGB_MIG_STATE_STOP &&
            old != IGB_MIG_STATE_PRE_COPY) {
            return false;
        }
        if (old == IGB_MIG_STATE_PRE_COPY) {
            igb_core_vf_quiesce(s);
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
    if ((ms->mig_state == IGB_MIG_STATE_STOP_COPY ||
         ms->mig_state == IGB_MIG_STATE_PRE_COPY) && ms->mig_data_size > 0) {
        status |= IGB_MIG_STATUS_DATA_AVAIL;
    }

    /*
     * QUIESCED tells the driver it is safe to read device state.
     * STOP and STOP_COPY must guarantee that no VF DMA is in flight;
     * igb_core_vf_quiesce() enforces this by disabling RX/TX for the
     * VF.
     *
     * Under QEMU, DMA completes synchronously within the MMIO handler
     * so there is nothing to drain, but real hardware would need this.
     */
    if (ms->mig_state == IGB_MIG_STATE_STOP ||
        ms->mig_state == IGB_MIG_STATE_STOP_COPY) {
        status |= IGB_MIG_STATUS_QUIESCED;
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
    case IGB_MIG_STATE_PRE_COPY:
        /*
         * Re-snapshot device state so the driver can read a fresh
         * copy on each pre-copy iteration.
         */
        ret = igb_core_vf_save_state(s, ms->mig_data, sizeof(ms->mig_data));
        if (ret < 0) {
            ms->mig_error = -ret;
            ms->mig_state = IGB_MIG_STATE_ERROR;
            return;
        }
        ms->mig_data_size = ret;
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
        val = IGB_MIG_CAP_F_STATE | IGB_MIG_CAP_F_DIRTY |
            (IGB_MIG_CAPS_MAX_RANGES << IGB_MIG_CAPS_MAX_RANGES_SHIFT) |
            (1u << 12);  /* 4K page size supported */
        break;
    case IGB_MIG_VERSION:
        val = IGB_MIG_CAP_VERSION;
        break;
    case IGB_MIG_DATA_SIZE:
        val = ms->mig_data_size;
        break;
    case IGB_MIG_DIRTY_PGSIZE:
        val = ms->mig_dirty_pgsize ? ms->mig_dirty_pgsize
            : IGB_MIG_DIRTY_DEFAULT_PGSIZE;
        break;
    case IGB_MIG_DIRTY_BUF_ADDR_LO:
        val = (uint32_t)ms->mig_dirty_buf_addr;
        break;
    case IGB_MIG_DIRTY_BUF_ADDR_HI:
        val = (uint32_t)(ms->mig_dirty_buf_addr >> 32);
        break;
    case IGB_MIG_DIRTY_STATUS:
        val = ms->mig_dirty_status;
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

static uint32_t igbvf_mig_dirty_count(const void *bitmap, size_t size)
{
    const unsigned long *p = bitmap;
    size_t n = size / sizeof(unsigned long);
    uint32_t count = 0;

    for (size_t i = 0; i < n; i++) {
        count += ctpopl(p[i]);
    }
    return count;
}

static void igbvf_mig_dirty_query(IgbVfState *s, uint64_t pgsize)
{
    IgbVfMigState *ms = &s->mig;
    PCIDevice *dev = pcie_sriov_get_pf(PCI_DEVICE(s));
    uint64_t buf_addr = ms->mig_dirty_buf_addr;
    uint64_t range_iova = 0, range_size = 0;
    uint32_t bmp_bytes;
    size_t out_size;
    g_autofree void *bitmap = NULL;
    bool valid;

    ldq_le_pci_dma(dev,
                   buf_addr + offsetof(struct igb_mig_dirty_query, iova),
                   &range_iova, MEMTXATTRS_UNSPECIFIED);
    ldq_le_pci_dma(dev,
                   buf_addr + offsetof(struct igb_mig_dirty_query, size),
                   &range_size, MEMTXATTRS_UNSPECIFIED);

    bmp_bytes = DIV_ROUND_UP(range_size / pgsize, 8);
    bitmap = g_malloc0(bmp_bytes);

    valid = igb_core_vf_dirty_query(s, bitmap, bmp_bytes, &out_size,
                                    range_iova, range_size);

    if (valid && out_size) {
        if (pci_dma_write(dev,
                    buf_addr + offsetof(struct igb_mig_dirty_query, bitmap),
                    bitmap, out_size)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "igbvf: VF%u dirty bitmap write failed at 0x%" PRIx64 "\n",
                s->vfn, buf_addr);
            valid = false;
        }
    }

    uint32_t dirty_pages = valid ? igbvf_mig_dirty_count(bitmap, out_size) : 0;

    stl_le_pci_dma(dev,
                   buf_addr + offsetof(struct igb_mig_dirty_query, bitmap_size),
                   valid ? out_size : 0, MEMTXATTRS_UNSPECIFIED);
    stl_le_pci_dma(dev,
                   buf_addr + offsetof(struct igb_mig_dirty_query, dirty_page_count),
                   dirty_pages, MEMTXATTRS_UNSPECIFIED);
    stl_le_pci_dma(dev,
                   buf_addr + offsetof(struct igb_mig_dirty_query, status),
                   valid ? IGB_MIG_DIRTY_STATUS_COMPLETE : 0,
                   MEMTXATTRS_UNSPECIFIED);

    trace_igbvf_mig_dirty_query(s->vfn, (uint64_t)out_size, dirty_pages);
}

static void igbvf_mig_dirty_ctrl(IgbVfState *s, uint32_t val)
{
    IgbVfMigState *ms = &s->mig;
    uint64_t pgsize = ms->mig_dirty_pgsize
        ? ms->mig_dirty_pgsize : IGB_MIG_DIRTY_DEFAULT_PGSIZE;

    switch (val) {
    case IGB_MIG_DIRTY_CTRL_ENABLE:
        ms->mig_dirty_status = igb_core_vf_dirty_enable(s, pgsize,
                                      ms->mig_dirty_range_iova,
                                      ms->mig_dirty_range_size);
        if (ms->mig_dirty_status) {
            break;
        }
        trace_igbvf_mig_dirty_enable(s->vfn, pgsize,
                                     ms->mig_dirty_range_size / pgsize);
        break;
    case IGB_MIG_DIRTY_CTRL_DISABLE:
        igb_core_vf_dirty_disable(s);
        ms->mig_dirty_status = IGB_MIG_DIRTY_STATUS_OK;
        trace_igbvf_mig_dirty_disable(s->vfn);
        break;
    case IGB_MIG_DIRTY_CTRL_QUERY:
        if (!igb_core_vf_dirty_enabled(s)) {
            ms->mig_dirty_status = IGB_MIG_DIRTY_STATUS_NOT_ENABLED;
            break;
        }
        if (!ms->mig_dirty_buf_addr) {
            ms->mig_dirty_status = IGB_MIG_DIRTY_STATUS_NO_BUFFER;
            break;
        }
        igbvf_mig_dirty_query(s, pgsize);
        ms->mig_dirty_status = IGB_MIG_DIRTY_STATUS_OK;
        break;
    }
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
    case IGB_MIG_DIRTY_PGSIZE:
        ms->mig_dirty_pgsize = (uint32_t)val;
        break;
    case IGB_MIG_DIRTY_CTRL:
        igbvf_mig_dirty_ctrl(s, (uint32_t)val);
        break;
    case IGB_MIG_DIRTY_RANGE_IOVA_LO:
        ms->mig_dirty_range_iova =
            deposit64(ms->mig_dirty_range_iova, 0, 32, val);
        break;
    case IGB_MIG_DIRTY_RANGE_IOVA_HI:
        ms->mig_dirty_range_iova =
            deposit64(ms->mig_dirty_range_iova, 32, 32, val);
        break;
    case IGB_MIG_DIRTY_RANGE_SIZE:
        ms->mig_dirty_range_size = (uint32_t)val;
        break;
    case IGB_MIG_DIRTY_BUF_ADDR_LO:
        ms->mig_dirty_buf_addr =
            deposit64(ms->mig_dirty_buf_addr, 0, 32, val);
        break;
    case IGB_MIG_DIRTY_BUF_ADDR_HI:
        ms->mig_dirty_buf_addr =
            deposit64(ms->mig_dirty_buf_addr, 32, 32, val);
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

    igb_core_vf_dirty_disable(s);
    ms->mig_dirty_pgsize = 0;
    ms->mig_dirty_range_iova = 0;
    ms->mig_dirty_range_size = 0;
    ms->mig_dirty_buf_addr = 0;
    ms->mig_dirty_status = IGB_MIG_DIRTY_STATUS_OK;
    ms->mig_saved_vfre = true;
    ms->mig_saved_vfte = true;
    trace_igbvf_mig_reset(s->vfn);
}
