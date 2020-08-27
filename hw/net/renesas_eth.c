/*
 *  Renesas ETHERC / EDMAC
 *
 *  Copyright 2019 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Contributions after 2012-01-13 are licensed under the terms of the
 *  GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"

#include "hw/hw.h"
#include "sysemu/dma.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/irq.h"
#include "hw/net/renesas_eth.h"

/* ETHERC Registers */
REG32(ECMR, 0x00)
  FIELD(ECMR, PRM, 0, 1)
  FIELD(ECMR, DM, 1, 1)
  FIELD(ECMR, RTM, 2, 1)
  FIELD(ECMR, ILB, 3, 1)
  FIELD(ECMR, TE, 5, 1)
  FIELD(ECMR, RE, 6, 1)
  FIELD(ECMR, MPDE, 9, 1)
  FIELD(ECMR, PRCREF, 12, 1)
  FIELD(ECMR, TXF, 16, 1)
  FIELD(ECMR, RXF, 17, 1)
  FIELD(ECMR, PFR, 18, 1)
  FIELD(ECMR, ZPF, 19, 1)
  FIELD(ECMR, TPC, 20, 1)
REG32(RFLR, 0x08)
  FIELD(RFLR, RFL, 0, 12)
REG32(ECSR, 0x10)
  FIELD(ECSR, ICD, 0, 1)
  FIELD(ECSR, MPD, 1, 1)
  FIELD(ECSR, LCHNG, 2, 1)
  FIELD(ECSR, PSRTO, 4, 1)
  FIELD(ECSR, BFR, 5, 1)
REG32(ECSIPR, 0x18)
  FIELD(ECSIPR, ICDIP, 0, 1)
  FIELD(ECSIPR, MPDIP, 1, 1)
  FIELD(ECSIPR, LCHNGIP, 2, 1)
  FIELD(ECSIPR, PSRTOIP, 4, 1)
  FIELD(ECSIPR, BFSIPR, 5, 1)
REG32(PIR, 0x20)
  FIELD(PIR, MDC, 0, 1)
  FIELD(PIR, MMD, 1, 1)
  FIELD(PIR, MDO, 2, 1)
  FIELD(PIR, MDI, 3, 1)
REG32(PSR, 0x28)
  FIELD(PSR, LMON, 0, 1)
REG32(RDMLR, 0x40)
  FIELD(RDMLR, RMD, 0, 20)
REG32(IPGR, 0x50)
  FIELD(IPGR, IPG, 0, 5)
REG32(APR, 0x54)
  FIELD(APR, AP, 0, 16)
REG32(MPR, 0x58)
  FIELD(MPR, MP, 0, 16)
REG32(RFCF, 0x60)
  FIELD(RFCF, RPAUSE, 0, 8)
REG32(TPAUSER, 0x64)
REG32(TPAUSECR, 0x68)
  FIELD(TPAUSECR, TXP, 0, 8)
  FIELD(TPAUSER, TPAUSE, 0, 16)
REG32(BCFRR, 0x6c)
  FIELD(BCFRR, BCF, 0, 16)
REG32(MAHR, 0xc0)
  FIELD(MAHR, MA, 0, 32)
REG32(MALR, 0xc8)
  FIELD(MALR, MA, 0, 16)
REG32(TROCR, 0xd0)
REG32(CDCR, 0xd4)
REG32(LCCR, 0xd8)
REG32(CNDCR, 0xdc)
REG32(CEFCR, 0xe4)
REG32(FRECR, 0xe8)
REG32(TSFRCR, 0xec)
REG32(TLFRCR, 0xf0)
REG32(RFCR, 0xf4)
REG32(MAFCR, 0xf8)

/* EDMAC register */
REG32(EDMR, 0x00)
  FIELD(EDMR, SWR, 0, 1)
  FIELD(EDMR, DL, 4, 2)
  FIELD(EDMR, DE, 6, 1)
REG32(EDTRR, 0x08)
  FIELD(EDTRR, TR, 0, 1)
REG32(EDRRR, 0x10)
  FIELD(EDRRR, RR, 0, 1)
REG32(TDLAR, 0x18)
REG32(RDLAR, 0x20)
REG32(EESR, 0x28)
  FIELD(EESR, CERF, 0, 1)
  FIELD(EESR, PRE,  1, 1)
  FIELD(EESR, RTSF, 2, 1)
  FIELD(EESR, RTLF, 3, 1)
  FIELD(EESR, RRF,  4, 1)
  FIELD(EESR, RMAF, 7, 1)
  FIELD(EESR, TRO,  8, 1)
  FIELD(EESR, CD,   9, 1)
  FIELD(EESR, RDESC, 0, 10)
  FIELD(EESR, DLC,  10, 1)
  FIELD(EESR, CND,  11, 1)
  FIELD(EESR, RDOF, 16, 1)
  FIELD(EESR, RDE,  17, 1)
  FIELD(EESR, FR,   18, 1)
  FIELD(EESR, TFUF, 19, 1)
  FIELD(EESR, TDE,  20, 1)
  FIELD(EESR, TC,   21, 1)
  FIELD(EESR, ECI,  22, 1)
  FIELD(EESR, ADE,  23, 1)
  FIELD(EESR, RFCOF, 24, 1)
  FIELD(EESR, RABT, 25, 1)
  FIELD(EESR, TABT, 26, 1)
  FIELD(EESR, TWB,  30, 1)
REG32(EESIPR, 0x30)
  FIELD(EESIPR, CERFIP, 0, 1)
  FIELD(EESIPR, PREIP,  1, 1)
  FIELD(EESIPR, RTSFIP, 2, 1)
  FIELD(EESIPR, RTLFIP, 3, 1)
  FIELD(EESIPR, RRFIP,  4, 1)
  FIELD(EESIPR, RMAFIP, 7, 1)
  FIELD(EESIPR, TROIP,  8, 1)
  FIELD(EESIPR, CDIP,   9, 1)
  FIELD(EESIPR, DLCIP,  10, 1)
  FIELD(EESIPR, CNDIP,  11, 1)
  FIELD(EESIPR, RDOFIP, 16, 1)
  FIELD(EESIPR, RDEIP,  17, 1)
  FIELD(EESIPR, FRIP,   18, 1)
  FIELD(EESIPR, TFUFIP, 19, 1)
  FIELD(EESIPR, TDEIP,  20, 1)
  FIELD(EESIPR, TCIP,   21, 1)
  FIELD(EESIPR, ECIIP,  22, 1)
  FIELD(EESIPR, ADEIP,  23, 1)
  FIELD(EESIPR, RFCOFIP, 24, 1)
  FIELD(EESIPR, RABTIP, 25, 1)
  FIELD(EESIPR, TABTIP, 26, 1)
  FIELD(EESIPR, TWBIP,  30, 1)
REG32(TRSCER, 0x38)
  FIELD(TRSCER, RRFCE, 4, 1)
  FIELD(TRSCER, RMAFCE, 7, 1)
REG32(RMFCR, 0x40)
  FIELD(RMFCR, MFC, 0, 16)
REG32(TFTR, 0x48)
  FIELD(TFTR, TFT, 0, 11)
REG32(FDR, 0x50)
  FIELD(FDR, RFD, 0, 5)
  FIELD(FDR, TFD, 8, 5)
REG32(RMCR, 0x58)
  FIELD(RMCR, RNR, 0, 1)
  FIELD(RMCR, RNC, 1, 1)
REG32(TFUCR, 0x64)
  FIELD(TFUCR, UNDER, 0, 16)
REG32(RFOCR, 0x68)
  FIELD(RFOCR, OVER, 0, 16)
REG32(IOSR, 0x6c)
  FIELD(IOSR, ELB, 0, 1);
REG32(FCFTR, 0x70)
  FIELD(FCFTR, RFDO, 0, 3)
  FIELD(FCFTR, RFFO, 16, 3)
REG32(RPADIR, 0x78)
  FIELD(RPADIR, PADR, 0, 6)
  FIELD(RPADIR, PADS, 16, 2)
REG32(TRIMD, 0x7c)
  FIELD(TRIMD, TIS, 0, 1)
  FIELD(TRIMD, TIM, 4, 1)
REG32(RBWAR, 0xc8)
REG32(RDFAR, 0xcc)
REG32(TBRAR, 0xd4)
REG32(TDFAR, 0xd8)

/* Transmit Descriptor */
REG32(TD0, 0x0000)
  FIELD(TD0, TFS0, 0, 1)
  FIELD(TD0, TFS1, 1, 1)
  FIELD(TD0, TFS2, 2, 1)
  FIELD(TD0, TFS3, 3, 1)
  FIELD(TD0, TFS8, 8, 1)
  FIELD(TD0, TWBI, 26, 1)
  FIELD(TD0, TFE,  27, 1)
  FIELD(TD0, TFP,  28, 2)
  FIELD(TD0, TDLE, 30, 1)
  FIELD(TD0, TACT, 31, 1)
REG32(TD1, 0x0004)
  FIELD(TD1, TBL, 16, 16)
REG32(TD2, 0x0008)
  FIELD(TD2, TBA, 0, 32)

/* Receive Descriptor */
REG32(RD0, 0x0000)
  FIELD(RD0, RFS,  0, 10)
    FIELD(RD0, RFS0, 0, 1)
    FIELD(RD0, RFS1, 1, 1)
    FIELD(RD0, RFS2, 2, 1)
    FIELD(RD0, RFS3, 3, 1)
    FIELD(RD0, RFS4, 4, 1)
    FIELD(RD0, RFS7, 7, 1)
    FIELD(RD0, RFS8, 8, 1)
    FIELD(RD0, RFS9, 9, 1)
  FIELD(RD0, RFE,  27, 1)
  FIELD(RD0, RFP,  28, 2)
  FIELD(RD0, RFP0, 28, 1)
  FIELD(RD0, RDLE, 30, 1)
  FIELD(RD0, RACT, 31, 1)
REG32(RD1, 0x0004)
  FIELD(RD1, RFL, 0, 16)
  FIELD(RD1, RBL, 16, 16)
REG32(RD2, 0x0008)
  FIELD(RD2, RBA, 0, 32)

static void renesas_eth_set_irq(RenesasEthState *s)
{
    if (s->edmac_regs[R_EESR] & s->edmac_regs[R_EESIPR]) {
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, 0);
    }
}

static bool renesas_eth_can_receive(NetClientState *nc)
{
    RenesasEthState *s = RenesasEth(qemu_get_nic_opaque(nc));

    return FIELD_EX32(s->edmac_regs[R_EDRRR], EDRRR, RR);
}

static void set_ecsr(RenesasEthState *s, int bit)
{
    s->etherc_regs[R_ECSR] = deposit32(s->etherc_regs[R_ECSR], bit, 1, 1);
    if (s->etherc_regs[R_ECSR] & s->etherc_regs[R_ECSIPR]) {
        s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR],
                                           EESR, ECI, 1);
    }
    renesas_eth_set_irq(s);
}

static void renesas_eth_set_link_status(NetClientState *nc)
{
    RenesasEthState *s = RenesasEth(qemu_get_nic_opaque(nc));
    int old_lmon, new_lmon;
    if (s->mdiodev) {
        old_lmon = mdio_phy_linksta(mdio_get_phy(s->mdiodev));
        mdio_phy_set_link(mdio_get_phy(s->mdiodev), !nc->link_down);
        new_lmon = mdio_phy_linksta(mdio_get_phy(s->mdiodev));
        if (old_lmon ^ new_lmon) {
            set_ecsr(s, R_ECSR_LCHNG_SHIFT);
        }
    }
}

static void edmac_write(RenesasEthState *s, const uint8_t *buf,
                        size_t size, int pad)
{
    uint32_t rdesc[3];
    uint32_t eesr;
    int state = 0;

    while (size > 0) {
        size_t wsize;
        /* RDESC read */
        dma_memory_read(&address_space_memory,
                        s->edmac_regs[R_RDFAR], rdesc, sizeof(rdesc));
        if (FIELD_EX32(rdesc[0], RD0, RACT)) {
            if (state == 0) {
                /* Fist block */
                rdesc[0] = FIELD_DP32(rdesc[0], RD0, RFP, 2);
            }
            state++;
            s->edmac_regs[R_RBWAR] = rdesc[2];
            wsize = MIN(FIELD_EX32(rdesc[1], RD1, RBL), size);
            /* Write receive data */
            dma_memory_write(&address_space_memory,
                             s->edmac_regs[R_RBWAR], buf, wsize);
            buf += wsize;
            size -= wsize;
            rdesc[1] = FIELD_DP32(rdesc[1], RD1, RFL, wsize);
            if (size == 0) {
                /* Last descriptor */
                rdesc[0] = FIELD_DP32(rdesc[0], RD0, RFP0, 1);
                if (FIELD_EX32(s->edmac_regs[R_RMCR], RMCR, RNR) == 0) {
                    s->edmac_regs[R_EDRRR] = FIELD_DP32(s->edmac_regs[R_EDRRR],
                                                  EDRRR, RR, 0);
                }
                s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR],
                                                   EESR, FR, 1);
                renesas_eth_set_irq(s);
            }
            eesr = FIELD_EX32(s->edmac_regs[R_EESR], EESR, RDESC);
            rdesc[0] = FIELD_DP32(rdesc[0], RD0, RFS,
                                  eesr & ~(s->edmac_regs[R_TRSCER]));
            rdesc[0] = FIELD_DP32(rdesc[0], RD0, RFE, eesr != 0);
            rdesc[0] = FIELD_DP32(rdesc[0], RD0, RACT, 0);
            /* RDESC write back */
            dma_memory_write(&address_space_memory,
                             s->edmac_regs[R_RDFAR], rdesc, sizeof(rdesc));
            if (FIELD_EX32(rdesc[0], RD0, RDLE)) {
                s->edmac_regs[R_RDFAR] = s->edmac_regs[R_RDLAR];
            } else {
                s->edmac_regs[R_RDFAR] += s->descsize;
            }
            s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR],
                                               EESR, FR, 1);
        } else {
            /* no active RDESC */
            if (FIELD_EX32(s->edmac_regs[R_RMCR], RMCR, RNC) == 0) {
                s->edmac_regs[R_EDRRR] = FIELD_DP32(s->edmac_regs[R_EDRRR],
                                                    EDRRR, RR, 0);
            }
            s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR],
                                               EESR, RDE, 1);
            break;
        }
    }
    renesas_eth_set_irq(s);
}

static inline void update_count(uint32_t *cnt)
{
    if (*cnt < UINT32_MAX) {
        /* Satulate on 32bit value */
        (*cnt)++;
    }
}

#define MIN_BUF_SIZE 60
static ssize_t renesas_eth_receive(NetClientState *nc,
                            const uint8_t *buf, size_t size)
{
    RenesasEthState *s = RenesasEth(qemu_get_nic_opaque(nc));
    static const uint8_t bcast_addr[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    static const uint8_t pad[3] = { 0 };
    uint8_t buf1[MIN_BUF_SIZE];
    bool receive = false;
    size_t pads;
    uint32_t rflr;

    if (size >= 6) {
        if (memcmp(buf, bcast_addr, sizeof(bcast_addr)) == 0) {
            /* broadcast */
            if (s->etherc_regs[R_BCFRR] == 0 ||
                s->etherc_regs[R_BCFRR] < s->rcv_bcast) {
                s->rcv_bcast++;
                receive = true;
            }
        } else if (buf[0] & 0x1) {
            /* multicast */
            receive = true;
            s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR],
                                               EESR, RMAF, 1);
            update_count(&s->edmac_regs[R_MAFCR]);
        } else if (FIELD_EX32(s->edmac_regs[R_ECMR], ECMR, PRM)) {
            /* promiscas */
            receive = true;
        } else if (memcmp(buf, s->macadr, sizeof(s->macadr)) == 0) {
            /* normal */
            receive = true;
        }
    }
    if (!receive) {
        return size;
    }
    /* if too small buffer, then expand it */
    if (size < MIN_BUF_SIZE) {
        memcpy(buf1, buf, size);
        memset(buf1 + size, 0, MIN_BUF_SIZE - size);
        buf = buf1;
        size = MIN_BUF_SIZE;
    }

    rflr = FIELD_EX32(s->etherc_regs[R_RFLR], RFLR, RFL);
    rflr = MAX(rflr, 1518);
    if (size > rflr) {
        update_count(&s->etherc_regs[R_TLFRCR]);
        s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR],
                                           EESR, RTLF, 1);
    }
    pads = FIELD_EX32(s->edmac_regs[R_RPADIR], RPADIR, PADS);
    if (pads > 0) {
        int pos = FIELD_EX32(s->edmac_regs[R_RPADIR], RPADIR, PADR);
        uint8_t *padbuf = g_new(uint8_t, size + pads);
        if (size > pos) {
            if (pos > 0) {
                memcpy(padbuf, buf, pos);
            }
            memcpy(padbuf + pos, pad, pads);
            memcpy(padbuf + pos + pads, buf + pos, size - pos);
        } else {
            pads = 0;
        }
        edmac_write(s, padbuf, size + pads, pads);
        g_free(padbuf);
    } else {
        edmac_write(s, buf, size, 0);
    }
    return size;
}

static size_t edmac_read(RenesasEthState *s, uint8_t **buf)
{
    uint32_t tdesc[3];
    uint32_t size = 0;

    *buf = NULL;
    for (;;) {
        size_t rsize;
        dma_memory_read(&address_space_memory,
                        s->edmac_regs[R_TDFAR], tdesc, sizeof(tdesc));
        if (FIELD_EX32(tdesc[0], TD0, TACT)) {
            s->edmac_regs[R_TBRAR] = tdesc[2];
            rsize = FIELD_EX32(tdesc[1], TD1, TBL);
            *buf = g_realloc(*buf, size + rsize);
            dma_memory_read(&address_space_memory,
                            s->edmac_regs[R_TBRAR], *buf + size, rsize);
            tdesc[0] = FIELD_DP32(tdesc[0], TD0, TACT, 0);
            dma_memory_write(&address_space_memory,
                            s->edmac_regs[R_TDFAR], tdesc, sizeof(tdesc));
            size += rsize;
            if (FIELD_EX32(tdesc[0], TD0, TDLE)) {
                s->edmac_regs[R_TDFAR] = s->edmac_regs[R_TDLAR];
            } else {
                s->edmac_regs[R_TDFAR] += s->descsize;
            }
            if (FIELD_EX32(tdesc[0], TD0, TFP) & 1) {
                break;
            }
        } else {
            s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR],
                                               EESR, TDE, 1);
            renesas_eth_set_irq(s);
            break;
        }
    }
    return size;
}

static void renesas_eth_start_xmit(RenesasEthState *s)
{
    uint8_t *txbuf;
    size_t size;

    size = edmac_read(s, &txbuf);
    qemu_send_packet(qemu_get_queue(s->nic), txbuf, size);
    g_free(txbuf);
    s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR], EESR, TWB, 1);
    s->edmac_regs[R_EDTRR] = FIELD_DP32(s->edmac_regs[R_EDTRR], EDTRR, TR, 0);
    renesas_eth_set_irq(s);
}

static void renesas_eth_reset(RenesasEthState *s)
{
    int i;

    for (i = 0; i < RENESAS_ETHERC_R_MAX; i++) {
        register_reset(&s->etherc_regs_info[i]);
    }
    for (i = 0; i < RENESAS_EDMAC_R_MAX; i++) {
        register_reset(&s->edmac_regs_info[i]);
    }
}

static uint64_t ecsr_pre_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    uint32_t old_val = s->etherc_regs[R_ECSR];

    val ^= old_val;
    val &= old_val;
    return val;
}

static void ecsr_post_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);

    if (s->etherc_regs[R_ECSR] & s->etherc_regs[R_ECSIPR]) {
        s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR],
                                           EESR, ECI, 1);
    } else {
        s->edmac_regs[R_EESR] = FIELD_DP32(s->edmac_regs[R_EESR],
                                           EESR, ECI, 0);
    }
    renesas_eth_set_irq(s);
}

static void pir_post_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    if (s->mdiodev) {
        mdio_set_mdc_pin(s->mdiodev, FIELD_EX32(val, PIR, MDC));
        if (FIELD_EX32(val, PIR, MMD)) {
            mdio_set_mdo_pin(s->mdiodev, FIELD_EX32(val, PIR, MDO));
        }
    }
}

static uint64_t pir_post_read(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    if (s->mdiodev) {
        val = FIELD_DP64(val, PIR, MDI, mdio_read_mdi_pin(s->mdiodev));
    }
    return val;
}

static uint64_t mar_pre_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    if (FIELD_EX32(s->edmac_regs[R_EDTRR], EDTRR, TR) ||
        FIELD_EX32(s->edmac_regs[R_EDRRR], EDRRR, RR)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_eth: Tx/Rx enabled in MAR write.\n");
    }
    return val;
}

static void mar_post_write(RegisterInfo *reg, uint64_t val)
{
    int i;
    RenesasEthState *s = RenesasEth(reg->opaque);
    for (i = 0; i < 4; i++) {
        s->macadr[i] = extract32(s->etherc_regs[R_MAHR], 8 * (3 - i), 8);
    }
    for (i = 0; i < 2; i++) {
        s->macadr[i + 4] = extract32(s->etherc_regs[R_MALR], 8 * (1 - i), 8);
    }
}

static uint64_t etherc_counter_write(RegisterInfo *reg, uint64_t val)
{
    /* Counter register clear in any write operation */
    return 0;
}

static void edmr_post_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    uint32_t TDLAR, RMFCR, TFUCR, RFOCR;
    int dl;

    if (FIELD_EX32(val, EDMR, SWR)) {
        /* Following register keep for SWR */
        TDLAR = s->edmac_regs[R_TDLAR];
        RMFCR = s->edmac_regs[R_RMFCR];
        TFUCR = s->edmac_regs[R_TFUCR];
        RFOCR = s->edmac_regs[R_RFOCR];
        renesas_eth_reset(s);
        s->edmac_regs[R_TDLAR] = TDLAR;
        s->edmac_regs[R_RMFCR] = RMFCR;
        s->edmac_regs[R_TFUCR] = TFUCR;
        s->edmac_regs[R_RFOCR] = RFOCR;
    }
    dl = FIELD_EX32(val, EDMR, DL) % 3;
    s->descsize = 16 << dl;
}

static void edtrr_post_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    if (FIELD_EX32(val, EDTRR, TR)) {
        renesas_eth_start_xmit(s);
    }
}

static uint64_t eesr_pre_write(RegisterInfo *reg, uint64_t val)
{
    uint32_t eesr;
    RenesasEthState *s = RenesasEth(reg->opaque);
    /* flag clear for write 1 */
    eesr = s->edmac_regs[R_EESR];
    val = FIELD_DP64(val, EESR, ECI, 0); /* Keep ECI value */
    eesr &= ~val;
    return eesr;
}

static void eesr_post_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    renesas_eth_set_irq(s);
}

static void tdlar_post_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    s->edmac_regs[R_TDFAR] = s->edmac_regs[R_TDLAR];
}

static void rdlar_post_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    s->edmac_regs[R_RDFAR] = s->edmac_regs[R_RDLAR];
}

static uint64_t fdr_pre_write(RegisterInfo *reg, uint64_t val)
{
    RenesasEthState *s = RenesasEth(reg->opaque);
    if (FIELD_EX32(val, FDR, TFD) != 7 || FIELD_EX32(val, FDR, RFD) != 7) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_eth: invalid FDR setting %"
                      HWADDR_PRIX ".\n", val);
    }
    if (FIELD_EX32(s->edmac_regs[R_EDTRR], EDTRR, TR) ||
        FIELD_EX32(s->edmac_regs[R_EDRRR], EDRRR, RR)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_eth: Tx/Rx enabled in FDR write.\n");
    }
    return val;
}

static uint64_t edmac_reg_read(void *opaque, hwaddr addr, unsigned int size)
{
    RegisterInfoArray *ra = opaque;
    RenesasEthState *s = RenesasEth(ra->r[0]->opaque);
    if (clock_is_enabled(s->ick)) {
        return register_read_memory(ra, addr, size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_eth: EDMAC module stopped.\n");
        return UINT64_MAX;
    }
}

static void edmac_reg_write(void *opaque, hwaddr addr,
                        uint64_t value, unsigned int size)
{
    RegisterInfoArray *ra = opaque;
    RenesasEthState *s = RenesasEth(ra->r[0]->opaque);
    if (clock_is_enabled(s->ick)) {
        register_write_memory(ra, addr, value, size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_eth: EDMAC module stopped.\n");
    }
}

static const MemoryRegionOps renesas_etherc_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps renesas_edmac_ops = {
    .read = edmac_reg_read,
    .write = edmac_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static NetClientInfo net_renesas_eth_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = renesas_eth_can_receive,
    .receive = renesas_eth_receive,
    .link_status_changed = renesas_eth_set_link_status,
};

static const RegisterAccessInfo renesas_etherc_regs_info[] = {
    { .name = "ECMR", .addr = A_ECMR,
      .rsvd = 0xffe0ed90, },
    { .name = "RFLR", .addr = A_RFLR,
      .rsvd = 0xfffff000, },
    { .name = "ECSR", .addr = A_ECSR,
      .rsvd = 0xffffffc8,
      .pre_write = ecsr_pre_write,
      .post_write = ecsr_post_write, },
    { .name = "ECSIPR", .addr = A_ECSIPR,
      .rsvd = 0xffffffc8,
      .post_write = ecsr_post_write, },
    { .name = "PIR", .addr = A_PIR,
      .rsvd = 0xfffffff0,
      .post_write = pir_post_write,
      .post_read = pir_post_read, },
    { .name = "PSR", .addr = A_PSR,
      .rsvd = 0xfffffffe, },
    { .name = "RDMLR", .addr = A_RDMLR,
      .rsvd = 0xfff00000, },
    { .name = "IPGR", .addr = A_IPGR,
      .rsvd = 0xffffffe0, .reset = 0x00000014, },
    { .name = "APR", .addr = A_APR,
      .rsvd = 0xffff0000, },
    { .name = "MPR", .addr = A_MPR,
      .rsvd = 0xffff0000, },
    { .name = "RFCF", .addr = A_RFCF,
      .rsvd = 0xffffff00, },
    { .name = "TPAUSER", .addr = A_TPAUSER,
      .rsvd = 0xffff0000, },
    { .name = "TPAUSECR", .addr = A_TPAUSECR,
      .rsvd = 0xffffff00, },
    { .name = "BCFRR", .addr = A_BCFRR,
      .rsvd = 0xffff0000, },
    { .name = "MAHR", .addr = A_MAHR,
      .pre_write = mar_pre_write,
      .post_write = mar_post_write, },
    { .name = "MALR", .addr = A_MALR,
      .rsvd = 0xffff0000,
      .pre_write = mar_pre_write,
      .post_write = mar_post_write, },
    { .name = "TROCR", .addr = A_TROCR,
      .pre_write = etherc_counter_write, },
    { .name = "CDCR", .addr = A_CDCR,
      .pre_write = etherc_counter_write, },
    { .name = "LCCR", .addr = A_LCCR,
      .pre_write = etherc_counter_write, },
    { .name = "CNDCR", .addr = A_CNDCR,
      .pre_write = etherc_counter_write, },
    { .name = "CEFCR", .addr = A_CEFCR,
      .pre_write = etherc_counter_write, },
    { .name = "FRECR", .addr = A_FRECR,
      .pre_write = etherc_counter_write, },
    { .name = "TSFRCR", .addr = A_TSFRCR,
      .pre_write = etherc_counter_write, },
    { .name = "TLFRCR", .addr = A_TLFRCR,
      .pre_write = etherc_counter_write, },
    { .name = "RFCR", .addr = A_RFCR,
      .pre_write = etherc_counter_write, },
    { .name = "MAFCR", .addr = A_MAFCR,
      .pre_write = etherc_counter_write, },
};

static const RegisterAccessInfo renesas_edmac_regs_info[] = {
    { .name = "EDMR", .addr = A_EDMR,
      .rsvd = 0xfffffff8e,
      .post_write = edmr_post_write, },
    { .name = "EDTRR", .addr = A_EDTRR,
      .rsvd = 0xffffffffe,
      .post_write = edtrr_post_write, },
    { .name = "EDRRR", .addr = A_EDRRR,
      .rsvd = 0xffffffffe, },
    { .name = "TDLAR", .addr = A_TDLAR,
      .post_write = tdlar_post_write, },
    { .name = "RDLAR", .addr = A_RDLAR,
      .post_write = rdlar_post_write, },
    { .name = "EESR", .addr = A_EESR,
      .rsvd = 0xb800f0c0, .ro = 0x00400000,
      .pre_write = eesr_pre_write,
      .post_write = eesr_post_write, },
    { .name = "EESIPR", .addr = A_EESIPR,
      .rsvd = 0xb800f060,
      .post_write = eesr_post_write, },
    { .name = "TRSCER", .addr = A_TRSCER,
      .rsvd = 0xfffffd6f, },
    { .name = "RMFCR", .addr = A_RMFCR,
      .rsvd = 0xffff0000, },
    { .name = "TFTR", .addr = A_TFTR,
      .rsvd = 0xfffff800, },
    { .name = "FDR", .addr = A_FDR,
      .rsvd = 0xffffe0e0,
      .pre_write = fdr_pre_write, },
    { .name = "RMCR", .addr = A_RMCR,
      .rsvd = 0xfffffffc, },
    { .name = "TFUCR", .addr = A_TFUCR,
      .rsvd = 0xffff0000,
      .pre_write = etherc_counter_write, },
    { .name = "RFOCR", .addr = A_RFOCR,
      .rsvd = 0xffff0000,
      .pre_write = etherc_counter_write, },
    { .name = "RBWAR", .addr = A_RBWAR,
      .ro = 0xffffffff, .rsvd = 0xffff0000, },
    { .name = "RDFAR", .addr = A_RDFAR,
      .ro = 0xffffffff, .rsvd = 0xffff0000, },
    { .name = "TBRAR", .addr = A_TBRAR,
      .ro = 0xffffffff, .rsvd = 0xffff0000, },
    { .name = "TDFAR", .addr = A_TDFAR,
      .ro = 0xffffffff, .rsvd = 0xffff0000, },
    { .name = "FCFTR", .addr = A_FCFTR,
      .rsvd = 0xfff8fff8, },
    { .name = "RPADIR", .addr = A_RPADIR,
      .rsvd = 0xfffcffc0, },
    { .name = "TRIMD", .addr = A_TRIMD,
      .rsvd = 0xffffffee, },
    { .name = "IOSR", .addr = A_IOSR,
      .rsvd = 0xfffffffe, },
};

static void renesas_eth_realize(DeviceState *dev, Error **errp)
{
    RenesasEthState *s = RenesasEth(dev);

    s->nic = qemu_new_nic(&net_renesas_eth_info, &s->conf,
                          object_get_typename(OBJECT(s)), dev->id, s);

    renesas_eth_reset(s);
    if (s->mdiodev) {
        mdio_phy_set_link(mdio_get_phy(s->mdiodev),
                          !qemu_get_queue(s->nic)->link_down);
    }
}

static Property renesas_eth_properties[] = {
    DEFINE_NIC_PROPERTIES(RenesasEthState, conf),
    DEFINE_PROP_LINK("mdio", RenesasEthState, mdiodev, TYPE_ETHER_MDIO_BB,
                     MDIOState *),
    DEFINE_PROP_END_OF_LIST(),
};

static void renesas_eth_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RenesasEthState *s = RenesasEth(obj);
    RegisterInfoArray *ra_etherc;
    RegisterInfoArray *ra_edmac;

    memory_region_init(&s->etherc_mem, obj, "renesas-etherc", 0x100);
    ra_etherc = register_init_block32(DEVICE(d), renesas_etherc_regs_info,
                                      ARRAY_SIZE(renesas_etherc_regs_info),
                                      s->etherc_regs_info, s->etherc_regs,
                                      &renesas_etherc_ops,
                                      false, 0x100);
    memory_region_add_subregion(&s->etherc_mem, 0x00, &ra_etherc->mem);
    sysbus_init_mmio(d, &s->etherc_mem);

    memory_region_init(&s->edmac_mem, obj, "renesas-edmac", 0x100);
    ra_edmac = register_init_block32(DEVICE(d), renesas_edmac_regs_info,
                                     ARRAY_SIZE(renesas_edmac_regs_info),
                                     s->edmac_regs_info, s->edmac_regs,
                                     &renesas_edmac_ops,
                                     false, 0x100);
    memory_region_add_subregion(&s->edmac_mem, 0x00, &ra_edmac->mem);
    sysbus_init_mmio(d, &s->edmac_mem);

    sysbus_init_irq(d, &s->irq);
    s->ick =  qdev_init_clock_in(DEVICE(d), "ick", NULL, NULL);
}

static void renesas_eth_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    device_class_set_props(dc, renesas_eth_properties);
    dc->realize = renesas_eth_realize;
}

static const TypeInfo renesas_eth_info = {
    .name          = TYPE_RENESAS_ETH,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasEthState),
    .instance_init = renesas_eth_init,
    .class_init    = renesas_eth_class_init,
};

static void renesas_eth_register_types(void)
{
    type_register_static(&renesas_eth_info);
}

type_init(renesas_eth_register_types)
