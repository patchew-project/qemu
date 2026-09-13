/*
 * Renesas RX65N Ethernet Controller (ETHERC) + Ethernet DMA Controller (EDMAC)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.1.00 R01UH0590EJ0100), Sections 32–33
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/irq.h"
#include "hw/net/renesas_etherc.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "system/dma.h"

/*
 * Register offsets within one channel's 0x200-byte MMIO window.
 * EDMAC occupies offsets 0x000-0x0ff; ETHERC occupies 0x100-0x1ff.
 * RX65N/RX72M/RX72N channel 0 starts at 0x000c0000. RX72M/RX72N
 * channel 1 starts at 0x000c0200 and must use a separate device instance.
 * References: RX65N hardware manual sections 34/35; RX72M/RX72N 36/38.
 */
/* ETHERC */
#define R_ECMR      0x100
#define R_RFLR      0x108
#define R_ECSR      0x110
#define R_ECSIPR    0x118
#define R_PIR       0x120
#define R_PSR       0x128
#define R_RDMLR     0x140
#define R_IPGR      0x150
#define R_APR       0x154
#define R_MPR       0x158
#define R_RFCF      0x160
#define R_TPAUSER   0x164
#define R_TPAUSECR  0x168
#define R_BCFRR     0x16C
#define R_MAHR      0x1C0
#define R_MALR      0x1C8
#define R_TROCR     0x1D0
#define R_CDCR      0x1D4
#define R_LCCR      0x1D8
#define R_CNDCR     0x1DC
#define R_CEFCR     0x1E4
#define R_FRECR     0x1E8
#define R_TSFRCR    0x1EC
#define R_TLFRCR    0x1F0
#define R_RFCR      0x1F4
#define R_MAFCR     0x1F8
/* EDMAC */
#define R_EDMR      0x000
#define R_EDTRR     0x008
#define R_EDRRR     0x010
#define R_TDLAR     0x018
#define R_RDLAR     0x020
#define R_EESR      0x028
#define R_EESIPR    0x030
#define R_TRSCER    0x038
#define R_RMFCR     0x040
#define R_TFTR      0x048
#define R_FDR       0x050
#define R_RMCR      0x058
#define R_TFUCR     0x064
#define R_RFOCR     0x068
#define R_IOSR      0x06C
#define R_FCFTR     0x070
#define R_RPADIR    0x078
#define R_TRIMD     0x07C
#define R_RBWAR     0x0C8
#define R_RDFAR     0x0CC
#define R_TBRAR     0x0D4
#define R_TDFAR     0x0D8

/* ECMR bits */
#define ECMR_PRM    (1u << 0)   /* promiscuous */
#define ECMR_DM     (1u << 1)   /* full duplex */
#define ECMR_TE     (1u << 5)   /* transmit enable */
#define ECMR_RE     (1u << 6)   /* receive enable */

/* PIR bits */
#define PIR_MDC     (1u << 0)
#define PIR_MDO     (1u << 2)
#define PIR_MMD     (1u << 1)   /* direction: 1=SoC drives MDO */
#define PIR_MDI     (1u << 3)   /* MDI input (read-only) */

/* EESR / EESIPR bits */
#define EESR_CERF   (1u << 0)
#define EESR_RMAF   (1u << 7)
#define EESR_RFOF   (1u << 21)
#define EESR_RDE    (1u << 22)
#define EESR_FR     (1u << 23)
#define EESR_TFUF   (1u << 25)
#define EESR_TDE    (1u << 26)
#define EESR_TC     (1u << 27)
#define EESR_TWB    (1u << 31)

/* TX descriptor word 0 (TD0) bits */
#define TD0_TACT    (1u << 31)
#define TD0_TDLE    (1u << 30)
#define TD0_TFP1    (1u << 29)
#define TD0_TFP0    (1u << 28)
#define TD0_TFE     (1u << 27)
#define TD0_TWBI    (1u << 26)
#define TD0_TBL     0x0000FFFFu

/* RX descriptor word 0 (RD0) bits */
#define RD0_RACT    (1u << 31)
#define RD0_RDLE    (1u << 30)
#define RD0_RFP1    (1u << 29)
#define RD0_RFP0    (1u << 28)
#define RD0_RBL     0x0000FFFFu

/* Descriptor size (EDMR.DL = 00 → 4 words = 16 bytes) */
#define DESC_SIZE   16

/*
 * PHY simulation
 * Emulates a generic 100BASE-TX PHY at addresses 0 and 1, always link-up.
 */
static uint16_t mdio_phy_read(RX65NEthercState *s, int phy, int reg)
{
    /* Accept both PHY station addresses used by RX firmware variants. */
    if (phy != 0 && phy != 1) {
        return 0xFFFF;
    }
    switch (reg) {
    case 0:
        /*Basic Control: FD, 100Mbps */
        return 0x3100;
    case 1:
        /*Basic Status: link up, AN complete */
        return 0x786D;
    case 2:
        /*PHY ID 1 */
        return 0x0022;
    case 3:
        /*PHY ID 2 */
        return 0x1550;
    case 4:
        /*ANAR */
        return 0x05E1;
    case 5:
        /*ANLPAR */
        return 0x45E1;
    default: return s->mdio.phy_regs[reg < 32 ? reg : 0];
    }
}

static void mdio_phy_write(RX65NEthercState *s, int phy, int reg, uint16_t val)
{
    if ((phy == 0 || phy == 1) && reg < 32) {
        s->mdio.phy_regs[reg] = val;
    }
}

/*
 * MDIO bit-bang state machine.
 *
 * Invoked on every PIR write that has a MDC rising edge.
 * Accumulates the MDIO frame header (SOF + OP + PHYADDR + REGADDR = 13
 * master bits after the preamble-terminating 0), decodes read/write,
 * and then either: schedules slave-driven MDI bits for reads, or
 * accumulates the TA + DATA bits for writes.
 *
 * MDI is updated in pir immediately so the firmware can read it back.
 */
static void mdio_do_rising(RX65NEthercState *s, bool mdo, bool mden)
{
    MDIOState *m = &s->mdio;

    /* ---- Slave output phase (read response) ---- */
    if (m->out_cnt >= 0) {
        /*
         * out_cnt 0  → TA[1]: floating pull-up (MDI=1)
         * out_cnt 1  → TA[0]: slave drives 0    (MDI=0)
         * out_cnt 2–17 → DATA[15:0] MSB-first
         */
        if (m->out_cnt == 0) {
            m->mdi = true;
        } else if (m->out_cnt == 1) {
            m->mdi = false;
        } else {
            int bit = 17 - m->out_cnt;           /* 15 down to 0 */
            m->mdi = (m->read_data >> bit) & 1;
        }
        m->out_cnt++;
        if (m->out_cnt >= 18) {
            m->out_cnt = -1;
            m->in_cnt  = -1;
            m->preamble = 0;
        }
        return;
    }

    if (!mden) {
        return;    /* master not driving; ignore (should be in output phase) */
    }

    /* ---- Preamble phase ---- */
    if (m->in_cnt == -1) {
        if (mdo) {
            m->preamble++;
        } else if (m->preamble >= 32) {
            /* SOF[0] = 0 detected, start header accumulation */
            m->in_cnt  = 0;
            m->sr      = 0;
            m->write_cnt = -1;
        } else {
            m->preamble = 0;
        }
        return;
    }

    /* ---- Header accumulation (13 bits after SOF[0]) ---- */
    if (m->in_cnt < 13) {
        m->sr = (m->sr << 1) | (mdo ? 1 : 0);
        m->in_cnt++;

        if (m->in_cnt == 13) {
            /*
             * sr[12] = SOF[1]
             * sr[11:10] = OP  (10=read, 01=write)
             * sr[9:5]   = PHYADDR
             * sr[4:0]   = REGADDR
             */
            int sof1  = (m->sr >> 12) & 1;
            int op    = (m->sr >> 10) & 3;
            int paddr = (m->sr >> 5) & 0x1F;
            int raddr = m->sr & 0x1F;

            if (sof1 != 1) {
                m->in_cnt = -1;
                m->preamble = 0;
                return;
            }
            if (op == 2) {
                m->is_read = true;
                m->read_data = mdio_phy_read(s, paddr, raddr);
                /* Slave output starts on the next rising edge. */
                m->out_cnt = 0;
            } else if (op == 1) {
                m->is_read = false;
                m->write_paddr = paddr;
                m->write_raddr = raddr;
                m->write_data = 0;
                m->write_cnt = 0;
            }
        }
        return;
    }

    /* ---- Write: TA (2) + DATA (16) = 18 master bits ---- */
    if (!m->is_read && m->write_cnt >= 0 && m->write_cnt < 18) {
        if (m->write_cnt >= 2) {
            m->write_data = (m->write_data << 1) | (mdo ? 1 : 0);
        }
        m->write_cnt++;
        if (m->write_cnt == 18) {
            mdio_phy_write(s, m->write_paddr, m->write_raddr, m->write_data);
            m->in_cnt   = -1;
            m->write_cnt = -1;
            m->preamble = 0;
        }
    }
}

static void etherc_pir_write(RX65NEthercState *s, uint32_t val)
{
    bool old_mdc = !!(s->pir & PIR_MDC);
    bool new_mdc = !!(val & PIR_MDC);
    bool new_mden = !!(val & PIR_MMD);
    bool new_mdo  = !!(val & PIR_MDO);

    /* Update writable bits, keep MDI */
    s->pir = (val & ~PIR_MDI) | (s->pir & PIR_MDI);

    if (!old_mdc && new_mdc) {
        /* Rising edge: run state machine and update MDI */
        mdio_do_rising(s, new_mdo, new_mden);
        s->pir = (s->pir & ~PIR_MDI) | (s->mdio.mdi ? PIR_MDI : 0);
    }
}

/*
 * Interrupt management
 */
static void etherc_update_irq(RX65NEthercState *s)
{
    qemu_set_irq(s->irq[ETHERC_IRQ_EINT], !!(s->eesr & s->eesipr));
}

/*
 * TX path: walk descriptor ring, send frames, update EESR.
 */
static void etherc_tx_run(RX65NEthercState *s)
{
    bool did_tx = false;

    for (;;) {
        uint32_t td0, td1;
        dma_memory_read(&address_space_memory, s->tx_cur,
                        &td0, 4, MEMTXATTRS_UNSPECIFIED);
        dma_memory_read(&address_space_memory, s->tx_cur + 4,
                        &td1, 4, MEMTXATTRS_UNSPECIFIED);
        td0 = le32_to_cpu(td0);
        td1 = le32_to_cpu(td1);

        if (!(td0 & TD0_TACT)) {
            /* CPU owns descriptor: TX descriptor list exhausted */
            s->eesr |= EESR_TDE;
            break;
        }

        uint32_t buf_addr = td1;
        uint32_t buf_len  = td0 & TD0_TBL;

        if (buf_len > 0 && (s->ecmr & ECMR_TE)) {
            uint8_t *buf = g_malloc(buf_len);
            dma_memory_read(&address_space_memory, buf_addr,
                            buf, buf_len, MEMTXATTRS_UNSPECIFIED);
            qemu_send_packet(qemu_get_queue(s->nic), buf, buf_len);
            g_free(buf);
        }

        /* Return descriptor to CPU: clear TACT, mark SOF+EOF */
        td0 &= ~TD0_TACT;
        td0 |= TD0_TFP1 | TD0_TFP0;
        uint32_t w = cpu_to_le32(td0);
        dma_memory_write(&address_space_memory, s->tx_cur,
                         &w, 4, MEMTXATTRS_UNSPECIFIED);

        s->eesr |= EESR_TC;
        if (td0 & TD0_TWBI) {
            s->eesr |= EESR_TWB;
        }
        did_tx = true;

        if (td0 & TD0_TDLE) {
            s->tx_cur = s->tdlar;
        } else {
            s->tx_cur += DESC_SIZE;
        }
    }

    if (did_tx) {
        s->edtrr &= ~0x1u;
        etherc_update_irq(s);
    }
}

/*
 * RX path: called by QEMU net layer on packet arrival.
 */
static bool etherc_can_receive(NetClientState *nc)
{
    RX65NEthercState *s = qemu_get_nic_opaque(nc);
    return !!(s->edrrr & 0x1) && !!(s->ecmr & ECMR_RE);
}

static ssize_t etherc_receive(NetClientState *nc, const uint8_t *buf,
                              size_t len)
{
    RX65NEthercState *s = qemu_get_nic_opaque(nc);

    if (!(s->edrrr & 0x1) || !(s->ecmr & ECMR_RE)) {
        return -1;
    }

    /* Walk descriptor ring to find an EDMAC-owned (RACT=1) entry */
    uint32_t start = s->rx_cur;
    for (;;) {
        uint32_t rd0, rd1;
        dma_memory_read(&address_space_memory, s->rx_cur,
                        &rd0, 4, MEMTXATTRS_UNSPECIFIED);
        dma_memory_read(&address_space_memory, s->rx_cur + 4,
                        &rd1, 4, MEMTXATTRS_UNSPECIFIED);
        rd0 = le32_to_cpu(rd0);
        rd1 = le32_to_cpu(rd1);

        if (rd0 & RD0_RACT) {
            break;   /* found a free descriptor */
        }

        /* Descriptor is CPU-owned: advance */
        bool last = !!(rd0 & RD0_RDLE);
        s->rx_cur = last ? s->rdlar : s->rx_cur + DESC_SIZE;

        if (s->rx_cur == start) {
            /* Wrapped all the way around: no free descriptors */
            s->eesr  |= EESR_RDE;
            s->rmfcr += 1;
            etherc_update_irq(s);
            return -1;
        }
    }

    /* Read the free descriptor */
    uint32_t rd0, rd1;
    dma_memory_read(&address_space_memory, s->rx_cur,
                    &rd0, 4, MEMTXATTRS_UNSPECIFIED);
    dma_memory_read(&address_space_memory, s->rx_cur + 4,
                    &rd1, 4, MEMTXATTRS_UNSPECIFIED);
    rd0 = le32_to_cpu(rd0);
    rd1 = le32_to_cpu(rd1);

    uint32_t buf_addr = rd1;
    uint32_t buf_len  = rd0 & RD0_RBL;
    size_t   copy_len = MIN(len, buf_len);

    dma_memory_write(&address_space_memory, buf_addr,
                     buf, copy_len, MEMTXATTRS_UNSPECIFIED);

    /* Write back descriptor: clear RACT, mark SOF+EOF, write RFL in RD2 */
    rd0 &= ~RD0_RACT;
    rd0 |= RD0_RFP1 | RD0_RFP0;

    uint32_t w0 = cpu_to_le32(rd0);
    uint32_t w2 = cpu_to_le32((uint32_t)copy_len << 16);
    dma_memory_write(&address_space_memory, s->rx_cur,
                     &w0, 4, MEMTXATTRS_UNSPECIFIED);
    dma_memory_write(&address_space_memory, s->rx_cur + 8,
                     &w2, 4, MEMTXATTRS_UNSPECIFIED);

    s->eesr |= EESR_FR;
    if (buf[0] & 0x01) {
        s->eesr |= EESR_RMAF;
    }

    if (rd0 & RD0_RDLE) {
        s->rx_cur = s->rdlar;
    } else {
        s->rx_cur += DESC_SIZE;
    }

    etherc_update_irq(s);
    return (ssize_t)len;
}

/*
 * Filtering: honour promiscuous mode and MAC address.
 */
static bool etherc_accept_frame(RX65NEthercState *s, const uint8_t *buf)
{
    uint8_t mac[6];

    if (s->ecmr & ECMR_PRM) {
        return true;
    }
    /* Build our 6-byte MAC from MAHR/MALR */
    mac[0] = (s->mahr >> 24) & 0xFF;
    mac[1] = (s->mahr >> 16) & 0xFF;
    mac[2] = (s->mahr >>  8) & 0xFF;
    mac[3] = s->mahr & 0xFF;
    mac[4] = (s->malr >> 24) & 0xFF;
    mac[5] = (s->malr >> 16) & 0xFF;

    if (buf[0] & 0x01) {
        return true;   /* multicast / broadcast */
    }
    return memcmp(buf, mac, 6) == 0;
}

static ssize_t etherc_receive_filter(NetClientState *nc,
                                     const uint8_t *buf, size_t len)
{
    RX65NEthercState *s = qemu_get_nic_opaque(nc);
    if (!etherc_accept_frame(s, buf)) {
        return len;    /* silently discard */
    }
    return etherc_receive(nc, buf, len);
}

/*
 * EDMAC software reset
 */
static void etherc_sw_reset(RX65NEthercState *s)
{
    s->eesr   = 0;
    s->edtrr  = 0;
    s->edrrr  = 0;
    s->tx_cur = s->tdlar;
    s->rx_cur = s->rdlar;
    etherc_update_irq(s);
}

/*
 * MMIO read
 */
static uint64_t etherc_read(void *opaque, hwaddr addr, unsigned size)
{
    RX65NEthercState *s = opaque;

    switch (addr) {
    /* ETHERC */
    case R_ECMR:
        return s->ecmr;
    case R_RFLR:
        return s->rflr;
    case R_ECSR:
        return s->ecsr;
    case R_ECSIPR:
        return s->ecsipr;
    case R_PIR:
        return s->pir;
    case R_PSR:
        /*link up */
        return 0x0001;
    case R_RDMLR:
        return s->rdmlr;
    case R_IPGR:
        return s->ipgr;
    case R_APR:
        return s->apr;
    case R_MPR:
        return s->mpr;
    case R_RFCF:
        return 0;
    case R_TPAUSER:
        return s->tpauser;
    case R_TPAUSECR:return 0;
    case R_BCFRR:
        return s->bcfrr;
    case R_MAHR:
        return s->mahr;
    case R_MALR:
        return s->malr;
    case R_TROCR:
        return s->trocr;
    case R_CDCR:
        return s->cdcr;
    case R_LCCR:
        return s->lccr;
    case R_CNDCR:
        return s->cndcr;
    case R_CEFCR:
        return s->cefcr;
    case R_FRECR:
        return s->frecr;
    case R_TSFRCR:
        return s->tsfrcr;
    case R_TLFRCR:
        return s->tlfrcr;
    case R_RFCR:
        return s->rfcr;
    case R_MAFCR:
        return s->mafcr;
    /* EDMAC */
    case R_EDMR:
        return s->edmr;
    case R_EDTRR:
        return s->edtrr;
    case R_EDRRR:
        return s->edrrr;
    case R_TDLAR:
        return s->tdlar;
    case R_RDLAR:
        return s->rdlar;
    case R_EESR:
        return s->eesr;
    case R_EESIPR:
        return s->eesipr;
    case R_TRSCER:
        return s->trscer;
    case R_RMFCR:
        return s->rmfcr;
    case R_TFTR:
        return s->tftr;
    case R_FDR:
        return s->fdr;
    case R_RMCR:
        return s->rmcr;
    case R_TFUCR:
        return s->tfucr;
    case R_RFOCR:
        return s->rfocr;
    case R_FCFTR:
        return s->fcftr;
    case R_RPADIR:
        return s->rpadir;
    case R_TRIMD:
        return s->trimd;
    case R_RBWAR:
        return s->rx_cur;
    case R_RDFAR:
        return s->rx_cur;
    case R_TBRAR:
        return s->tx_cur;
    case R_TDFAR:
        return s->tx_cur;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "renesas_etherc: read 0x%" HWADDR_PRIX " unimplemented\n",
                      addr);
        return UINT64_MAX;
    }
}

/*
 * MMIO write
 */
static void etherc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RX65NEthercState *s = opaque;

    switch (addr) {
    /* ETHERC */
    case R_ECMR:
        s->ecmr   = val;
        break;
    case R_RFLR:
        s->rflr   = val;
        break;
    case R_ECSR:
        /*W0C */
        s->ecsr  &= ~val;
        break;
    case R_ECSIPR:
        s->ecsipr = val;
        break;
    case R_PIR:
        etherc_pir_write(s, val);
        break;
    case R_RDMLR:
        s->rdmlr  = val;
        break;
    case R_IPGR:
        s->ipgr   = val;
        break;
    case R_APR:
        s->apr    = val;
        break;
    case R_MPR:
        s->mpr    = val;
        break;
    case R_TPAUSER:
        s->tpauser = val;
        break;
    case R_BCFRR:
        s->bcfrr  = val;
        break;
    case R_MAHR:
        s->mahr   = val;
        break;
    case R_MALR:
        s->malr   = val;
        break;
    case R_TROCR:
        /*write clears counter */
        s->trocr  = 0;
        break;
    case R_CDCR:
        s->cdcr   = 0;
        break;
    case R_LCCR:
        s->lccr   = 0;
        break;
    case R_CNDCR:
        s->cndcr  = 0;
        break;
    case R_CEFCR:
        s->cefcr  = 0;
        break;
    case R_FRECR:
        s->frecr  = 0;
        break;
    case R_TSFRCR:
        s->tsfrcr = 0;
        break;
    case R_TLFRCR:
        s->tlfrcr = 0;
        break;
    case R_RFCR:
        s->rfcr   = 0;
        break;
    case R_MAFCR:
        s->mafcr  = 0;
        break;

    /* EDMAC */
    case R_EDMR:
        s->edmr = val;
        if (val & 0x1) {   /* SWR: software reset */
            etherc_sw_reset(s);
            s->edmr &= ~0x1u;
        }
        break;
    case R_EDTRR:
        s->edtrr = val;
        if (val & 0x1) {
            s->tx_cur = s->tdlar;
            etherc_tx_run(s);
        }
        break;
    case R_EDRRR:
        s->edrrr = val;
        if (val & 0x1) {
            s->rx_cur = s->rdlar;
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;
    case R_TDLAR:
        s->tdlar  = val;
        s->tx_cur = val;
        break;
    case R_RDLAR:
        s->rdlar  = val;
        s->rx_cur = val;
        break;
    case R_EESR:
        /*W0C */
        s->eesr  &= ~val;
        etherc_update_irq(s);
        break;
    case R_EESIPR:
        s->eesipr = val;
        etherc_update_irq(s);
        break;
    case R_TRSCER:
        s->trscer = val;
        break;
    case R_RMFCR:
        s->rmfcr  = 0;
        break;
    case R_TFTR:
        s->tftr   = val;
        break;
    case R_FDR:
        s->fdr    = val;
        break;
    case R_RMCR:
        s->rmcr   = val;
        break;
    case R_TFUCR:
        s->tfucr  = 0;
        break;
    case R_RFOCR:
        s->rfocr  = 0;
        break;
    case R_FCFTR:
        s->fcftr  = val;
        break;
    case R_RPADIR:
        s->rpadir = val;
        break;
    case R_TRIMD:
        s->trimd  = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_etherc: write 0x%" HWADDR_PRIX
                      " unimplemented\n",
                      addr);
        break;
    }
}

static const MemoryRegionOps etherc_ops = {
    .read  = etherc_read,
    .write = etherc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

/*
 * NIC callbacks
 */
static NetClientInfo etherc_net_info = {
    .type         = NET_CLIENT_DRIVER_NIC,
    .size         = sizeof(NICState),
    .can_receive  = etherc_can_receive,
    .receive      = etherc_receive_filter,
};

/*
 * Device lifecycle
 */
static void etherc_reset(DeviceState *dev)
{
    RX65NEthercState *s = RENESAS_ETHERC(dev);

    s->ecmr    = 0;
    s->rflr    = 0x05EE;   /* 1518 bytes max frame length (reset value) */
    s->ecsr    = 0;
    s->ecsipr  = 0;
    s->pir     = 0;
    s->rdmlr   = 0;
    s->ipgr    = 0x0014;
    s->apr     = 0x0000FFFF;
    s->mpr     = 0;
    s->tpauser = 0;
    s->bcfrr   = 0;
    s->mahr    = 0;
    s->malr    = 0;
    s->trocr   = 0; s->cdcr  = 0; s->lccr  = 0; s->cndcr = 0;
    s->cefcr   = 0; s->frecr = 0; s->tsfrcr = 0; s->tlfrcr = 0;
    s->rfcr    = 0; s->mafcr = 0;

    s->edmr    = 0;
    s->edtrr   = 0;
    s->edrrr   = 0;
    s->tdlar   = 0;
    s->rdlar   = 0;
    s->eesr    = 0;
    s->eesipr  = 0;
    s->trscer  = 0;
    s->rmfcr   = 0;
    s->tftr    = 0;
    s->fdr     = 0x0707;
    s->rmcr    = 0;
    s->tfucr   = 0; s->rfocr = 0;
    s->fcftr   = 0;
    s->rpadir  = 0;
    s->trimd   = 0;

    s->tx_cur  = 0;
    s->rx_cur  = 0;

    memset(&s->mdio, 0, sizeof(s->mdio));
    s->mdio.in_cnt  = -1;
    s->mdio.out_cnt = -1;
    s->mdio.write_cnt = -1;

    etherc_update_irq(s);
}

static void etherc_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RX65NEthercState *s = RENESAS_ETHERC(obj);

    memory_region_init_io(&s->memory, obj, &etherc_ops, s,
                          "renesas-etherc", ETHERC_MMIO_SIZE);
    sysbus_init_mmio(d, &s->memory);

    sysbus_init_irq(d, &s->irq[ETHERC_IRQ_EINT]);
}

static void etherc_realize(DeviceState *dev, Error **errp)
{
    RX65NEthercState *s = RENESAS_ETHERC(dev);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&etherc_net_info, &s->conf,
                          object_get_typename(OBJECT(dev)),
                          dev->id, &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    /* Pre-load MAC address registers from NICConf */
    const uint8_t *m = s->conf.macaddr.a;
    s->mahr = ((uint32_t)m[0] << 24) | ((uint32_t)m[1] << 16) |
              ((uint32_t)m[2] <<  8) | m[3];
    s->malr = ((uint32_t)m[4] << 24) | ((uint32_t)m[5] << 16);
}

/*
 * VMState
 */
static const VMStateDescription vmstate_mdio = {
    .name = "renesas-etherc-mdio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32(preamble,    MDIOState),
        VMSTATE_INT32(in_cnt,      MDIOState),
        VMSTATE_UINT32(sr,         MDIOState),
        VMSTATE_BOOL(is_read,      MDIOState),
        VMSTATE_INT32(write_cnt,   MDIOState),
        VMSTATE_INT32(write_paddr, MDIOState),
        VMSTATE_INT32(write_raddr, MDIOState),
        VMSTATE_UINT16(write_data, MDIOState),
        VMSTATE_UINT16(read_data,  MDIOState),
        VMSTATE_INT32(out_cnt,     MDIOState),
        VMSTATE_BOOL(mdi,          MDIOState),
        VMSTATE_UINT16_ARRAY(phy_regs, MDIOState, 32),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_etherc = {
    .name = "renesas-etherc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ecmr,    RX65NEthercState),
        VMSTATE_UINT32(rflr,    RX65NEthercState),
        VMSTATE_UINT32(ecsr,    RX65NEthercState),
        VMSTATE_UINT32(ecsipr,  RX65NEthercState),
        VMSTATE_UINT32(pir,     RX65NEthercState),
        VMSTATE_UINT32(rdmlr,   RX65NEthercState),
        VMSTATE_UINT32(ipgr,    RX65NEthercState),
        VMSTATE_UINT32(apr,     RX65NEthercState),
        VMSTATE_UINT32(mpr,     RX65NEthercState),
        VMSTATE_UINT32(tpauser, RX65NEthercState),
        VMSTATE_UINT32(bcfrr,   RX65NEthercState),
        VMSTATE_UINT32(mahr,    RX65NEthercState),
        VMSTATE_UINT32(malr,    RX65NEthercState),
        VMSTATE_UINT32(edmr,    RX65NEthercState),
        VMSTATE_UINT32(edtrr,   RX65NEthercState),
        VMSTATE_UINT32(edrrr,   RX65NEthercState),
        VMSTATE_UINT32(tdlar,   RX65NEthercState),
        VMSTATE_UINT32(rdlar,   RX65NEthercState),
        VMSTATE_UINT32(eesr,    RX65NEthercState),
        VMSTATE_UINT32(eesipr,  RX65NEthercState),
        VMSTATE_UINT32(trscer,  RX65NEthercState),
        VMSTATE_UINT32(rmfcr,   RX65NEthercState),
        VMSTATE_UINT32(tftr,    RX65NEthercState),
        VMSTATE_UINT32(fdr,     RX65NEthercState),
        VMSTATE_UINT32(rmcr,    RX65NEthercState),
        VMSTATE_UINT32(fcftr,   RX65NEthercState),
        VMSTATE_UINT32(rpadir,  RX65NEthercState),
        VMSTATE_UINT32(trimd,   RX65NEthercState),
        VMSTATE_UINT32(tx_cur,  RX65NEthercState),
        VMSTATE_UINT32(rx_cur,  RX65NEthercState),
        VMSTATE_STRUCT(mdio, RX65NEthercState, 1, vmstate_mdio, MDIOState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property etherc_properties[] = {
    DEFINE_NIC_PROPERTIES(RX65NEthercState, conf),
};

static void etherc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = etherc_realize;
    dc->vmsd    = &vmstate_etherc;
    device_class_set_legacy_reset(dc, etherc_reset);
    device_class_set_props(dc, etherc_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo etherc_info = {
    .name          = TYPE_RENESAS_ETHERC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RX65NEthercState),
    .instance_init = etherc_init,
    .class_init    = etherc_class_init,
};

static void etherc_register_types(void)
{
    type_register_static(&etherc_info);
}

type_init(etherc_register_types)
