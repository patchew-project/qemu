/*
 * Renesas Serial Communication Interface
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 *            (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2020 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include "hw/qdev-properties-system.h"
#include "hw/char/renesas_sci.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"

/*
 * SCI register map
 * SCI(a) register size all 8bit.
 */
REG32(SMR, 0) /* 8bit */
  FIELD(SMR, CKS,  0, 2)
  FIELD(SMR, MP,   2, 1)
  FIELD(SMR, STOP, 3, 1)
  FIELD(SMR, PM,   4, 1)
  FIELD(SMR, PE,   5, 1)
  FIELD(SMR, CHR,  6, 1)
  FIELD(SMR, CM,   7, 1)
REG32(BRR, 4) /* 8bit */
REG32(SCR, 8)
  FIELD(SCR, CKE, 0, 2)
  FIELD(SCR, TEIE, 2, 1)
  FIELD(SCR, MPIE, 3, 1)
  FIELD(SCR, REIE, 3, 1)
  FIELD(SCR, RE,   4, 1)
  FIELD(SCR, TE,   5, 1)
  FIELD(SCR, RIE,  6, 1)
  FIELD(SCR, TIE,  7, 1)
REG32(TDR, 12) /* 8bit */
REG32(SSR, 16) /* 8bit */
  FIELD(SSR, MPBT, 0, 1)
  FIELD(SSR, MPB,  1, 1)
  FIELD(SSR, TEND, 2, 1)
  FIELD(SSR, ERR, 3, 3)
    FIELD(SSR, PER,  3, 1)
    FIELD(SSR, FER,  4, 1)
    FIELD(SSR, ORER, 5, 1)
  FIELD(SSR, RDRF, 6, 1)
  FIELD(SSR, TDRE, 7, 1)
REG32(RDR, 20) /* 8bit */
REG32(SCMR, 24) /* 8bit */
  FIELD(SCMR, SMIF, 0, 1)
  FIELD(SCMR, SINV, 2, 1)
  FIELD(SCMR, SDIR, 3, 1)
  FIELD(SCMR, BCP2, 7, 1)
REG8(SEMR, 28)
  FIELD(SEMR, ACS0, 0, 1)
  FIELD(SEMR, ABCS, 4, 1)

#define SCIF_FIFO_DEPTH 16

static int sci_can_receive(void *opaque)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(opaque);
    if (FIELD_EX16(sci->scr, SCR, RE)) {
        return fifo8_num_free(&sci->rxfifo);
    } else {
        /* Receiver disabled. can't receive. */
        return 0;
    }
}

static void update_expire_time(RenesasSCIBaseState *sci)
{
    int64_t next;
    int i;

    next = INT64_MAX;
    for (i = 0; i < NR_SCI_EVENT; i++) {
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

static void update_event_time(RenesasSCIBaseState *sci, int evt, int64_t t)
{
    if (t > 0) {
        t +=  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        sci->event[evt].time = t;
    } else {
        sci->event[evt].time = 0;
    }
    update_expire_time(sci);
}

static void sci_receive(void *opaque, const uint8_t *buf, int size)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(opaque);
    RenesasSCIBaseClass *rc = RENESAS_SCI_BASE_GET_CLASS(sci);
    fifo8_push_all(&sci->rxfifo, buf, size);
    if (FIELD_EX16(sci->scr, SCR, RE)) {
        if (sci->event[RXNEXT].time == 0) {
            /* Receiver wake up */
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, RDRF, 1);
            rc->irq_fn(sci, RXI);
            update_event_time(sci, RXNEXT, sci->trtime);
        }
    }
}

static void scia_irq(RenesasSCIBaseState *sci, int req)
{
    int irq = 0;
    int rie;
    int tie;

    rie = FIELD_EX16(sci->scr, SCR, RIE);
    tie = FIELD_EX16(sci->scr, SCR, TIE);
    switch (req) {
    case ERI:
        irq = (FIELD_EX16(sci->Xsr, SSR, ERR) != 0) && rie;
        break;
    case RXI:
        irq = FIELD_EX16(sci->Xsr, SSR, RDRF) && rie;
        break;
    case TXI:
        irq = FIELD_EX16(sci->Xsr, SSR, TDRE) && tie;
        break;
    case BRI_TEI:
        irq = FIELD_EX16(sci->Xsr, SSR, TEND) &&
            FIELD_EX16(sci->scr, SCR, TEIE);
        break;
    }
    if (req == RXI || req == TXI) {
        if (irq) {
            qemu_irq_pulse(sci->irq[req]);
        }
    } else {
        qemu_set_irq(sci->irq[req], irq);
    }
}

static void sci_send_byte(RenesasSCIBaseState *sci)
{
    if (qemu_chr_fe_backend_connected(&sci->chr)) {
        qemu_chr_fe_write_all(&sci->chr, &sci->tdr, 1);
    }
    sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TEND, 0);
    sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TDRE, 1);
}

static int64_t sci_rx_next(RenesasSCIBaseState *sci)
{
    RenesasSCIBaseClass *rc = RENESAS_SCI_BASE_GET_CLASS(sci);
    if (!fifo8_is_empty(&sci->rxfifo)) {
        /* have receive charactor */
        if (FIELD_EX16(sci->Xsr, SSR, RDRF)) {
            /* Receiver overrun */
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, ORER, 1);
            rc->irq_fn(sci, ERI);
            return 0;
        }
        sci->Xsr = FIELD_DP16(sci->Xsr, SSR, RDRF, 1);
        rc->irq_fn(sci, RXI);
        /* next receive time */
        return sci->trtime;
    } else {
        /* No receive charactor. move to idle state */
        return 0;
    }
}

static int64_t sci_tx_empty(RenesasSCIBaseState *sci)
{
    int64_t ret = 0;
    RenesasSCIBaseClass *rc = RENESAS_SCI_BASE_GET_CLASS(sci);
    if (!FIELD_EX16(sci->Xsr, SSR, TDRE)) {
        sci_send_byte(sci);
        ret = sci->trtime;
        rc->irq_fn(sci, TXI);
    } else {
        sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TEND, 1);
        rc->irq_fn(sci, BRI_TEI);
    }
    return ret;
}

static void sci_timer_event(void *opaque)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(opaque);
    int64_t now, t;
    int i;

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    for (i = 0; i < NR_SCI_EVENT; i++) {
        if (sci->event[i].time > 0 && sci->event[i].time <= now) {
            t = sci->event[i].handler(sci);
            if (t > 0) {
                sci->event[i].time = now + t;
            } else {
                /* No next event */
                sci->event[i].time = 0;
            }
        }
    }
    update_expire_time(sci);
}

static int scia_divrate(RenesasSCIBaseState *sci)
{
    /*
     * SEMR.ABCS = 0 -> 32
     * SEMR.ABCS = 1 -> 16
     */
    RenesasSCIAState *scia = RENESAS_SCIA(sci);
    return 16 * (2 - FIELD_EX8(scia->semr, SEMR, ABCS));
}

static void update_trtime(RenesasSCIBaseState *sci)
{
    RenesasSCIBaseClass *rc = RENESAS_SCI_BASE_GET_CLASS(sci);
    int cks = 1 << (2 * FIELD_EX16(sci->smr, SMR, CKS));
    if (sci->input_freq > 0) {
        /* x bit transmit time (divrate * brr) / base freq */
        sci->etu = rc->divrate(sci) * cks;
        sci->etu *= sci->brr + 1;
        sci->etu *= NANOSECONDS_PER_SECOND;
        sci->etu /= sci->input_freq;

        /* char per bits */
        sci->trtime = 8 - FIELD_EX16(sci->smr, SMR, CHR);
        sci->trtime += FIELD_EX16(sci->smr, SMR, PE);
        sci->trtime += FIELD_EX16(sci->smr, SMR, STOP) + 1 + 1;
        sci->trtime *= sci->etu;
    }
}

#define IS_TR_ENABLED(scr) \
    (FIELD_EX16(scr, SCR, TE) || FIELD_EX16(scr, SCR, RE))

static hwaddr map_address(RenesasSCIBaseState *sci, hwaddr addr)
{
    return addr << (2 - sci->regshift);
}

static void sci_common_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(opaque);
    RenesasSCIBaseClass *rc = RENESAS_SCI_BASE_GET_CLASS(opaque);
    switch (addr) {
    case A_SCR:
        sci->scr = val;
        if (FIELD_EX16(sci->scr, SCR, TE)) {
            /* Transmitter enable */
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TDRE, 1);
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TEND, 1);
            rc->irq_fn(sci, TXI);
            rc->irq_fn(sci, BRI_TEI);
        } else {
            /* Transmitter disable  */
            update_event_time(sci, TXEND, 0);
            update_event_time(sci, TXEMPTY, 0);
        }
        break;
    case A_SMR:
        sci->smr = val;
        update_trtime(sci);
        break;
    case A_BRR:
        sci->brr = val;
        update_trtime(sci);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_sci: Register 0x%" HWADDR_PRIX
                      " not implemented\n", addr);
    }
}

static void scia_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(opaque);
    RenesasSCIAState *scia = RENESAS_SCIA(opaque);

    addr = map_address(sci, addr);
    switch (addr) {
    case A_SMR:
        if (IS_TR_ENABLED(sci->scr)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "reneas_sci: SMR write protected.\n");
        } else {
            sci_common_write(sci, addr, val, size);
        }
        break;
    case A_BRR:
        if (IS_TR_ENABLED(sci->scr)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "reneas_sci: BRR write protected.\n");
            break;
        } else {
            sci_common_write(sci, addr, val, size);
        }
        break;
    case A_TDR:
        sci->tdr = val;
        if (FIELD_EX16(sci->Xsr, SSR, TEND)) {
            /* Transmitter wakeup */
            update_event_time(sci, TXEMPTY, sci->trtime);
            sci_send_byte(sci);
        } else {
            sci->Xsr = FIELD_DP16(sci->Xsr, SSR, TDRE, 0);
        }
        /* Clear TEI */
        scia_irq(sci, BRI_TEI);
        break;
    case A_SSR:
        /* SSR.MBP and SSR.TEND is read only */
        val = FIELD_DP16(val, SSR, MPB, 1);
        val = FIELD_DP16(val, SSR, TEND, 1);
        /* SSR.RDRF and SSR.TDRE can write 1 */
        if (FIELD_EX16(val, SSR, RDRF) == 0 ||
            FIELD_EX16(val, SSR, TDRE) == 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "reneas_sci: SSR invalid write value %02lux.\n", val);
        }
        val = FIELD_DP16(val, SSR, RDRF, 1);
        val = FIELD_DP16(val, SSR, TDRE, 1);
        /* SSR.MBP and SSR.TEND is read only */
        val = FIELD_DP16(val, SSR, MPB, 1);
        val = FIELD_DP16(val, SSR, TEND, 1);
        /* SSR.PER, SSR.FER and SSR.ORER can write only 0 */
        sci->Xsr &= val;
        /* SSR.MPBT can write any value */
        sci->Xsr = FIELD_DP16(RENESAS_SCI_BASE(sci)->Xsr, SSR, MPBT,
                              FIELD_EX16(val, SSR, MPBT));
        /* Clear ERI */
        scia_irq(sci, ERI);
        break;
    case A_SCMR:
        scia->scmr = val;
        break;
    case A_SEMR:
        scia->semr = val;
        break;
    default:
        sci_common_write(sci, addr, val, size);
        break;
    }
}

static uint64_t sci_common_read(void *opaque, hwaddr addr, unsigned size)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(opaque);
    switch (addr) {
    case A_SMR:
        return sci->smr;
    case A_BRR:
        return sci->brr;
    case A_SCR:
        return sci->scr;
    case A_SSR:
        return sci->Xsr;
    case A_TDR:
        return sci->tdr;
    case A_RDR:
        if (fifo8_num_used(&sci->rxfifo) > 0) {
            return fifo8_pop(&sci->rxfifo);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "renesas_sci: Receiver underrun.");
            return 0xff;
        }
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_sci: Register 0x%" HWADDR_PRIX
                      " not implemented.\n", addr);
    }
    return UINT64_MAX;
}

static uint64_t scia_read(void *opaque, hwaddr addr, unsigned size)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(opaque);
    RenesasSCIAState *scia = RENESAS_SCIA(opaque);

    addr = map_address(sci, addr);
    switch (addr) {
    case A_RDR:
        sci->Xsr = FIELD_DP16(sci->Xsr, SSR, RDRF, 0);
        return sci_common_read(sci, addr, size);
    case A_SCMR:
        return scia->scmr;
    default:
        return sci_common_read(sci, addr, size);
    }
    return UINT64_MAX;
}

static void rsci_common_init(Object *obj)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(obj);
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    int i;

    for (i = 0; i < SCI_NR_IRQ; i++) {
        sysbus_init_irq(d, &sci->irq[i]);
    }
    fifo8_create(&sci->rxfifo, SCIF_FIFO_DEPTH);
    sci->event_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sci_timer_event, sci);
}

static void sci_event(void *opaque, QEMUChrEvent event)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(opaque);
    RenesasSCIBaseClass *rc = RENESAS_SCI_BASE_GET_CLASS(sci);
    if (event == CHR_EVENT_BREAK) {
        sci->Xsr = FIELD_DP16(sci->Xsr, SSR, FER, 1);
        rc->irq_fn(sci, BRI_TEI);
    }
}

static void rsci_common_realize(DeviceState *dev, Error **errp)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(dev);
    int r;

    r = sci->regshift;
    if ((r % 8) != 0 || ((r / 8) >> 1) > 2) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_sci: Invalid register size.");
        return;
    }
    sci->regshift = (r / 8) >> 1;
    sci->smr = sci->scr = 0x00;
    sci->brr = 0xff;
    sci->tdr = 0xff;
    sci->Xsr = 0x84;
    update_trtime(sci);

}

static void register_mmio(RenesasSCIBaseState *sci, int size)
{
    SysBusDevice *d = SYS_BUS_DEVICE(sci);
    RenesasSCIBaseClass *rc = RENESAS_SCI_BASE_GET_CLASS(sci);

    memory_region_init_io(&sci->memory, OBJECT(sci), rc->ops,
                          sci, "renesas-sci", size);
    sysbus_init_mmio(d, &sci->memory);
}

static void rscia_realize(DeviceState *dev, Error **errp)
{
    RenesasSCIAState *sci = RENESAS_SCIA(dev);
    RenesasSCIBaseState *common = RENESAS_SCI_BASE(dev);

    rsci_common_realize(dev, errp);

    register_mmio(common, 8 * (1 << common->regshift));
    qemu_chr_fe_set_handlers(&common->chr, sci_can_receive, sci_receive,
                             sci_event, NULL, sci, NULL, true);

    sci->scmr = 0x00;
    sci->semr = 0x00;
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
    DEFINE_PROP_UINT64("input-freq", RenesasSCIBaseState, input_freq, 0),
    DEFINE_PROP_INT32("register-size", RenesasSCIBaseState,
                      regshift, 0),
    DEFINE_PROP_UINT32("unit", RenesasSCIBaseState, unit, 0),
    DEFINE_PROP_CHR("chardev", RenesasSCIBaseState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void rsci_init(Object *obj)
{
    RenesasSCIBaseState *sci = RENESAS_SCI_BASE(obj);
    sci->event[RXNEXT].handler = sci_rx_next;
    sci->event[TXEMPTY].handler = sci_tx_empty;
}

static void rsci_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rsci;
    device_class_set_props(dc, rsci_properties);
}

static const MemoryRegionOps scia_ops = {
    .read = scia_read,
    .write = scia_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void rscia_class_init(ObjectClass *klass, void *data)
{
    RenesasSCIBaseClass *comm_rc = RENESAS_SCI_BASE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    comm_rc->ops = &scia_ops;
    comm_rc->irq_fn = scia_irq;
    comm_rc->divrate = scia_divrate;

    dc->realize = rscia_realize;
}

static const TypeInfo renesas_sci_info[] = {
    {
        .name       = TYPE_RENESAS_SCI_BASE,
        .parent     = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(RenesasSCIBaseState),
        .instance_init = rsci_common_init,
        .class_init = rsci_common_class_init,
        .class_size = sizeof(RenesasSCIBaseClass),
        .abstract = true,
    },
    {
        .name       = TYPE_RENESAS_SCIA,
        .parent     = TYPE_RENESAS_SCI_BASE,
        .instance_size = sizeof(RenesasSCIAState),
        .instance_init = rsci_init,
        .class_init = rscia_class_init,
        .class_size = sizeof(RenesasSCIAClass),
    },
};

DEFINE_TYPES(renesas_sci_info)
