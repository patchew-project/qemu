/*
 * Axiado HCP (Header & Crypto Processing) block - QEMU device model
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026 Ratan Lal Dondi <rdondi@axiado.com>
 */

#ifndef HW_NET_AXIADO_HCP_H
#define HW_NET_AXIADO_HCP_H

#include "hw/core/sysbus.h"
#include "net/net.h"

#define TYPE_AXIADO_HCP "axiado-hcp"
OBJECT_DECLARE_SIMPLE_TYPE(AXIADOHCPState, AXIADO_HCP)

/* MMIO region sizes, matching the SoC base map */
#define HCP_EIP197_SIZE    0x100000   /* EIP-197 registers and ring control */
#define HCP_SHIM_SIZE      0x4000     /* MAC config, MDIO and statistics */
#define HCP_PHY_CSR_SIZE   0x50000    /* PCS / PHY CSR */

/*
 * IRQ topology:
 *   index 0       = EIP-197 global
 *   indices 1..4  = ring interfaces
 *   indices 5..9  = per-MAC IRQs, PHY and link events only
 */
#define HCP_NUM_RINGS      4
#define HCP_NUM_MACS       5
#define HCP_IRQ_EIP197     0
#define HCP_IRQ_RING_BASE  1
#define HCP_IRQ_MAC_BASE   (HCP_IRQ_RING_BASE + HCP_NUM_RINGS)
#define HCP_NUM_IRQS       (HCP_IRQ_MAC_BASE + HCP_NUM_MACS)

/* MAC block layout within the SHIM region */
#define HCP_SHIM_MAC_BASE_OFFSET   0x400
#define HCP_SHIM_MAC_STRIDE        0x400

/* Per-MAC state. Five instances per device: one 10G XGMII and four 1G SGMII. */
typedef struct HCPMac {
    /* MAC configuration registers */
    uint32_t command_config;     /* R_COMMAND_CONFIG 0x008 */
    uint32_t mac_addr_lo;        /* R_MAC_0          0x00c */
    uint32_t mac_addr_hi;        /* R_MAC_1          0x010 */
    uint32_t frm_length;         /* R_FRM_LENGTH     0x014 */
    uint32_t rx_fifo_sections;   /* R_RX_FIFO_SECT   0x01c */
    uint32_t tx_fifo_sections;   /* R_TX_FIFO_SECT   0x020 */
    uint32_t if_mode;            /* R_IF_MODE        0x300 */

    /* Per-MAC MDIO controller (talks to this MAC's virtual PHY) */
    uint32_t mdio_command;       /* R_MDIO_COMMAND   0x034 */
    uint32_t mdio_data;          /* R_MDIO_DATA      0x038 */
    uint32_t mdio_regaddr;       /* R_MDIO_REGADDR   0x03c (Clause 45) */

    /* Virtual PHY: Clause 22 register file accessed via per-MAC MDIO */
    uint16_t mii_regs[32];

    /* Axiado IOTOKEN routing tag. MAC 0 (10G) = 5; MAC 1..4 (1G) = 1..4. */
    uint8_t app_id;

    /* QEMU NIC backend for this MAC */
    NICConf conf;
    NICState *nic;
} HCPMac;

/*
 * SafeXcel xDR — Command/Result Descriptor Ring control registers.
 * One instance is a CDR; another (same layout) is the paired RDR.
 */
typedef struct HCPxDR {
    uint64_t base;        /* +0x00 BASE_LO / +0x04 BASE_HI */
    uint32_t size;        /* +0x18 RING_SIZE (bytes) */
    uint32_t desc_size;   /* +0x1c DESC_SIZE */
    uint32_t cfg;         /* +0x20 CFG */
    uint32_t dma_cfg;     /* +0x24 DMA_CFG */
    uint32_t thresh;      /* +0x28 THRESH */
    /*
     * Byte cursors, not register images. Descriptors prepared by the guest
     * and descriptors consumed by the engine; the difference is the fill
     * level reported when +0x2c is read.
     */
    uint32_t prep_count;  /* +0x2c PREP_COUNT */
    uint32_t proc_count;  /* +0x30 PROC_COUNT */
    uint32_t prep_pntr;   /* +0x34 PREP_PNTR */
    uint32_t proc_pntr;   /* +0x38 PROC_PNTR */
    uint32_t stat;        /* +0x3c STAT (latched IRQ; bit 4 = THRESH IRQ) */
} HCPxDR;

/*
 * Backed register windows inside the EIP-197 region, as base address and
 * count of 32-bit registers. Each covers a block the guest writes during
 * setup and reads back later:
 *
 *   FLUE_FHT  flow look-up engine, flow hash table configuration
 *   PE        packet engine: buffer thresholds, ICE scratch RAM and control,
 *             and the EIP-96 token, context and seed registers
 *   CLS       classification engines, three instances at a 0x800 stride
 *   FLOW      flow control
 *   CACHE     record cache control
 *   TRC_RAM   classification record cache, actual on-chip RAM
 *
 * The ring status window at 0xffb00 is deliberately absent: the guest only
 * ever reads it, so it needs no storage and reads as zero.
 */
#define HCP_EIP_FLUE_FHT_BASE   0x00000
#define HCP_EIP_FLUE_FHT_REGS   8
#define HCP_EIP_PE_BASE         0xa0000
#define HCP_EIP_PE_REGS         2048
#define HCP_EIP_TRC_RAM_BASE    0xe0000
#define HCP_EIP_TRC_RAM_REGS    16384
#define HCP_EIP_CLS_BASE        0xf0000
#define HCP_EIP_CLS_REGS        2048
#define HCP_EIP_FLOW_BASE       0xf6000
#define HCP_EIP_FLOW_REGS       1024
#define HCP_EIP_CACHE_BASE      0xf7000
#define HCP_EIP_CACHE_REGS      1024

/* Per-ring state: a CDR/RDR pair plus its interrupt enable masks. */
typedef struct HCPRing {
    HCPxDR cdr;
    HCPxDR rdr;
    uint32_t aic_cdr_mask;
    uint32_t aic_rdr_mask;

    /*
     * Receive slot cursor, in descriptors. The guest's own read pointer is a
     * plain counter starting at zero and advancing one descriptor per packet,
     * so the byte counters above cannot drive the slot index: they carry
     * clear-bit and word-shift encodings from the register writes. Keeping a
     * separate cursor lines slot zero up with the guest's first read.
     */
    uint32_t rx_idx;
} HCPRing;

struct AXIADOHCPState {
    SysBusDevice parent_obj;

    MemoryRegion mmio_eip197;
    MemoryRegion mmio_shim;
    MemoryRegion mmio_phy_csr;

    qemu_irq irqs[HCP_NUM_IRQS];

    HCPMac macs[HCP_NUM_MACS];
    HCPRing rings[HCP_NUM_RINGS];

    /* Global AIC enable mask (per-ring bits) */
    uint32_t aic_g_mask;

    /*
     * Register windows within the EIP-197 region that hold guest-written
     * values. The engine's behaviour does not depend on them - the datapath
     * is driven entirely by the descriptor rings - but the guest writes them
     * during setup and reads them back, so the values have to persist.
     * Anything outside these windows reads as zero.
     */
    uint32_t flue_fht[HCP_EIP_FLUE_FHT_REGS];
    uint32_t pe_regs[HCP_EIP_PE_REGS];
    uint32_t cls_regs[HCP_EIP_CLS_REGS];
    uint32_t flow_regs[HCP_EIP_FLOW_REGS];
    uint32_t cache_regs[HCP_EIP_CACHE_REGS];

    /*
     * Classification record cache. Real on-chip RAM: at init the guest walks
     * it writing each record's own index, then reads the records back to
     * discover how large the cache is.
     */
    uint32_t trc_ram[HCP_EIP_TRC_RAM_REGS];
};

#endif /* HW_NET_AXIADO_HCP_H */
