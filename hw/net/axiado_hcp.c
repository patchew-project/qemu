/*
 * Axiado HCP (Header & Crypto Processing) block - QEMU device model
 *
 * The HCP is the network complex of the Axiado AX3000 SoC. It exposes three
 * MMIO regions:
 *
 *   - EIP-197: an Inside Secure SafeXcel packet engine. It is the bus master
 *     for all packet I/O, driven by four command/result descriptor ring pairs
 *     (CDR/RDR). Packets are routed to and from one of five MACs by an
 *     "application ID" tag carried in the EIP-96 input/output tokens.
 *   - SHIM: MAC configuration, per-MAC MDIO and statistics. Config only; no
 *     packet data flows through it.
 *   - PHY CSR: PCS/SerDes configuration.
 *
 * Only the packet datapath is modelled. The EIP-197's cryptographic
 * transforms are not implemented: descriptors are treated as plain
 * pass-through, which is what the guest driver requests for network traffic.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026 Ratan Lal Dondi <rdondi@axiado.com>
 */

#include "qemu/osdep.h"
#include "hw/net/axiado_hcp.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/dma.h"
#include "system/address-spaces.h"
#include "trace.h"

/*
 * MAC configuration region, within the SHIM MMIO area
 */

#define MAC_R_REVISION         0x000
#define MAC_R_SCRATCH          0x004
#define MAC_R_COMMAND_CONFIG   0x008
#define MAC_R_MAC_0            0x00c
#define MAC_R_MAC_1            0x010
#define MAC_R_FRM_LENGTH       0x014
#define MAC_R_RX_FIFO_SECTIONS 0x01c
#define MAC_R_TX_FIFO_SECTIONS 0x020
#define MAC_R_MDIO_CFG_STATUS  0x030
#define MAC_R_MDIO_COMMAND     0x034
#define MAC_R_MDIO_DATA        0x038
#define MAC_R_MDIO_REGADDR     0x03c
#define MAC_R_STATUS           0x040
#define MAC_R_IF_MODE          0x300
#define MAC_R_PCS_STATUS       0x304

#define MAC_REVISION_VAL       0x10136

#define MII_BMCR   0x00
#define MII_BMSR   0x01

#define MDIO_COMMAND_READ_FLAG BIT(15)

#define SHIM_REG_FIFO_BASE     0x040
#define SHIM_REG_STATS_BASE    0x060

#define FIFO_BIT_RX_RST        0
#define FIFO_BIT_RX_EMPTY      2
#define FIFO_BIT_RX_OVFLOW     3
#define FIFO_BIT_TX_RST        4
#define FIFO_BIT_TX_EMPTY      6
#define FIFO_BIT_TX_OVFLOW     7

/*
 * EIP-207 input classification engine, at offsets from the packet engine base
 * within the EIP-197 region.
 */
#define PE_ICE_SCRATCH_RAM        0x00800
#define PE_ICE_PUE_CTRL           0x00c80
#define PE_ICE_FPP_CTRL           0x00d80

/*
 * Administration RAM, at byte offsets from the ICE scratch RAM base. The
 * classification firmware publishes its version here and sets bit 0 of the
 * matching control word to say it has done so.
 */
#define ICE_ADMIN_IPUE_VERSION    0x00
#define ICE_ADMIN_IFPP_VERSION    0x08
#define ICE_ADMIN_IPUE_CTRL       0x14
#define ICE_ADMIN_IFPP_CTRL       0x18
#define ICE_ADMIN_VERSION_UPDATED (1u << 0)

/* Administration RAM byte offset to an index into the packet engine window. */
#define ICE_ADMIN_WORD(off)       ((PE_ICE_SCRATCH_RAM + (off)) / 4)

/* Version word: major in bits 11..8, minor in 7..4, patch level in 3..0. */
#define ICE_FW_VERSION_FIELD      0xf
#define ICE_FW_VERSION_MAJOR_SH   8
#define ICE_FW_VERSION_MINOR_SH   4
#define ICE_FW_VERSION_PATCH_SH   0

#define ICE_FW_VERSION(maj, min, patch)                                       \
    ((((maj)   & ICE_FW_VERSION_FIELD) << ICE_FW_VERSION_MAJOR_SH) |          \
     (((min)   & ICE_FW_VERSION_FIELD) << ICE_FW_VERSION_MINOR_SH) |          \
     (((patch) & ICE_FW_VERSION_FIELD) << ICE_FW_VERSION_PATCH_SH))

/*
 * The version reported to the guest, which compares it against the version of
 * the classification firmware image it has just downloaded. The two have to
 * agree: report anything else and classification init fails outright. Update
 * this when the guest ships a different classification firmware.
 */
#define ICE_FIRMWARE_VERSION      ICE_FW_VERSION(3, 3, 1)

static void mac_mdio_execute(HCPMac *m)
{
    uint32_t cmd = m->mdio_command;
    uint8_t reg = m->mdio_regaddr ? (m->mdio_regaddr & 0x1f)
                                  : (cmd & 0x1f);

    if (cmd & MDIO_COMMAND_READ_FLAG) {
        m->mdio_data = m->mii_regs[reg];
    } else {
        m->mii_regs[reg] = m->mdio_data & 0xffff;
    }
}

static uint64_t mac_block_read(HCPMac *m, hwaddr off)
{
    switch (off) {
    case MAC_R_REVISION:         return MAC_REVISION_VAL;
    case MAC_R_COMMAND_CONFIG:   return m->command_config;
    case MAC_R_MAC_0:            return m->mac_addr_lo;
    case MAC_R_MAC_1:            return m->mac_addr_hi;
    case MAC_R_FRM_LENGTH:       return m->frm_length;
    case MAC_R_RX_FIFO_SECTIONS: return m->rx_fifo_sections;
    case MAC_R_TX_FIFO_SECTIONS: return m->tx_fifo_sections;
    case MAC_R_MDIO_CFG_STATUS:  return 0;
    case MAC_R_MDIO_COMMAND:     return m->mdio_command;
    case MAC_R_MDIO_DATA:        return m->mdio_data;
    case MAC_R_MDIO_REGADDR:     return m->mdio_regaddr;
    case MAC_R_STATUS:           return 0;
    case MAC_R_IF_MODE:          return m->if_mode;
    case MAC_R_PCS_STATUS:       return 0x0084;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "axiado-hcp: MAC reg read offset 0x%" HWADDR_PRIx "\n",
                      off);
        return 0;
    }
}

static void mac_block_write(HCPMac *m, hwaddr off, uint64_t val)
{
    switch (off) {
    case MAC_R_COMMAND_CONFIG:
        m->command_config = val;
        break;
    case MAC_R_MAC_0:
        m->mac_addr_lo = val;
        break;
    case MAC_R_MAC_1:
        m->mac_addr_hi = val;
        break;
    case MAC_R_FRM_LENGTH:
        m->frm_length = val;
        break;
    case MAC_R_RX_FIFO_SECTIONS:
        m->rx_fifo_sections = val;
        break;
    case MAC_R_TX_FIFO_SECTIONS:
        m->tx_fifo_sections = val;
        break;
    case MAC_R_IF_MODE:
        m->if_mode = val;
        break;
    case MAC_R_MDIO_COMMAND:
        m->mdio_command = val;
        mac_mdio_execute(m);
        break;
    case MAC_R_MDIO_DATA:
        m->mdio_data = val & 0xffff;
        break;
    case MAC_R_MDIO_REGADDR:
        m->mdio_regaddr = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "axiado-hcp: MAC reg write offset 0x%" HWADDR_PRIx
                      " = 0x%" PRIx64 "\n", off, val);
        break;
    }
}

static uint64_t shim_global_read(AXIADOHCPState *s, hwaddr addr)
{
    if (addr >= SHIM_REG_FIFO_BASE &&
        addr <  SHIM_REG_FIFO_BASE + HCP_NUM_MACS * 4) {
        return (1U << FIFO_BIT_TX_EMPTY) | (1U << FIFO_BIT_RX_EMPTY);
    }
    if (addr >= SHIM_REG_STATS_BASE &&
        addr <  SHIM_REG_STATS_BASE + HCP_NUM_MACS * 4) {
        return 0;
    }
    qemu_log_mask(LOG_UNIMP,
                  "axiado-hcp: shim global read 0x%" HWADDR_PRIx "\n", addr);
    return 0;
}

static void shim_global_write(AXIADOHCPState *s, hwaddr addr, uint64_t val)
{
    if (addr >= SHIM_REG_FIFO_BASE &&
        addr <  SHIM_REG_FIFO_BASE + HCP_NUM_MACS * 4) {
        return;
    }
    qemu_log_mask(LOG_UNIMP,
                  "axiado-hcp: shim global write 0x%" HWADDR_PRIx
                  " = 0x%" PRIx64 "\n", addr, val);
}

static int shim_mac_index(hwaddr addr, hwaddr *off_out)
{
    if (addr < HCP_SHIM_MAC_BASE_OFFSET) {
        return -1;
    }
    hwaddr rel = addr - HCP_SHIM_MAC_BASE_OFFSET;
    int idx = rel / HCP_SHIM_MAC_STRIDE;
    if (idx >= HCP_NUM_MACS) {
        return -1;
    }
    *off_out = rel % HCP_SHIM_MAC_STRIDE;
    return idx;
}

static uint64_t axiado_shim_read(void *opaque, hwaddr addr, unsigned size)
{
    AXIADOHCPState *s = AXIADO_HCP(opaque);
    hwaddr off;
    int idx = shim_mac_index(addr, &off);

    if (idx >= 0) {
        return mac_block_read(&s->macs[idx], off);
    }
    return shim_global_read(s, addr);
}

static void axiado_shim_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    AXIADOHCPState *s = AXIADO_HCP(opaque);
    hwaddr off;
    int idx = shim_mac_index(addr, &off);

    if (idx >= 0) {
        mac_block_write(&s->macs[idx], off, val);
        return;
    }
    shim_global_write(s, addr, val);
}

static const MemoryRegionOps shim_ops = {
    .read = axiado_shim_read,
    .write = axiado_shim_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * EIP-197 region, SafeXcel register layout
 */

#define XDR_BASE_LO    0x00
#define XDR_BASE_HI    0x04
#define XDR_SIZE       0x18
#define XDR_DESC_SIZE  0x1c
#define XDR_CFG        0x20
#define XDR_DMA_CFG    0x24
#define XDR_THRESH     0x28
#define XDR_PREP_COUNT 0x2c
#define XDR_PROC_COUNT 0x30
#define XDR_PREP_PNTR  0x34
#define XDR_PROC_PNTR  0x38
#define XDR_STAT       0x3c

#define XDR_STAT_PROC_THRESH_IRQ  0x10

/*
 * Bits 16..27 of xDR_STAT report the descriptor FIFO size in 32-bit words.
 * The guest reads it at ring init to check that the descriptor size, fetch
 * size and threshold it wants to program all fit. 256 words leaves headroom
 * well past any combination the guest asks for.
 */
#define XDR_STAT_FIFO_SIZE_WORDS  256u
#define XDR_STAT_FIFO_SIZE_BITS   ((XDR_STAT_FIFO_SIZE_WORDS & 0xFFFu) << 16)

#define EIP_HIA_XDR_BASE   0x80000
#define EIP_HIA_XDR_STRIDE 0x1000
#define EIP_HIA_RDR_OFF    0x800

#define EIP_HIA_AIC_R_RING0_BASE 0x9E000
#define EIP_AIC_R_CDR_ENABLE_CTRL 0x008
#define EIP_AIC_R_CDR_ENABLED     0x010
#define EIP_AIC_R_CDR_ENABLE_CLR  0x014
#define EIP_AIC_R_RDR_ENABLE_CTRL 0x808
#define EIP_AIC_R_RDR_ENABLED     0x810
#define EIP_AIC_R_RDR_ENABLE_CLR  0x814

#define EIP_HIA_AIC_G_BASE        0x9F000
#define EIP_AIC_G_ENABLE_CTRL     0x808
#define EIP_AIC_G_ENABLED         0x810
#define EIP_AIC_G_ENABLE_CLR      0x814

/* SafeXcel command descriptor - field offsets used here (little-endian). */
#define CMD_DESC_FLAGS_OFF          0     /* particle_size:17, ..., flags */
#define CMD_DESC_DATA_LO_OFF        8
#define CMD_DESC_DATA_HI_OFF        12
#define CMD_DESC_APP_ID_OFF         28    /* EIP-96 input token, app_id << 9 */

/* SafeXcel result descriptor word-0 bit layout */
#define RES_DESC_FLAGS_OFF          0
#define RES_DESC_PARTICLE_MASK      0x1FFFFu
#define RES_DESC_LAST_SEG_BIT       (1u << 22)
#define RES_DESC_FIRST_SEG_BIT      (1u << 23)
#define RES_DESC_DATA_LO_OFF        8
#define RES_DESC_DATA_HI_OFF        12

#define MAX_PKT_LEN 9600

/*
 * DESC_SIZE register encoding:
 *   bits  0..7   = dscr_word_count       (data payload size, words)
 *   bits 16..23  = dscr_offs_word_count  (per-slot stride, words)
 *   bits 29..31  = flags (atp_to_token / atp / 64-bit)
 * The stride between descriptor slots in ring memory is the OFFSET field, not
 * the payload size. Returns 0 if the guest has not programmed the register,
 * which every caller checks before using it as a divisor.
 */
static uint32_t xdr_stride_bytes(HCPxDR *x)
{
    uint32_t offs_words = (x->desc_size >> 16) & 0xFFu;
    return offs_words * 4u;
}

/*
 * Drive the per-ring IRQ line from (STAT & AIC mask). The line is
 * level-triggered, so it must be recomputed - and deasserted - as soon as the
 * guest acks a STAT bit by writing it back, otherwise the interrupt
 * controller re-fires forever.
 */
static void hcp_update_ring_irq(AXIADOHCPState *s, int r)
{
    bool level = false;

    if (s->rings[r].aic_rdr_mask &&
        (s->rings[r].rdr.stat & XDR_STAT_PROC_THRESH_IRQ)) {
        level = true;
    }
    if (s->rings[r].aic_cdr_mask &&
        (s->rings[r].cdr.stat & XDR_STAT_PROC_THRESH_IRQ)) {
        level = true;
    }
    qemu_set_irq(s->irqs[HCP_IRQ_RING_BASE + r], level);
}

static int app_id_to_mac_index(AXIADOHCPState *s, uint16_t app_id)
{
    for (int i = 0; i < HCP_NUM_MACS; i++) {
        if (s->macs[i].app_id == app_id) {
            return i;
        }
    }
    return -1;
}

/*
 * Process all newly-prepared CDR descriptors for ring r. For each: DMA-read
 * the command descriptor, extract the routing app_id, DMA-read the packet,
 * and emit it on the matching MAC's NIC backend.
 *
 * No result descriptor is posted for a transmit. The engine and the guest
 * share one RDR per ring for both transmit completions and receive results;
 * the guest tracks transmit buffer reuse from the CDR consumed count alone,
 * so writing transmit completions would only corrupt receive slots.
 */
static void hcp_process_tx_ring(AXIADOHCPState *s, int r)
{
    HCPxDR *cdr = &s->rings[r].cdr;
    uint32_t stride = xdr_stride_bytes(cdr);

    if (stride == 0 || cdr->size == 0 || cdr->base == 0) {
        return;
    }
    uint32_t ring_descs = cdr->size / stride;
    if (ring_descs == 0) {
        return;
    }

    while (cdr->proc_count < cdr->prep_count) {
        uint32_t idx = (cdr->proc_count / stride) % ring_descs;
        hwaddr desc_addr = cdr->base + (hwaddr)idx * stride;

        uint8_t desc[64];
        size_t read_bytes = MIN(stride, (uint32_t)sizeof(desc));
        if (dma_memory_read(&address_space_memory, desc_addr, desc, read_bytes,
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "axiado-hcp: CDR DMA read failed at 0x%" HWADDR_PRIx
                          "\n", desc_addr);
            break;
        }

        uint32_t flags = ldl_le_p(&desc[CMD_DESC_FLAGS_OFF]);
        uint32_t pkt_len = flags & 0x1FFFFu;
        uint64_t data_addr =
            (uint64_t)ldl_le_p(&desc[CMD_DESC_DATA_LO_OFF])
            | ((uint64_t)ldl_le_p(&desc[CMD_DESC_DATA_HI_OFF]) << 32);
        /*
         * The routing app_id is not a plain descriptor field: it lives in the
         * EIP-96 input token as (app_id << 9).
         */
        uint16_t raw_token = lduw_le_p(&desc[CMD_DESC_APP_ID_OFF]);
        uint16_t app_id = (raw_token >> 9) & 0x7Fu;

        int mac_idx = app_id_to_mac_index(s, app_id);
        if (mac_idx < 0 || pkt_len == 0 || pkt_len > MAX_PKT_LEN) {
            /*
             * An all-zero descriptor is an unused slot, not an error. Only
             * report slots that carry data we could not route.
             */
            if (pkt_len != 0 || raw_token != 0) {
                trace_axiado_hcp_tx_unroutable(r, app_id, pkt_len, data_addr);
            }
        } else {
            g_autofree uint8_t *pkt = g_malloc(pkt_len);
            if (dma_memory_read(&address_space_memory, data_addr, pkt, pkt_len,
                                MEMTXATTRS_UNSPECIFIED) == MEMTX_OK) {
                HCPMac *mac = &s->macs[mac_idx];

                trace_axiado_hcp_tx_packet(r, app_id, mac_idx, pkt_len);
                if (mac->nic) {
                    qemu_send_packet(qemu_get_queue(mac->nic), pkt, pkt_len);
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "axiado-hcp: packet DMA read failed at 0x%"
                              PRIx64 "\n", data_addr);
            }
        }

        cdr->proc_count += stride;
        cdr->proc_pntr  = (cdr->proc_pntr + stride) % cdr->size;
    }
}

static uint64_t eip_xdr_read(HCPxDR *x, hwaddr off)
{
    switch (off) {
    case XDR_BASE_LO:    return x->base & 0xFFFFFFFFu;
    case XDR_BASE_HI:    return (x->base >> 32) & 0xFFFFFFFFu;
    case XDR_SIZE:       return x->size;
    case XDR_DESC_SIZE:  return x->desc_size;
    case XDR_CFG:        return x->cfg;
    case XDR_DMA_CFG:    return x->dma_cfg;
    case XDR_THRESH:     return x->thresh;
    case XDR_PREP_COUNT:
        /*
         * This register is asymmetric. A write means "N more descriptors have
         * been prepared", but a read returns the ring's current fill level:
         * the guest recovers a descriptor count as
         *
         *     ((reg >> 2) & 0x3fffff) / dscr_offs_word_count
         *
         * and uses it to decide how much room is left in the ring. Reporting
         * the cumulative prepared count instead would make that fill level
         * grow without bound, and the guest would eventually see the ring as
         * permanently full and stop transmitting for good.
         *
         * Both counters are byte cursors and the guest's >> 2 undoes the << 2
         * applied on write, so the outstanding byte count is the register
         * value directly. Transmit descriptors are consumed synchronously, so
         * this reads back as empty once a transmit has been processed.
         */
        return x->prep_count > x->proc_count
               ? x->prep_count - x->proc_count : 0;
    case XDR_PROC_COUNT: return x->proc_count;
    case XDR_PREP_PNTR:  return x->prep_pntr;
    case XDR_PROC_PNTR:  return x->proc_pntr;
    case XDR_STAT:       return x->stat | XDR_STAT_FIFO_SIZE_BITS;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "axiado-hcp: eip xdr read offset 0x%" HWADDR_PRIx "\n",
                      off);
        return 0;
    }
}

static int eip_xdr_ring(hwaddr addr, hwaddr *off_out, bool *is_rdr_out)
{
    if (addr < EIP_HIA_XDR_BASE ||
        addr >= EIP_HIA_XDR_BASE + HCP_NUM_RINGS * EIP_HIA_XDR_STRIDE) {
        return -1;
    }
    hwaddr rel = addr - EIP_HIA_XDR_BASE;
    int r = rel / EIP_HIA_XDR_STRIDE;
    hwaddr intra = rel % EIP_HIA_XDR_STRIDE;
    *is_rdr_out = (intra >= EIP_HIA_RDR_OFF);
    *off_out = (*is_rdr_out ? (intra - EIP_HIA_RDR_OFF) : intra) & 0x3F;
    return r;
}

/*
 * xDR write. Returns true if this write was a CDR PREP_COUNT bump that needs
 * follow-up TX processing (caller invokes hcp_process_tx_ring afterward).
 */
static bool eip_xdr_write(HCPxDR *x, hwaddr off, uint64_t val)
{
    switch (off) {
    case XDR_BASE_LO:
        x->base = (x->base & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFFu);
        return false;
    case XDR_BASE_HI:
        x->base = (x->base & 0xFFFFFFFFULL) | ((val & 0xFFFFFFFFu) << 32);
        return false;
    case XDR_SIZE:
        x->size = val;
        return false;
    case XDR_DESC_SIZE:
        x->desc_size = val;
        return false;
    case XDR_CFG:
        x->cfg = val;
        return false;
    case XDR_DMA_CFG:
        x->dma_cfg = val;
        return false;
    case XDR_THRESH:
        x->thresh = val;
        return false;
    case XDR_PREP_COUNT: {
        /*
         * Write layout:
         *   bit 31   = clear-count flag, reset rather than add
         *   bits 2.. = added descriptor word count, i.e. a byte count, since
         *              the per-slot stride is dscr_offs_word_count * 4
         * The guest issues a clear at ring init; treating that as an increment
         * would leave the ring permanently reporting millions of pending
         * descriptors.
         */
        uint32_t add = val & 0x7FFFFFFFu;
        if (val & 0x80000000u) {
            x->prep_count = add;
            x->prep_pntr  = (x->size > 0) ? (add % x->size) : 0;
        } else {
            x->prep_count += add;
            if (x->size > 0) {
                x->prep_pntr = (x->prep_pntr + add) % x->size;
            }
        }
        return true;
    }
    case XDR_PROC_COUNT:
        /*
         * In ownership-word mode this write only acknowledges results the
         * guest has already read; the ownership word is the real handshake.
         * It must not clobber proc_count, which is the engine-side slot
         * cursor and only advances when a slot is actually consumed or
         * produced.
         */
        return false;
    case XDR_PREP_PNTR:
        x->prep_pntr = val;
        return false;
    case XDR_PROC_PNTR:
        x->proc_pntr = val;
        return false;
    case XDR_STAT:
        if (val & XDR_STAT_PROC_THRESH_IRQ) {
            x->stat &= ~XDR_STAT_PROC_THRESH_IRQ;
        }
        return false;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "axiado-hcp: eip xdr write offset 0x%" HWADDR_PRIx
                      " = 0x%" PRIx64 "\n", off, val);
        return false;
    }
}

/*
 * Complete the classification firmware version handshake.
 *
 * Having downloaded the firmware, the guest starts a micro-engine in debug
 * mode at a fixed program counter and polls the administration RAM for a
 * version word that the running microcode would have written. No microcode
 * executes here, so the model publishes the version itself: a write to either
 * engine's control register makes that engine's version word and its updated
 * flag appear, and the guest's poll succeeds on its first read.
 *
 * Without this the guest cannot complete classification init, and the only
 * other way past it is to disable the check in the guest - which a device
 * model should never require.
 */
static void hcp_ice_publish_fw_version(AXIADOHCPState *s, hwaddr ctrl_off)
{
    unsigned version, ctrl;

    QEMU_BUILD_BUG_ON(ICE_ADMIN_WORD(ICE_ADMIN_IFPP_CTRL) >= HCP_EIP_PE_REGS);

    if (ctrl_off == PE_ICE_FPP_CTRL) {
        version = ICE_ADMIN_WORD(ICE_ADMIN_IFPP_VERSION);
        ctrl    = ICE_ADMIN_WORD(ICE_ADMIN_IFPP_CTRL);
    } else {
        version = ICE_ADMIN_WORD(ICE_ADMIN_IPUE_VERSION);
        ctrl    = ICE_ADMIN_WORD(ICE_ADMIN_IPUE_CTRL);
    }

    s->pe_regs[version] = ICE_FIRMWARE_VERSION;
    s->pe_regs[ctrl] |= ICE_ADMIN_VERSION_UPDATED;
}

/*
 * Locate the backing word for a register inside one of the windows the guest
 * writes and reads back, or NULL if this address is not in any of them.
 * Registers outside the windows are either write-only or read-only status,
 * and need no storage.
 */
static uint32_t *eip_reg_slot(AXIADOHCPState *s, hwaddr addr)
{
    struct {
        hwaddr base;
        unsigned regs;
        uint32_t *store;
    } const windows[] = {
        { HCP_EIP_FLUE_FHT_BASE, HCP_EIP_FLUE_FHT_REGS, s->flue_fht },
        { HCP_EIP_PE_BASE,       HCP_EIP_PE_REGS,       s->pe_regs },
        { HCP_EIP_TRC_RAM_BASE,  HCP_EIP_TRC_RAM_REGS,  s->trc_ram },
        { HCP_EIP_CLS_BASE,      HCP_EIP_CLS_REGS,      s->cls_regs },
        { HCP_EIP_FLOW_BASE,     HCP_EIP_FLOW_REGS,     s->flow_regs },
        { HCP_EIP_CACHE_BASE,    HCP_EIP_CACHE_REGS,    s->cache_regs },
    };

    for (int i = 0; i < ARRAY_SIZE(windows); i++) {
        hwaddr base = windows[i].base;

        if (addr >= base && addr < base + windows[i].regs * 4) {
            return &windows[i].store[(addr - base) / 4];
        }
    }
    return NULL;
}

static int eip_aic_r_ring(hwaddr addr, hwaddr *off_out)
{
    hwaddr lo = EIP_HIA_AIC_R_RING0_BASE -
                (HCP_NUM_RINGS - 1) * EIP_HIA_XDR_STRIDE;
    if (addr < lo || addr > EIP_HIA_AIC_R_RING0_BASE + 0xFFF) {
        return -1;
    }
    hwaddr block_base = addr & ~0xFFFULL;
    int r = (EIP_HIA_AIC_R_RING0_BASE - block_base) / EIP_HIA_XDR_STRIDE;
    if (r < 0 || r >= HCP_NUM_RINGS) {
        return -1;
    }
    *off_out = addr & 0xFFF;
    return r;
}

static uint64_t axiado_eip197_read(void *opaque, hwaddr addr, unsigned size)
{
    AXIADOHCPState *s = AXIADO_HCP(opaque);
    uint32_t *slot;
    hwaddr off;
    bool is_rdr;
    int r;

    /*
     * EIP-2xx identification registers, matched before the AIC range dispatch
     * so they take precedence over it. Each sub-block has a version register
     * whose low 16 bits hold a fixed signature and whose high 16 bits encode
     * the revision, plus one or two options registers describing the
     * capabilities the guest may use.
     */
    switch (addr) {
    /*
     * EIP-202 host interface adapter, signature 0x35ca.
     *   0x9fff0 = OPTIONS2: lookaside interfaces (0..3), inline interfaces
     *             (4..7), AXI write channels (16..19), read clusters (20..27)
     *   0x9fff4 = MST_CTRL, answered from the scratchpad
     *   0x9fff8 = OPTIONS: rings (0..3), packet engines (4..8), command and
     *             result FIFO sizes (9..14), host interface (16..19), DMA
     *             length (20..24), host data width (25..27), target alignment
     *             (28..30), 64-bit addressing (31)
     *   0x9fffc = VERSION
     */
    case 0x9FFF0:
        return 0x000400CAu;
    case 0x9FFF8:
        /* Four rings, one packet engine, 64-bit addressing. */
        return 0x80000014u;
    case 0x9FFFC:
        return 0x010135CAu;

    /*
     * EIP-207 classification sub-block, signature 0x30cf.
     *   0xf7ff0 = OPTIONS2, unused by the minimum init path
     *   0xf7ff8 = OPTIONS: bits 1..2 number of cache sets, bits 28..30
     *             number of lookup tables. One of each is the minimum the
     *             guest accepts; all other capability bits stay clear.
     *   0xf7ffc = VERSION
     */
    case 0xF7FF0:
        return 0;
    case 0xF7FF8:
        return (1u << 28) | (1u << 1);
    case 0xF7FFC:
        return 0x010130CFu;

    /* EIP-207s flow look-up engine, same 0x30cf signature. */
    case 0x01FF8:
        return 0;
    case 0x01FFC:
        return 0x010130CFu;

    /*
     * EIP-96 packet engine, signature 0x9f60. Both the PE + 0x13fc and the
     * PE + 0xfffc placements are answered, as the guest probes either
     * depending on the variant it believes it is talking to.
     */
    case 0xA13FC:
    case 0xA0FFC:
        return 0x01019F60u;
    case 0xA13F8:
    case 0xA0FF0:
    case 0xA0FF8:
        return 0;

    /* EIP-201 interrupt controller / HIA xDR block, signature 0x36c9. */
    case 0x8FFF0:
    case 0x8FFFC:
        return 0x010136C9u;
    case 0x8FFF8:
        return 0;

    /* EIP-197 global, signature 0x3ac5. */
    case 0xFFFF0:
    case 0xFFFFC:
        return 0x01013AC5u;
    case 0xFFFF8:
        return 0;
    }

    r = eip_xdr_ring(addr, &off, &is_rdr);
    if (r >= 0) {
        HCPxDR *x = is_rdr ? &s->rings[r].rdr : &s->rings[r].cdr;
        return eip_xdr_read(x, off);
    }

    r = eip_aic_r_ring(addr, &off);
    if (r >= 0) {
        HCPRing *ring = &s->rings[r];
        switch (off) {
        case EIP_AIC_R_CDR_ENABLE_CTRL: return ring->aic_cdr_mask;
        case EIP_AIC_R_CDR_ENABLE_CLR:  return ring->aic_cdr_mask;
        case EIP_AIC_R_CDR_ENABLED:     return 0;
        case EIP_AIC_R_RDR_ENABLE_CTRL: return ring->aic_rdr_mask;
        case EIP_AIC_R_RDR_ENABLE_CLR:  return ring->aic_rdr_mask;
        case EIP_AIC_R_RDR_ENABLED:     return 0;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "axiado-hcp: eip aic_r[%d] read 0x%" HWADDR_PRIx "\n",
                          r, off);
            return 0;
        }
    }

    if (addr >= EIP_HIA_AIC_G_BASE && addr < EIP_HIA_AIC_G_BASE + 0x1000) {
        hwaddr g_off = addr - EIP_HIA_AIC_G_BASE;
        switch (g_off) {
        case EIP_AIC_G_ENABLE_CTRL: return s->aic_g_mask;
        case EIP_AIC_G_ENABLE_CLR:  return s->aic_g_mask;
        case EIP_AIC_G_ENABLED:     return 0;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "axiado-hcp: eip aic_g read 0x%" HWADDR_PRIx "\n",
                          g_off);
            return 0;
        }
    }

    slot = eip_reg_slot(s, addr);
    if (slot) {
        return *slot;
    }

    /* Write-only or read-only-status register: reads as zero. */
    qemu_log_mask(LOG_UNIMP,
                  "axiado-hcp: eip197 read 0x%" HWADDR_PRIx "\n", addr);
    return 0;
}

static void axiado_eip197_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    AXIADOHCPState *s = AXIADO_HCP(opaque);
    uint32_t *slot;
    hwaddr off;
    bool is_rdr;
    int r;

    r = eip_xdr_ring(addr, &off, &is_rdr);
    if (r >= 0) {
        HCPxDR *x = is_rdr ? &s->rings[r].rdr : &s->rings[r].cdr;
        bool kick = eip_xdr_write(x, off, val);

        /*
         * A STAT write that cleared the threshold bit must immediately
         * deassert the level-triggered ring interrupt, so the line is
         * reconciled after any write to that register.
         */
        if (off == XDR_STAT) {
            hcp_update_ring_irq(s, r);
        }
        if (kick) {
            if (!is_rdr) {
                /* Transmit command descriptors were posted on this ring. */
                hcp_process_tx_ring(s, r);
            } else {
                /*
                 * Fresh receive buffers were posted, so retry anything that
                 * was back-pressured earlier.
                 */
                for (int i = 0; i < HCP_NUM_MACS; i++) {
                    NICState *nic = s->macs[i].nic;

                    if (nic) {
                        qemu_flush_queued_packets(qemu_get_queue(nic));
                    }
                }
            }
        }
        return;
    }

    r = eip_aic_r_ring(addr, &off);
    if (r >= 0) {
        HCPRing *ring = &s->rings[r];
        switch (off) {
        case EIP_AIC_R_CDR_ENABLE_CTRL:
            ring->aic_cdr_mask |= val;
            hcp_update_ring_irq(s, r);
            return;
        case EIP_AIC_R_CDR_ENABLE_CLR:
            ring->aic_cdr_mask &= ~val;
            hcp_update_ring_irq(s, r);
            return;
        case EIP_AIC_R_RDR_ENABLE_CTRL:
            ring->aic_rdr_mask |= val;
            hcp_update_ring_irq(s, r);
            return;
        case EIP_AIC_R_RDR_ENABLE_CLR:
            ring->aic_rdr_mask &= ~val;
            hcp_update_ring_irq(s, r);
            return;
        case EIP_AIC_R_CDR_ENABLED:
        case EIP_AIC_R_RDR_ENABLED:
            return;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "axiado-hcp: eip aic_r[%d] write 0x%" HWADDR_PRIx
                          " = 0x%" PRIx64 "\n", r, off, val);
            return;
        }
    }

    if (addr >= EIP_HIA_AIC_G_BASE && addr < EIP_HIA_AIC_G_BASE + 0x1000) {
        hwaddr g_off = addr - EIP_HIA_AIC_G_BASE;
        switch (g_off) {
        case EIP_AIC_G_ENABLE_CTRL:
            s->aic_g_mask |= val;
            return;
        case EIP_AIC_G_ENABLE_CLR:
            s->aic_g_mask &= ~val;
            return;
        case EIP_AIC_G_ENABLED:
            return;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "axiado-hcp: eip aic_g write 0x%" HWADDR_PRIx
                          " = 0x%" PRIx64 "\n", g_off, val);
            return;
        }
    }

    slot = eip_reg_slot(s, addr);
    if (slot) {
        *slot = val;

        if (addr == HCP_EIP_PE_BASE + PE_ICE_FPP_CTRL ||
            addr == HCP_EIP_PE_BASE + PE_ICE_PUE_CTRL) {
            hcp_ice_publish_fw_version(s, addr - HCP_EIP_PE_BASE);
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "axiado-hcp: eip197 write 0x%" HWADDR_PRIx " = 0x%" PRIx64
                  "\n", addr, val);
}

static const MemoryRegionOps eip197_ops = {
    .read = axiado_eip197_read,
    .write = axiado_eip197_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * PHY CSR region
 */

/*
 * PCS/SerDes configuration. The guest programs two lane blocks here, at
 * 0x30000 and 0x34000, and never reads any of it back: the link is always up
 * in this model, so none of the calibration or lock status it would poll for
 * is needed. Writes are therefore accepted and discarded, and reads return
 * zero. Both are logged, so a guest that does start reading shows up as
 * LOG_UNIMP output rather than as silent wrong behaviour.
 */
static uint64_t axiado_phy_csr_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP,
                  "axiado-hcp: phy csr read 0x%" HWADDR_PRIx "\n", addr);
    return 0;
}

static void axiado_phy_csr_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    qemu_log_mask(LOG_UNIMP,
                  "axiado-hcp: phy csr write 0x%" HWADDR_PRIx " = 0x%" PRIx64
                  "\n", addr, val);
}

static const MemoryRegionOps phy_csr_ops = {
    .read = axiado_phy_csr_read,
    .write = axiado_phy_csr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * NIC backends and receive datapath
 */

/*
 * 64-bit result descriptor plus EIP-96 output token, as the guest reads them
 * back with ownership words enabled and a host data width of zero:
 *
 *   word 0  : control word   (segment byte count | FIRST_SEG | LAST_SEG)
 *   word 1  : extended length (not read by the driver)
 *   word 2/3: destination packet address (lo/hi)  -> RES_DESC_DATA_LO/HI_OFF
 *   word 4..: EIP-96 output token
 *               token word 0, byte 16: packet byte count and error bits
 *               token word 2, byte 24: app_id << 9
 *   last word of the slot (stride-4): ownership word = 0xAAAAAAAA
 *
 * A packet is ready purely by virtue of the ownership word; the guest takes
 * the length and app_id from the output token, not from a fixed result
 * descriptor field.
 */
#define RES_TOKEN_WORD_OFF        4
/* Byte offsets of the length/error word and the app_id word in the token. */
#define RES_TOKEN_HDR_OFF         (RES_TOKEN_WORD_OFF * 4)
#define RES_TOKEN_APP_ID_OFF      ((RES_TOKEN_WORD_OFF + 2) * 4)
#define RES_TOKEN_LEN_MASK        0x1FFFFu
#define RES_OWNERSHIP_PATTERN     0xAAAAAAAAu

/*
 * Ring that carries received packets. Ring 0 is the default interface and
 * posts no receive buffers; rings 1 to 3 run in continuous-scatter mode,
 * where each result descriptor slot is pre-loaded with the address of a
 * guest receive buffer at offsets +8 and +12. Any of them will do, and all
 * five MACs are demultiplexed from the app_id, so one ring serves them all.
 */
#define HCP_RX_RING 1

/* Map a NetClientState back to the MAC index that owns it. */
static int hcp_find_mac_for_nc(AXIADOHCPState *s, NetClientState *nc)
{
    for (int i = 0; i < HCP_NUM_MACS; i++) {
        if (s->macs[i].nic && qemu_get_queue(s->macs[i].nic) == nc) {
            return i;
        }
    }
    return -1;
}

static bool hcp_rdr_has_slot(HCPxDR *rdr)
{
    uint32_t stride = xdr_stride_bytes(rdr);
    if (stride == 0 || rdr->size == 0 || rdr->base == 0) {
        return false;
    }
    if (rdr->size / stride == 0) {
        return false;
    }
    return rdr->prep_count > rdr->proc_count;
}

/*
 * Deliver one inbound packet through ring r's result descriptor ring.
 *
 *   1. Read the current slot. The guest pre-loaded it with the address of a
 *      receive buffer at +8 and +12.
 *   2. DMA-write the packet into that buffer. The buffer lives elsewhere in
 *      guest RAM, not in the descriptor.
 *   3. Rebuild the slot as a result descriptor plus output token, and stamp
 *      the ownership word last.
 *   4. Advance the receive cursor and latch the threshold interrupt.
 *
 * Returns false without consuming the packet if no slot is available, so the
 * caller can apply back-pressure.
 */
static bool hcp_rdr_deliver(AXIADOHCPState *s, int r, int mac_idx,
                            const uint8_t *buf, size_t len)
{
    HCPxDR *rdr = &s->rings[r].rdr;

    uint32_t rdr_stride = xdr_stride_bytes(rdr);
    if (rdr_stride == 0 || rdr->size == 0 || rdr->base == 0) {
        return false;
    }
    uint32_t rdr_descs = rdr->size / rdr_stride;
    if (rdr_descs == 0) {
        return false;
    }

    /*
     * Step 1: locate the slot and read the receive buffer the guest posted in
     * it. The command ring is the transmit ring and takes no part in receive.
     */
    uint32_t rdr_idx = s->rings[r].rx_idx % rdr_descs;
    hwaddr rdr_desc_addr = rdr->base + (hwaddr)rdr_idx * rdr_stride;

    uint8_t res_desc[256];
    size_t rdr_io = MIN(rdr_stride, (uint32_t)sizeof(res_desc));
    if (dma_memory_read(&address_space_memory, rdr_desc_addr, res_desc, rdr_io,
                        MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        return false;
    }

    uint32_t orig_own  = (rdr_io >= rdr_stride && rdr_stride >= 4)
                         ? ldl_le_p(&res_desc[rdr_stride - 4]) : 0;
    uint64_t buf_addr =
        (uint64_t)ldl_le_p(&res_desc[RES_DESC_DATA_LO_OFF])
        | ((uint64_t)ldl_le_p(&res_desc[RES_DESC_DATA_HI_OFF]) << 32);

    if (buf_addr == 0) {
        /* No buffer posted for this slot yet. */
        trace_axiado_hcp_rx_backpressure(r, rdr_idx);
        return false;
    }

    /*
     * Only fill a slot the guest has freshly prepared. An ownership word still
     * holding the pattern means this slot was filled earlier and has not been
     * consumed, so the ring is full: leave the packet queued rather than
     * overwrite a buffer that is still in use.
     */
    if (orig_own == RES_OWNERSHIP_PATTERN) {
        trace_axiado_hcp_rx_backpressure(r, rdr_idx);
        return false;
    }

    /* Step 2: DMA-write the packet into the guest's receive buffer. */
    if (dma_memory_write(&address_space_memory, buf_addr, buf, len,
                         MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        return false;
    }

    /*
     * Step 3: build the result descriptor and output token in this slot. The
     * slot is rebuilt from zero because the guest clears only the ownership
     * word, so stale token bytes would otherwise survive.
     */
    memset(res_desc, 0, rdr_io);

    /* word 0: control word — segment byte count + first/last segment */
    uint32_t ctrl = ((uint32_t)len & RES_DESC_PARTICLE_MASK)
                  | RES_DESC_FIRST_SEG_BIT
                  | RES_DESC_LAST_SEG_BIT;
    stl_le_p(&res_desc[RES_DESC_FLAGS_OFF], ctrl);

    /* words 2/3: destination packet address (64-bit device) */
    stl_le_p(&res_desc[RES_DESC_DATA_LO_OFF], (uint32_t)buf_addr);
    stl_le_p(&res_desc[RES_DESC_DATA_HI_OFF], (uint32_t)(buf_addr >> 32));

    /* EIP-96 output token: length with a zero error code, then app_id << 9. */
    if (rdr_io >= RES_TOKEN_APP_ID_OFF + 4) {
        stl_le_p(&res_desc[RES_TOKEN_HDR_OFF],
                 (uint32_t)len & RES_TOKEN_LEN_MASK);
        stl_le_p(&res_desc[RES_TOKEN_APP_ID_OFF],
                 ((uint32_t)s->macs[mac_idx].app_id & 0x7Fu) << 9);
    }

    /*
     * The ownership word in the last word of the slot is the packet-ready
     * handshake, so it is written after everything else the guest will read.
     */
    if (rdr_io >= rdr_stride && rdr_stride >= 4) {
        stl_le_p(&res_desc[rdr_stride - 4], RES_OWNERSHIP_PATTERN);
    }

    dma_memory_write(&address_space_memory, rdr_desc_addr, res_desc, rdr_io,
                     MEMTXATTRS_UNSPECIFIED);

    /*
     * Step 4: advance the receive cursor. The command ring counters belong to
     * the transmit path and must not move here.
     */
    s->rings[r].rx_idx++;
    rdr->proc_count += rdr_stride;
    rdr->proc_pntr  = (rdr->proc_pntr + rdr_stride) % rdr->size;

    trace_axiado_hcp_rx_packet(r, mac_idx, len);

    rdr->stat |= XDR_STAT_PROC_THRESH_IRQ;
    hcp_update_ring_irq(s, r);

    return true;
}

static bool axiado_nic_can_receive(NetClientState *nc)
{
    AXIADOHCPState *s = qemu_get_nic_opaque(nc);

    return hcp_rdr_has_slot(&s->rings[HCP_RX_RING].rdr);
}

static ssize_t axiado_nic_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    AXIADOHCPState *s = qemu_get_nic_opaque(nc);
    int mac_idx = hcp_find_mac_for_nc(s, nc);

    if (mac_idx < 0 || size == 0 || size > MAX_PKT_LEN) {
        return size;
    }

    if (!hcp_rdr_deliver(s, HCP_RX_RING, mac_idx, buf, size)) {
        /*
         * No slot is free. Returning zero keeps the packet queued so it is
         * redelivered from qemu_flush_queued_packets() once the guest posts
         * more buffers; returning size would silently drop it and stall the
         * guest's transport layer under load.
         */
        return 0;
    }
    return size;
}

static NetClientInfo axiado_nic_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = axiado_nic_can_receive,
    .receive = axiado_nic_receive,
};

/*
 * QOM lifecycle
 */

static void axiado_hcp_realize(DeviceState *dev, Error **errp)
{
    AXIADOHCPState *s = AXIADO_HCP(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio_eip197, OBJECT(s), &eip197_ops, s,
                          "axiado-hcp.eip197", HCP_EIP197_SIZE);
    sysbus_init_mmio(sbd, &s->mmio_eip197);

    memory_region_init_io(&s->mmio_shim, OBJECT(s), &shim_ops, s,
                          "axiado-hcp.shim", HCP_SHIM_SIZE);
    sysbus_init_mmio(sbd, &s->mmio_shim);

    memory_region_init_io(&s->mmio_phy_csr, OBJECT(s), &phy_csr_ops, s,
                          "axiado-hcp.phy-csr", HCP_PHY_CSR_SIZE);
    sysbus_init_mmio(sbd, &s->mmio_phy_csr);

    for (int i = 0; i < HCP_NUM_IRQS; i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }

    for (int i = 0; i < HCP_NUM_MACS; i++) {
        HCPMac *m = &s->macs[i];
        char nic_name[16];

        snprintf(nic_name, sizeof(nic_name), "axiado-mac%d", i);
        qemu_macaddr_default_if_unset(&m->conf.macaddr);
        m->nic = qemu_new_nic(&axiado_nic_info, &m->conf,
                              object_get_typename(OBJECT(dev)),
                              nic_name, &dev->mem_reentrancy_guard, s);
        qemu_format_nic_info_str(qemu_get_queue(m->nic), m->conf.macaddr.a);
    }
}

static void axiado_hcp_unrealize(DeviceState *dev)
{
    AXIADOHCPState *s = AXIADO_HCP(dev);

    for (int i = 0; i < HCP_NUM_MACS; i++) {
        if (s->macs[i].nic) {
            qemu_del_nic(s->macs[i].nic);
            s->macs[i].nic = NULL;
        }
    }
}

static void axiado_hcp_reset(DeviceState *dev)
{
    AXIADOHCPState *s = AXIADO_HCP(dev);
    static const uint8_t mac_app_ids[HCP_NUM_MACS] = { 5, 1, 2, 3, 4 };

    for (int i = 0; i < HCP_NUM_MACS; i++) {
        HCPMac *m = &s->macs[i];
        /* Preserve nic + conf across reset */
        NICState *saved_nic = m->nic;
        NICConf saved_conf = m->conf;

        memset(m, 0, sizeof(*m));
        m->nic = saved_nic;
        m->conf = saved_conf;
        m->app_id = mac_app_ids[i];
        m->mii_regs[MII_BMCR] = 0x1140;
        m->mii_regs[MII_BMSR] = 0x002d;
    }

    for (int i = 0; i < HCP_NUM_RINGS; i++) {
        memset(&s->rings[i], 0, sizeof(s->rings[i]));
    }
    s->aic_g_mask = 0;

    memset(s->flue_fht, 0, sizeof(s->flue_fht));
    memset(s->pe_regs, 0, sizeof(s->pe_regs));
    memset(s->cls_regs, 0, sizeof(s->cls_regs));
    memset(s->flow_regs, 0, sizeof(s->flow_regs));
    memset(s->cache_regs, 0, sizeof(s->cache_regs));
    memset(s->trc_ram, 0, sizeof(s->trc_ram));
}

static const VMStateDescription vmstate_axiado_hcp_mac = {
    .name = "axiado-hcp/mac",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(command_config, HCPMac),
        VMSTATE_UINT32(mac_addr_lo, HCPMac),
        VMSTATE_UINT32(mac_addr_hi, HCPMac),
        VMSTATE_UINT32(frm_length, HCPMac),
        VMSTATE_UINT32(rx_fifo_sections, HCPMac),
        VMSTATE_UINT32(tx_fifo_sections, HCPMac),
        VMSTATE_UINT32(if_mode, HCPMac),
        VMSTATE_UINT32(mdio_command, HCPMac),
        VMSTATE_UINT32(mdio_data, HCPMac),
        VMSTATE_UINT32(mdio_regaddr, HCPMac),
        VMSTATE_UINT16_ARRAY(mii_regs, HCPMac, 32),
        VMSTATE_UINT8(app_id, HCPMac),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_axiado_hcp_xdr = {
    .name = "axiado-hcp/xdr",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(base, HCPxDR),
        VMSTATE_UINT32(size, HCPxDR),
        VMSTATE_UINT32(desc_size, HCPxDR),
        VMSTATE_UINT32(cfg, HCPxDR),
        VMSTATE_UINT32(dma_cfg, HCPxDR),
        VMSTATE_UINT32(thresh, HCPxDR),
        VMSTATE_UINT32(prep_count, HCPxDR),
        VMSTATE_UINT32(proc_count, HCPxDR),
        VMSTATE_UINT32(prep_pntr, HCPxDR),
        VMSTATE_UINT32(proc_pntr, HCPxDR),
        VMSTATE_UINT32(stat, HCPxDR),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_axiado_hcp_ring = {
    .name = "axiado-hcp/ring",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(cdr, HCPRing, 1, vmstate_axiado_hcp_xdr, HCPxDR),
        VMSTATE_STRUCT(rdr, HCPRing, 1, vmstate_axiado_hcp_xdr, HCPxDR),
        VMSTATE_UINT32(aic_cdr_mask, HCPRing),
        VMSTATE_UINT32(aic_rdr_mask, HCPRing),
        VMSTATE_UINT32(rx_idx, HCPRing),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_axiado_hcp = {
    .name = TYPE_AXIADO_HCP,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(macs, AXIADOHCPState, HCP_NUM_MACS, 1,
                             vmstate_axiado_hcp_mac, HCPMac),
        VMSTATE_STRUCT_ARRAY(rings, AXIADOHCPState, HCP_NUM_RINGS, 1,
                             vmstate_axiado_hcp_ring, HCPRing),
        VMSTATE_UINT32(aic_g_mask, AXIADOHCPState),
        VMSTATE_UINT32_ARRAY(flue_fht, AXIADOHCPState,
                             HCP_EIP_FLUE_FHT_REGS),
        VMSTATE_UINT32_ARRAY(pe_regs, AXIADOHCPState, HCP_EIP_PE_REGS),
        VMSTATE_UINT32_ARRAY(cls_regs, AXIADOHCPState, HCP_EIP_CLS_REGS),
        VMSTATE_UINT32_ARRAY(flow_regs, AXIADOHCPState, HCP_EIP_FLOW_REGS),
        VMSTATE_UINT32_ARRAY(cache_regs, AXIADOHCPState, HCP_EIP_CACHE_REGS),
        VMSTATE_UINT32_ARRAY(trc_ram, AXIADOHCPState, HCP_EIP_TRC_RAM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static const Property axiado_hcp_properties[] = {
    DEFINE_PROP_MACADDR("mac0", AXIADOHCPState, macs[0].conf.macaddr),
    DEFINE_PROP_MACADDR("mac1", AXIADOHCPState, macs[1].conf.macaddr),
    DEFINE_PROP_MACADDR("mac2", AXIADOHCPState, macs[2].conf.macaddr),
    DEFINE_PROP_MACADDR("mac3", AXIADOHCPState, macs[3].conf.macaddr),
    DEFINE_PROP_MACADDR("mac4", AXIADOHCPState, macs[4].conf.macaddr),
    DEFINE_PROP_NETDEV("netdev0", AXIADOHCPState, macs[0].conf.peers),
    DEFINE_PROP_NETDEV("netdev1", AXIADOHCPState, macs[1].conf.peers),
    DEFINE_PROP_NETDEV("netdev2", AXIADOHCPState, macs[2].conf.peers),
    DEFINE_PROP_NETDEV("netdev3", AXIADOHCPState, macs[3].conf.peers),
    DEFINE_PROP_NETDEV("netdev4", AXIADOHCPState, macs[4].conf.peers),
};

static void axiado_hcp_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Axiado HCP network complex";
    dc->realize = axiado_hcp_realize;
    dc->unrealize = axiado_hcp_unrealize;
    dc->vmsd = &vmstate_axiado_hcp;
    device_class_set_legacy_reset(dc, axiado_hcp_reset);
    device_class_set_props(dc, axiado_hcp_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo axiado_hcp_info = {
    .name          = TYPE_AXIADO_HCP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AXIADOHCPState),
    .class_init    = axiado_hcp_class_init,
};

static void axiado_hcp_register_types(void)
{
    type_register_static(&axiado_hcp_info);
}
type_init(axiado_hcp_register_types)
