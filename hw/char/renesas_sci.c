/*
 * Renesas Serial Communication Interface (SCI / SCIa / SCIF)
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 * (Rev.1.40 R01UH0033EJ0140)
 * And SH7751 Group, SH7751R Group User's Manual: Hardware
 * (Rev.4.01 R01UH0457EJ0401)
 *
 * Copyright (c) 2020 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/char/renesas_sci.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"

/* SCI register map */
REG8(SMR, 0)
  FIELD(SMR, CKS,  0, 2)
  FIELD(SMR, MP,   2, 1)
  FIELD(SMR, STOP, 3, 1)
  FIELD(SMR, PM,   4, 1)
  FIELD(SMR, PE,   5, 1)
  FIELD(SMR, CHR,  6, 1)
  FIELD(SMR, CM,   7, 1)
REG16(BRR, 2)
REG16(SCR, 4)
  FIELD(SCR, CKE, 0, 2)
  FIELD(SCR, TEIE, 2, 1)
  FIELD(SCR, MPIE, 3, 1)
  FIELD(SCR, REIE, 3, 1)
  FIELD(SCR, RE,   4, 1)
  FIELD(SCR, TE,   5, 1)
  FIELD(SCR, RIE,  6, 1)
  FIELD(SCR, TIE,  7, 1)
REG16(TDR, 6)
REG16(SSR, 8)
  FIELD(SSR, MPBT, 0, 1)
  FIELD(SSR, MPB,  1, 1)
  FIELD(SSR, TEND, 2, 1)
  FIELD(SSR, ERR, 3, 3)
    FIELD(SSR, PER,  3, 1)
    FIELD(SSR, FER,  4, 1)
    FIELD(SSR, ORER, 5, 1)
  FIELD(SSR, RDRF, 6, 1)
  FIELD(SSR, TDRE, 7, 1)
REG16(FSR, 8)
  FIELD(FSR, DR, 0, 1)
  FIELD(FSR, RDF, 1, 1)
  FIELD(FSR, RDF_DR, 0, 2)
  FIELD(FSR, PER, 2, 1)
  FIELD(FSR, FER, 3, 1)
  FIELD(FSR, BRK, 4, 1)
  FIELD(FSR, TDFE, 5, 1)
  FIELD(FSR, TEND, 6, 1)
  FIELD(FSR, ER, 7, 1)
  FIELD(FSR, FERn, 8, 4)
  FIELD(FSR, PERn, 12, 4)
REG16(RDR, 10)
REG16(SCMR, 12)
  FIELD(SCMR, SMIF, 0, 1)
  FIELD(SCMR, SINV, 2, 1)
  FIELD(SCMR, SDIR, 3, 1)
  FIELD(SCMR, BCP2, 7, 1)
REG16(FCR, 12)
  FIELD(FCR, LOOP, 0, 1)
  FIELD(FCR, RFRST, 1, 1)
  FIELD(FCR, TFRST, 2, 1)
  FIELD(FCR, MCE, 3, 1)
  FIELD(FCR, TTRG, 4, 2)
  FIELD(FCR, RTRG, 6, 2)
  FIELD(FCR, RSTRG, 8, 3)
REG16(SEMR, 14)
  FIELD(SEMR, ACS0, 0, 1)
  FIELD(SEMR, ABCS, 4, 1)
REG16(FDR, 14)
  FIELD(FDR, Rn, 0, 4)
  FIELD(FDR, Tn, 8, 4)
REG16(SPTR, 16)
  FIELD(SPTR, SPB2DT, 0, 1)
  FIELD(SPTR, SPB2IO, 1, 1)
  FIELD(SPTR, SCKDT, 2, 1)
  FIELD(SPTR, SCKIO, 3, 1)
  FIELD(SPTR, CTSDT, 4, 1)
  FIELD(SPTR, CTSIO, 5, 1)
  FIELD(SPTR, RTSDT, 6, 1)
  FIELD(SPTR, RTSIO, 7, 1)
  FIELD(SPTR, EIO, 7, 1)
REG16(LSR, 18)
  FIELD(LSR, ORER, 0, 1)

#define SCIF_FIFO_DEPTH 16
#define IS_SCI(sci) (sci->feature < SCI_FEAT_SCIF)
#define IS_SCIA(sci) (IS_SCI(sci) && sci->feature >= SCI_FEAT_SCIA)
#define IS_SCIF(sci) (!IS_SCI(sci))

static const int sci_rtrg[] = {1, 4, 8, 14};

static void update_event_time(RSCIState *sci, int evt, int64_t t)
{
    if (t > 0) {
        t +=  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        sci->event[evt].time = t;
        if (timer_expire_time_ns(sci->event_timer) > t) {
            timer_mod(sci->event_timer, t);
        }
    } else {
        sci->event[evt].time = 0;
    }
}

static int sci_is_rxi(RSCIState *sci)
{
    int eio;
    int enable;
    enable = FIELD_EX16(sci->scr, SCR, RIE);
    if (IS_SCI(sci)) {
        eio = (sci->feature != SCI_FEAT_SCI) ||
            (FIELD_EX16(sci->sptr, SPTR, EIO) == 0);
        return FIELD_EX16(sci->Xsr, SSR, RDRF) && enable && eio;
    } else {
        return (FIELD_EX16(sci->Xsr, FSR, RDF_DR) != 0) && enable;
    }
}

static int sci_is_txi(RSCIState *sci)
{
    int enable = FIELD_EX16(sci->scr, SCR, TIE);
    if (IS_SCI(sci)) {
        return enable && FIELD_EX16(sci->Xsr, SSR, TDRE);
    } else {
        return enable && FIELD_EX16(sci->Xsr, FSR, TDFE);
    }
}

static void sci_irq(RSCIState *sci, int req)
{
    int br;
    int irq;
    int rie;

    switch (req) {
    case ERI:
        rie = FIELD_EX16(sci->scr, SCR, RIE);
        if (IS_SCI(sci)) {
            irq = rie && (FIELD_EX16(sci->Xsr, SSR, ERR) != 0);
        } else {
            irq = (rie || FIELD_EX16(sci->scr, SCR, REIE)) &&
                FIELD_EX16(sci->Xsr, FSR, ER);
        }
        qemu_set_irq(sci->irq[ERI], irq);
        break;
    case RXI:
        if (IS_SCIA(sci)) {
            if (sci_is_rxi(sci)) {
                qemu_irq_pulse(sci->irq[RXI]);
            }
        } else {
            qemu_set_irq(sci->irq[RXI], sci_is_rxi(sci));
        }
        break;
    case TXI:
        if (IS_SCIA(sci)) {
            if (sci_is_txi(sci)) {
                qemu_irq_pulse(sci->irq[TXI]);
            }
        } else {
            qemu_set_irq(sci->irq[TXI], sci_is_txi(sci));
        }
        break;
    case BRI: /* TEI */
        if (IS_SCI(sci)) {
            qemu_set_irq(sci->irq[TEI],
                         FIELD_EX16(sci->Xsr, SSR, TEND) &&
                         FIELD_EX16(sci->scr, SCR, TEIE));
        } else {
            rie = FIELD_EX16(sci->scr, SCR, RIE);
            br = (rie || FIELD_EX16(sci->scr, SCR, REIE)) &&
                FIELD_EX16(sci->Xsr, FSR, BRK);
            qemu_set_irq(sci->irq[BRI], br);
        }
        break;
    }
}

static int can_receive(void *opaque)
{
    RSCIState *sci = RSCI(opaque);
    int fifo_free = 0;
    if (FIELD_EX16(sci->scr, SCR, RE)) {
        /* Receiver enabled */
        fifo_free = fifo8_num_free(&sci->rxfifo);
        if (IS_SCIF(sci) && fifo_free == 0) {
            /* FIFO overrun */
            sci->lsr = FIELD_DP16(sci->lsr, LSR, ORER, 1);
            sci_irq(sci, ERI);
        }
    }
    return fifo_free;
}

static void sci_receive(void *opaque, const uint8_t *buf, int size)
{
    RSCIState *sci = RSCI(opaque);
    int rtrg;

    fifo8_push_all(&sci->rxfifo, buf, size);
    if (sci->event[RXNEXT].time == 0) {
        if (IS_SCI(sci)) {
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, RDRF, 1);
            update_event_time(sci, RXNEXT, sci->trtime);
        } else {
            rtrg = sci_rtrg[FIELD_EX16(sci->fcr, FCR, RTRG)];
            if (fifo8_num_used(&sci->rxfifo) >= rtrg) {
                sci->Xsr = FIELD_DP16(sci->Xsr, FSR, RDF, 1);
            } else {
                update_event_time(sci, RXTOUT, 15 * sci->etu);
            }
        }
        sci_irq(sci, RXI);
    }
}

static void sci_send_byte(RSCIState *sci)
{
    if (IS_SCI(sci)) {
        if (qemu_chr_fe_backend_connected(&sci->chr)) {
            qemu_chr_fe_write_all(&sci->chr, &sci->tdr, 1);
        }
        sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TEND, 0);
        sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TDRE, 1);
    }
}

static int transmit_byte(RSCIState *sci)
{
    int64_t elapsed;
    int byte = 0;
    if (sci->tx_start_time > 0) {
        elapsed = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - sci->tx_start_time;
        byte = elapsed / sci->trtime;
        if (byte > sci->tdcnt) {
            byte = sci->tdcnt;
        }
    }
    return byte;
}

static int64_t sci_rx_timeout(RSCIState *sci)
{
    if (IS_SCIF(sci)) {
        sci->Xsr = FIELD_DP16(sci->Xsr, FSR, DR, 1);
        sci_irq(sci, RXI);
    }
    return 0;
}

static int64_t sci_rx_next(RSCIState *sci)
{
    int64_t next_event = 0;
    if (IS_SCI(sci) && !fifo8_is_empty(&sci->rxfifo)) {
        if (FIELD_EX16(sci->Xsr, SSR, RDRF)) {
            /* Receiver overrun */
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, ORER, 1);
            sci_irq(sci, ERI);
        } else {
            /* Trigger next event */
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, RDRF, 1);
            sci_irq(sci, RXI);
            next_event = sci->trtime;
        }
    }
    return next_event;
}

static int64_t sci_tx_empty(RSCIState *sci)
{
    int64_t ret = 0;
    if (IS_SCI(sci)) {
        if (!FIELD_EX16(sci->Xsr, SSR, TDRE)) {
            sci_send_byte(sci);
            ret = sci->trtime;
            sci_irq(sci, TXI);
        } else {
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TEND, 1);
            sci_irq(sci, TEI);
        }
    } else {
        sci->tdcnt -= transmit_byte(sci);
        sci->Xsr = FIELD_DP16(sci->Xsr, FSR, TDFE, 1);
        sci_irq(sci, TXI);
    }
    return ret;
}

static int64_t sci_tx_end(RSCIState *sci)
{
    if (IS_SCIF(sci)) {
        sci->tdcnt = 0;
        sci->Xsr = FIELD_DP16(sci->Xsr, FSR, TEND, 1);
        sci_irq(sci, TEI);
    }
    return 0;
}

static void sci_timer_event(void *opaque)
{
    RSCIState *sci = RSCI(opaque);
    int64_t now, next, t;
    int i;

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    next = INT64_MAX;
    for (i = 0; i < NR_SCI_EVENT; i++) {
        if (sci->event[i].time > 0 && sci->event[i].time <= now) {
            t = sci->event[i].handler(sci);
            sci->event[i].time = (t > 0) ? now + t : 0;
        }
        if (sci->event[i].time > 0) {
            next = MIN(next, sci->event[i].time);
        }
    }
    if (next < INT64_MAX) {
        timer_mod(sci->event_timer, next);
    } else {
        timer_del(sci->event_timer);
    }
}

static void update_trtime(RSCIState *sci)
{
    int divrate;
    if (IS_SCIA(sci)) {
        divrate = 16 * (2 - FIELD_EX8(sci->semr, SEMR, ABCS));
    } else {
        divrate = 32;
    }

    /* x bit transmit time (divrate * brr) / base freq */
    sci->etu = divrate * 1 << (2 * FIELD_EX16(sci->smr, SMR, CKS));
    sci->etu *= sci->brr + 1;
    sci->etu *= NANOSECONDS_PER_SECOND;
    sci->etu /= sci->input_freq;

    /* char per bits */
    sci->trtime = 8 - FIELD_EX16(sci->smr, SMR, CHR);
    sci->trtime += FIELD_EX16(sci->smr, SMR, PE);
    sci->trtime += FIELD_EX16(sci->smr, SMR, STOP) + 1 + 1;
    sci->trtime *= sci->etu;
}

#define IS_TR_ENABLED(scr) \
    (FIELD_EX16(scr, SCR, TE) || FIELD_EX16(scr, SCR, RE))

#define SCI_IS_NOT_SUPPORTED(sci, name)                                 \
    if (sci->feature == SCI_FEAT_SCI) {                                 \
        qemu_log_mask(LOG_GUEST_ERROR,                                  \
                      "reneas_sci: " #name " is not supported.\n");     \
    }

static void sci_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RSCIState *sci = RSCI(opaque);
    int txtrg;
    int rxtrg;
    uint8_t txd;
    uint16_t ssr_mask;
    bool tx_start = false;

    if (IS_SCI(sci)) {
        txtrg = 1;
    } else {
        txtrg = 1 << (3 - FIELD_EX16(sci->fcr, FCR, TTRG));
    }
    switch (sci->regsize) {
    case 8:
        addr <<= 1; break;
    case 32:
        addr >>= 1; break;
    }
    switch (addr) {
    case A_SMR:
        if (IS_SCIA(sci) && IS_TR_ENABLED(sci->scr)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "reneas_sci: SMR write protected.\n");
            break;
        }
        sci->smr = val;
        update_trtime(sci);
        break;
    case A_BRR:
        if (IS_SCIA(sci) && IS_TR_ENABLED(sci->scr)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "reneas_sci: BRR write protected.\n");
            break;
        }
        sci->brr = val;
        update_trtime(sci);
        break;
    case A_SCR:
        sci->scr = val;
        if (FIELD_EX16(sci->scr, SCR, TE)) {
            /* Transmitter enable */
            if (IS_SCI(sci)) {
                sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TDRE, 1);
                sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TEND, 1);
            } else {
                sci->Xsr = FIELD_DP16(sci->Xsr, FSR, TEND, 1);
                sci->Xsr = FIELD_DP16(sci->Xsr, FSR, TDFE, 1);
                sci->tx_start_time = 0;
            }
            sci_irq(sci, TXI);
            sci_irq(sci, TEI);
        } else {
            /* Transmitter disable  */
            update_event_time(sci, TXEND, 0);
            update_event_time(sci, TXEMPTY, 0);
        }
        break;
    case A_TDR:
        if (IS_SCI(sci)) {
            sci->tdr = val;
            if (IS_SCIA(sci)) {
                if (FIELD_EX16(sci->Xsr, SSR, TEND)) {
                    update_event_time(sci, TXEMPTY, sci->trtime);
                    sci_send_byte(sci);
                } else {
                    sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TDRE, 0);
                }
                sci_irq(sci, TXI);
                sci_irq(sci, TEI);
            }
        } else {
            if (sci->tx_start_time > 0) {
                sci->tdcnt -= transmit_byte(sci);
            } else {
                sci->tx_start_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            }
            if (sci->tdcnt >= SCIF_FIFO_DEPTH) {
                break;
            }
            txd = val;
            if (qemu_chr_fe_backend_connected(&sci->chr)) {
                qemu_chr_fe_write_all(&sci->chr, &txd, 1);
            }
            if (FIELD_EX16(sci->fcr, FCR, LOOP) && can_receive(sci) > 0) {
                /* Loopback mode */
                sci_receive(sci, &txd, 1);
            }
            sci->tdcnt++;
            sci->Xsr = FIELD_DP16(sci->Xsr, FSR, TEND, 0);
            update_event_time(sci, TXEND, sci->tdcnt);
            if (sci->tdcnt > txtrg) {
                sci->Xsr = FIELD_DP16(sci->Xsr, FSR, TDFE, 0);
                update_event_time(sci, TXEMPTY, sci->tdcnt - txtrg + 1);
                sci_irq(sci, TXI);
            }
        }
        break;
    case A_FSR: /* A_SSR */
        if (IS_SCI(sci)) {
            /* Mask for read only bits */
            ssr_mask = IS_SCIA(sci) ? 0xc7 : 0x07;
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, MPBT,
                                 FIELD_EX16(val, SSR, MPBT));
            sci->Xsr &= (val | ssr_mask);
            /* Clear ERI */
            sci_irq(sci, ERI);
            if (sci->feature == SCI_FEAT_SCI) {
                tx_start = FIELD_EX16(sci->read_Xsr, SSR, TDRE) &&
                    !FIELD_EX16(sci->Xsr, SSR, TDRE) &&
                    (FIELD_EX16(sci->Xsr, SSR, ERR) == 0);
                if (tx_start) {
                    sci_send_byte(sci);
                    update_event_time(sci, TXEMPTY, sci->trtime);
                    sci_irq(sci, TXI);
                }
            }
        } else {
            rxtrg = sci_rtrg[FIELD_EX16(sci->fcr, FCR, RTRG)];
            ssr_mask = ~(sci->read_Xsr & 0xf3);
            sci->tdcnt -= transmit_byte(sci);
            if (sci->tdcnt < txtrg) {
                ssr_mask = FIELD_DP16(ssr_mask, FSR, TDFE, 1);
            }
            if (fifo8_num_used(&sci->rxfifo) >= rxtrg) {
                ssr_mask = FIELD_DP16(ssr_mask, FSR, RDF, 1);
            }
            sci->Xsr &= (val | ssr_mask);
            sci_irq(sci, ERI);
            sci_irq(sci, RXI);
            sci_irq(sci, TXI);
        }
        break;
    case A_RDR:
        qemu_log_mask(LOG_GUEST_ERROR, "reneas_sci: RDR is read only.\n");
        break;
    case A_FCR: /* A_SCMR / A_SPTR */
        if (IS_SCI(sci)) {
            if (sci->feature == SCI_FEAT_SCI) {
                sci->sptr = val;
            } else {
                sci->scmr = val;
            }
        } else {
            sci->fcr = val;
            if (FIELD_EX16(sci->fcr, FCR, RFRST)) {
                fifo8_reset(&sci->rxfifo);
                update_event_time(sci, RXTOUT, 0);
                update_event_time(sci, RXNEXT, 0);
            }
            if (FIELD_EX16(sci->fcr, FCR, TFRST)) {
                sci->tdcnt = 0;
            }
        }
        break;
    case A_FDR: /* A_SEMR */
        if (IS_SCI(sci)) {
            SCI_IS_NOT_SUPPORTED(sci, SEMR);
            sci->semr = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "reneas_sci: FDR is read only.\n");
        }
        break;
    case A_SPTR:
        if (IS_SCI(sci)) {
            goto error;
        } else {
            sci->sptr = val;
        }
        break;
    case A_LSR:
        if (IS_SCI(sci)) {
            goto error;
        } else {
            if (FIELD_EX16(sci->read_lsr, LSR, ORER) != 1) {
                val = FIELD_DP16(val, LSR, ORER, 1);
            }
            sci->lsr &= val;
            sci_irq(sci, ERI);
        }
        break;
    default:
    error:
        qemu_log_mask(LOG_UNIMP, "renesas_sci: Register 0x%" HWADDR_PRIX
                      " not implemented\n", addr);
    }
}

static uint64_t sci_read(void *opaque, hwaddr addr, unsigned size)
{
    RSCIState *sci = RSCI(opaque);
    uint64_t ret;

    switch (sci->regsize) {
    case 8:
        addr <<= 1; break;
    case 32:
        addr >>= 1; break;
    }

    switch (addr) {
    case A_SMR:
        return sci->smr;
    case A_BRR:
        return sci->brr;
    case A_SCR:
        return sci->scr;
    case A_TDR:
        if (IS_SCI(sci)) {
            return sci->tdr;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "reneas_sci: TDR is write only.\n");
            return UINT64_MAX;
        }
    case A_FSR: /* A_SSR */
        sci->read_Xsr = sci->Xsr;
        return sci->Xsr;
    case A_RDR:
        ret = fifo8_pop(&sci->rxfifo);
        if (IS_SCIA(sci)) {
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, RDRF, 0);
        }
        return ret;
    case A_FCR: /* A_SCMR / A_SPTR */
        if (IS_SCI(sci)) {
            if (IS_SCIA(sci)) {
                return sci->scmr;
            } else {
                return sci->sptr;
            }
        } else {
            return sci->fcr & 0x7ff;
        }
    case A_FDR: /* A_SEMR */
        if (IS_SCI(sci)) {
            SCI_IS_NOT_SUPPORTED(sci, SEMR);
            return sci->semr;
        } else {
            ret = 0;
            ret = FIELD_DP16(ret, FDR, Rn, fifo8_num_used(&sci->rxfifo));
            ret = FIELD_DP16(ret, FDR, Tn, sci->tdcnt - transmit_byte(sci));
            return ret;
        }
    case A_SPTR:
        if (IS_SCI(sci)) {
            goto error;
        } else {
            return sci->sptr;
        }
    case A_LSR:
        if (IS_SCI(sci)) {
            goto error;
        } else {
            sci->read_lsr = sci->lsr;
            return sci->lsr;
        }
    default:
    error:
        qemu_log_mask(LOG_UNIMP, "renesas_sci: Register 0x%" HWADDR_PRIX
                      " not implemented.\n", addr);
    }
    return UINT64_MAX;
}

static const MemoryRegionOps sci_ops = {
    .write = sci_write,
    .read  = sci_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .max_access_size = 4,
    },
};

static void sci_register_init(RSCIState *sci)
{
    int i;
    sci->smr = sci->scr = 0x00;
    sci->brr = 0xff;
    if (IS_SCI(sci)) {
        sci->tdr = 0xff;
        sci->Xsr = 0x84;
        sci->scmr = 0x00;
        sci->semr = 0x00;
        sci->sptr = 0x00;
    } else {
        sci->Xsr = 0x0060;
        sci->fcr = 0x0000;
        sci->sptr = 0x0000;
        sci->lsr = 0x0000;
    }
    update_trtime(sci);
    for (i = 0; i < NR_SCI_EVENT; i++) {
        sci->event[i].time = 0;
    }
}

static void sci_event(void *opaque, QEMUChrEvent event)
{
    RSCIState *sci = RSCI(opaque);
    if (event == CHR_EVENT_BREAK) {
        if (IS_SCI(sci)) {
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, FER, 1);
        } else {
            sci->Xsr = FIELD_DP16(sci->Xsr, FSR, BRK, 1);
        }
        sci_irq(sci, ERI);
    }
}

static void rsci_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    RSCIState *sci = RSCI(dev);
    int i;
    int size;

    if (sci->input_freq == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_sci: input-freq property must be set.");
        return;
    }
    if (sci->regsize != 8 && sci->regsize != 16 && sci->regsize != 32) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_sci: Invalid regsize.");
        return;
    }

    size = IS_SCI(sci) ? 16 : 20;
    switch (sci->regsize) {
    case 8:
        size >>= 1;
        break;
    case 32:
        size <<= 1;
        break;
    }
    memory_region_init_io(&sci->memory, OBJECT(sci), &sci_ops,
                          sci, "renesas-sci", size);
    sysbus_init_mmio(d, &sci->memory);
    memory_region_init_alias(&sci->memory_p4, NULL, "renesas-sci-p4",
                             &sci->memory, 0, size);
    sysbus_init_mmio(d, &sci->memory_p4);
    memory_region_init_alias(&sci->memory_a7, NULL, "renesas-sci-a7",
                             &sci->memory, 0, size);
    sysbus_init_mmio(d, &sci->memory_a7);

    for (i = 0; i < SCI_NR_IRQ; i++) {
        sysbus_init_irq(d, &sci->irq[i]);
    }
    sci->event_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sci_timer_event, sci);

    qemu_chr_fe_set_handlers(&sci->chr, can_receive, sci_receive,
                             sci_event, NULL, sci, NULL, true);
    fifo8_create(&sci->rxfifo, SCIF_FIFO_DEPTH);
    sci_register_init(sci);
}

static const VMStateDescription vmstate_rsci = {
    .name = "renesas-sci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static Property rsci_properties[] = {
    DEFINE_PROP_UINT64("input-freq", RSCIState, input_freq, 0),
    DEFINE_PROP_INT32("register-size", RSCIState, regsize, 8),
    DEFINE_PROP_INT32("feature", RSCIState, feature, 0),
    DEFINE_PROP_CHR("chardev", RSCIState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void rsci_init(Object *obj)
{
    RSCIState *sci = RSCI(obj);
    sci->event[RXTOUT].handler = sci_rx_timeout;
    sci->event[RXNEXT].handler = sci_rx_next;
    sci->event[TXEMPTY].handler = sci_tx_empty;
    sci->event[TXEND].handler = sci_tx_end;
}
static void rsci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rsci_realize;
    dc->vmsd = &vmstate_rsci;
    device_class_set_props(dc, rsci_properties);
}

static const TypeInfo rsci_info = {
    .name       = TYPE_RENESAS_SCI,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RSCIState),
    .instance_init = rsci_init,
    .class_init = rsci_class_init,
};

static void rsci_register_types(void)
{
    type_register_static(&rsci_info);
}

type_init(rsci_register_types)
