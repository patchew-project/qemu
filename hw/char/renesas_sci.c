/*
 * Renesas Serial Communication Interface
 *
 * Copyright (c) 2019 Yoshinori Sato
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
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/char/renesas_sci.h"
#include "qemu/error-report.h"

#define freq_to_ns(freq) (1000000000LL / freq)

static int can_receive(void *opaque)
{
    RSCIState *sci = RSCI(opaque);
    if (sci->rx_next > qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
        return 0;
    } else {
        return sci->scr & 0x10;
    }
}

static void receive(void *opaque, const uint8_t *buf, int size)
{
    RSCIState *sci = RSCI(opaque);
    sci->rdr = buf[0];
    if (sci->ssr & 0x40 || size > 1) {
        sci->ssr |= 0x20;
        if (sci->scr & 0x40) {
            qemu_set_irq(sci->irq[ERI], 1);
        }
    } else {
        sci->ssr |= 0x40;
        sci->rx_next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + sci->trtime;
        if (sci->scr & 0x40) {
            qemu_set_irq(sci->irq[RXI], 1);
            qemu_set_irq(sci->irq[RXI], 0);
        }
    }
}

static void send_byte(RSCIState *sci)
{
    if (qemu_chr_fe_backend_connected(&sci->chr)) {
        qemu_chr_fe_write_all(&sci->chr, &sci->tdr, 1);
    }
    timer_mod(sci->timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + sci->trtime);
    sci->ssr &= ~0x04;
    sci->ssr |= 0x80;
    qemu_set_irq(sci->irq[TEI], 0);
    if (sci->scr & 0x80) {
        qemu_set_irq(sci->irq[TXI], 1);
        qemu_set_irq(sci->irq[TXI], 0);
    }
}

static void txend(void *opaque)
{
    RSCIState *sci = RSCI(opaque);
    if ((sci->ssr & 0x80) == 0) {
        send_byte(sci);
    } else {
        sci->ssr |= 0x04;
        if (sci->scr & 0x04) {
            qemu_set_irq(sci->irq[TEI], 1);
        }
    }
}

static void update_trtime(RSCIState *sci)
{
    static const int div[] = {1, 4, 16, 64};
    int w;

    w = (sci->smr & 0x40) ? 7 : 8;      /* CHR */
    w += (sci->smr >> 5) & 1;           /* PE */
    w += (sci->smr & 0x08) ? 2 : 1;     /* STOP */
    sci->trtime = w * freq_to_ns(sci->input_freq) *
        32 * div[sci->smr & 0x03] * sci->brr;
}

static void sci_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    hwaddr offset = addr & 0x07;
    RSCIState *sci = RSCI(opaque);
    int error = 0;

    switch (offset) {
    case 0: /* SMR */
        if ((sci->scr & 0x30) == 0) {
            sci->smr = val;
            update_trtime(sci);
        }
        break;
    case 1: /* BRR */
        if ((sci->scr & 0x30) == 0) {
            sci->brr = val;
            update_trtime(sci);
        }
        break;
    case 2: /* SCR */
        sci->scr = val;
        if (sci->scr & 0x20) {
            sci->ssr |= 0x84;
            qemu_set_irq(sci->irq[TXI], 1);
            qemu_set_irq(sci->irq[TXI], 0);
        }
        if ((sci->scr & 0x04) == 0) {
            qemu_set_irq(sci->irq[TEI], 0);
        }
        if ((sci->scr & 0x40) == 0) {
            qemu_set_irq(sci->irq[ERI], 0);
        }
        break;
    case 3: /* TDR */
        sci->tdr = val;
        if (sci->ssr & 0x04) {
            send_byte(sci);
        } else{
            sci->ssr &= ~0x80;
        }
        break;
    case 4: /* SSR */
        sci->ssr &= ~0x38 | (val & 0x38);
        if (((sci->read_ssr & 0x38) ^ (sci->ssr & 0x38)) &&
            (sci->ssr & 0x38) == 0) {
            qemu_set_irq(sci->irq[ERI], 0);
        }
        break;
    case 5: /* RDR */
        error = 1; break;
    case 6: /* SCMR */
        sci->scmr = val; break;
    case 7: /* SEMR */
        sci->semr = val; break;
    }

    if (error) {
        error_report("rsci: unsupported write request to %08lx", addr);
    }
}

static uint64_t sci_read(void *opaque, hwaddr addr, unsigned size)
{
    hwaddr offset = addr & 0x07;
    RSCIState *sci = RSCI(opaque);
    int error = 0;
    switch (offset) {
    case 0: /* SMR */
        return sci->smr;
    case 1: /* BRR */
        return sci->brr;
    case 2: /* SCR */
        return sci->scr;
    case 3: /* TDR */
        return sci->tdr;
    case 4: /* SSR */
        sci->read_ssr = sci->ssr;
        return sci->ssr;
    case 5: /* RDR */
        sci->ssr &= ~0x40;
        return sci->rdr;
    case 6: /* SCMR */
        return sci->scmr;
    case 7: /* SEMR */
        return sci->semr;
    }

    if (error) {
        error_report("rsci: unsupported write request to %08lx", addr);
    }
    return -1;
}

static const MemoryRegionOps sci_ops = {
    .write = sci_write,
    .read  = sci_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void rsci_reset(DeviceState *dev)
{
    RSCIState *sci = RSCI(dev);
    sci->smr = sci->scr = 0x00;
    sci->brr = 0xff;
    sci->tdr = 0xff;
    sci->rdr = 0x00;
    sci->ssr = 0x84;
    sci->scmr = 0x00;
    sci->semr = 0x00;
    sci->rx_next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void sci_event(void *opaque, int event)
{
    RSCIState *sci = RSCI(opaque);
    if (event == CHR_EVENT_BREAK) {
        sci->ssr |= 0x10;
        if (sci->scr & 0x40) {
            qemu_set_irq(sci->irq[ERI], 1);
        }
    }
}

static void rsci_realize(DeviceState *dev, Error **errp)
{
    RSCIState *sci = RSCI(dev);

    qemu_chr_fe_set_handlers(&sci->chr, can_receive, receive,
                             sci_event, NULL, sci, NULL, true);
}

static void rsci_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RSCIState *sci = RSCI(obj);
    int i;

    memory_region_init_io(&sci->memory, OBJECT(sci), &sci_ops,
                          sci, "renesas-sci", 0x8);
    sysbus_init_mmio(d, &sci->memory);

    for (i = 0; i < 4; i++) {
        sysbus_init_irq(d, &sci->irq[i]);
    }
    sci->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, txend, sci);
}

static const VMStateDescription vmstate_rcmt = {
    .name = "renesas-sci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static Property rsci_properties[] = {
    DEFINE_PROP_UINT64("input-freq", RSCIState, input_freq, 0),
    DEFINE_PROP_CHR("chardev", RSCIState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void rsci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rsci_realize;
    dc->props = rsci_properties;
    dc->vmsd = &vmstate_rcmt;
    dc->reset = rsci_reset;
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
