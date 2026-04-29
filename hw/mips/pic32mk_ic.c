/*
 * PIC32MK Input Capture × 16 (IC1–IC16)
 * Datasheet: DS60001519E, §18
 *
 * Emulation model: full register model with 4-entry FIFO.
 * Two IRQ outputs per instance: capture IRQ and overflow-error IRQ.
 *
 * Capture injection via optional chardev ("ic-events"):
 *   The chardev is receive-side: the host writes 8-byte packets to
 *   inject capture events into the FIFO.
 *
 *   Packet format (8 bytes, little-endian):
 *     [0]   index   — IC instance (1–16); packet ignored if != this instance
 *     [1]   flags   — bit0=error inject, bit1=hi16 valid (32-bit capture pair)
 *     [2–3] val_lo  — 16-bit capture value (low word), LE
 *     [4–5] val_hi  — 16-bit capture value (high word), LE (C32 mode only)
 *     [6–7] reserved
 *
 *   On inject: push val_lo to FIFO (and val_hi if flags.bit1 and C32=1),
 *   set ICBNE, assert capture IRQ (if ICI != 0).  If FIFO full: set ICOV,
 *   assert error IRQ.  Error-inject (flags.bit0=1): set ICOV, assert error IRQ.
 *
 * Register layout within each 0x200-byte block
 * (SET/CLR/INV sub-regs at +4/+8/+C relative to register base):
 *   ICxCON  +0x00  Control: ON, SIDL, FEDGE, C32, ICTMR, ICI, ICOV, ICBNE, ICM
 *   ICxBUF  +0x10  Capture buffer (read-only FIFO pop; write ignored)
 *
 * FIFO: 4 entries × 16-bit.  ICBNE cleared when FIFO empties.
 *       In C32 mode, two consecutive ICxBUF reads assemble one 32-bit value.
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/irq.h"
#include "hw/mips/pic32mk.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"

/*
 * ICM mode names (ICxCON bits 2:0)
 * -----------------------------------------------------------------------
 */
static const char *icm_mode_names[] = {
    [0] = "Disabled",
    [1] = "Every edge",
    [2] = "Every 2nd rising edge",
    [3] = "Every 3rd rising edge",
    [4] = "Every 4th rising edge",
    [5] = "Every 5th rising edge (mode5)",
    [6] = "Every 6th rising edge (mode6)",
    [7] = "Every 7th rising edge (mode7)",
};

/*
 * Global instance table — lets IC1 (the sole chardev owner) dispatch
 * packets to any of the 16 IC instances by index.
 * -----------------------------------------------------------------------
 */

static struct PIC32MKICState *g_ic_instances[17]; /* index 1–16 */

/*
 * Device state
 * -----------------------------------------------------------------------
 */

#define TYPE_PIC32MK_IC  "pic32mk-ic"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKICState, PIC32MK_IC)

#define PIC32MK_IC_FIFO_DEPTH  4

struct PIC32MKICState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    uint32_t con;                          /* ICxCON */
    uint16_t fifo[PIC32MK_IC_FIFO_DEPTH]; /* capture FIFO */
    uint8_t  fifo_head;                    /* read pointer  */
    uint8_t  fifo_tail;                    /* write pointer */
    uint8_t  fifo_count;                   /* entries in use */

    qemu_irq irq_capture;   /* normal capture IRQ → EVIC */
    qemu_irq irq_error;     /* overflow / error IRQ → EVIC */
    uint8_t  index;         /* 1–16 */

    CharFrontend chr;       /* optional chardev for capture injection */

    /* Partial-packet accumulator for chardev receive */
    uint8_t  pkt_buf[8];
    int      pkt_len;
};

/*
 * FIFO helpers
 * -----------------------------------------------------------------------
 */

static void ic_fifo_reset(PIC32MKICState *s)
{
    s->fifo_head  = 0;
    s->fifo_tail  = 0;
    s->fifo_count = 0;
    s->con &= ~(PIC32MK_ICCON_ICBNE | PIC32MK_ICCON_ICOV);
}

static void ic_fifo_push(PIC32MKICState *s, uint16_t val)
{
    if (s->fifo_count == PIC32MK_IC_FIFO_DEPTH) {
        /* Overflow: set ICOV, pulse error IRQ */
        s->con |= PIC32MK_ICCON_ICOV;
        qemu_irq_pulse(s->irq_error);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk_ic: IC%u FIFO overflow\n", s->index);
        return;
    }
    s->fifo[s->fifo_tail] = val;
    s->fifo_tail = (s->fifo_tail + 1) % PIC32MK_IC_FIFO_DEPTH;
    s->fifo_count++;
    s->con |= PIC32MK_ICCON_ICBNE;
}

static uint16_t ic_fifo_pop(PIC32MKICState *s)
{
    if (s->fifo_count == 0) {
        return 0;
    }
    uint16_t val = s->fifo[s->fifo_head];
    s->fifo_head = (s->fifo_head + 1) % PIC32MK_IC_FIFO_DEPTH;
    s->fifo_count--;
    if (s->fifo_count == 0) {
        s->con &= ~PIC32MK_ICCON_ICBNE;
    }
    return val;
}

/*
 * Capture injection — called from chardev receive handler
 * -----------------------------------------------------------------------
 */

static void ic_inject_capture(PIC32MKICState *s, uint8_t flags,
                               uint16_t val_lo, uint16_t val_hi)
{
    if (!(s->con & PIC32MK_ICCON_ON)) {
        return;  /* IC not enabled — ignore */
    }

    if (flags & 1) {
        /* Error inject: set ICOV, pulse error IRQ */
        s->con |= PIC32MK_ICCON_ICOV;
        qemu_irq_pulse(s->irq_error);
        return;
    }

    ic_fifo_push(s, val_lo);
    if ((flags & 2) && (s->con & PIC32MK_ICCON_C32)) {
        ic_fifo_push(s, val_hi);
    }

    /*
     * ICI<1:0>: 00=every 1st, 01=every 2nd, 10=every 3rd, 11=every 4th capture.
     * Simplified model: always fire on every capture (ICI threshold ignored).
      */
    if (s->fifo_count > 0) {
        qemu_irq_pulse(s->irq_capture);
    }
}

/*
 * Chardev receive handler
 * -----------------------------------------------------------------------
 */

static int ic_chr_can_receive(void *opaque)
{
    /* Accept up to one full packet at a time */
    return 8;
}

static void ic_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    PIC32MKICState *s = opaque;
    int i = 0;

    while (i < size) {
        int need = 8 - s->pkt_len;
        int avail = size - i;
        int copy = (avail < need) ? avail : need;

        memcpy(s->pkt_buf + s->pkt_len, buf + i, copy);
        s->pkt_len += copy;
        i += copy;

        if (s->pkt_len == 8) {
            uint8_t  inst    = s->pkt_buf[0];
            uint8_t  flags   = s->pkt_buf[1];
            uint16_t val_lo, val_hi;
            memcpy(&val_lo, &s->pkt_buf[2], 2);
            memcpy(&val_hi, &s->pkt_buf[4], 2);
            val_lo = le16_to_cpu(val_lo);
            val_hi = le16_to_cpu(val_hi);
            s->pkt_len = 0;

            /* Route to target instance via global table */
            if (inst >= 1 && inst <= 16 && g_ic_instances[inst]) {
                ic_inject_capture(g_ic_instances[inst], flags, val_lo, val_hi);
            }
        }
    }
}

/*
 * MMIO helpers
 * -----------------------------------------------------------------------
 */

/* PIC32MK: base+0=REG, +4=CLR, +8=SET, +0xC=INV */
static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0:
        *reg  = val;
        break;
    case 4:
        *reg &= ~val;
        break;
    case 8:
        *reg |= val;
        break;
    case 12:
        *reg ^= val;
        break;
    }
}

/*
 * MMIO read / write
 * -----------------------------------------------------------------------
 */

static uint64_t ic_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKICState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xF;

    switch (base) {
    case PIC32MK_ICxCON:
        return s->con;
    case PIC32MK_ICxBUF:
        return ic_fifo_pop(s);
    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_ic: IC%u unimplemented read @ 0x%04"
                      HWADDR_PRIx "\n", s->index, addr);
        return 0;
    }
}

static void ic_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKICState *s = opaque;
    int    sub  = (int)(addr & 0xF);
    hwaddr base = addr & ~(hwaddr)0xF;

    switch (base) {
    case PIC32MK_ICxCON: {
        bool was_on = !!(s->con & PIC32MK_ICCON_ON);
        apply_sci(&s->con, (uint32_t)val, sub);
        bool now_on = !!(s->con & PIC32MK_ICCON_ON);

        if (!was_on && now_on) {
            /* ON 0→1: reset FIFO, log enable */
            ic_fifo_reset(s);
            uint32_t icm  = (s->con & PIC32MK_ICCON_ICM_MASK);
            uint32_t ici  = (s->con & PIC32MK_ICCON_ICI_MASK) >> PIC32MK_ICCON_ICI_SHIFT;
            bool     c32  = !!(s->con & PIC32MK_ICCON_C32);
            bool     ictmr = !!(s->con & PIC32MK_ICCON_ICTMR);
            bool     fedge = !!(s->con & PIC32MK_ICCON_FEDGE);
            qemu_log("pic32mk_ic: IC%u enabled — mode=%s, ICI=%u, "
                     "C32=%u, ICTMR=%u, FEDGE=%u\n",
                     s->index, icm_mode_names[icm], ici, c32, ictmr, fedge);
        } else if (was_on && !now_on) {
            qemu_log("pic32mk_ic: IC%u disabled\n", s->index);
        }
        break;
    }
    case PIC32MK_ICxBUF:
        /* ICxBUF is read-only; writes are ignored */
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_ic: IC%u unimplemented write @ 0x%04"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                      s->index, addr, val);
        break;
    }
}

static const MemoryRegionOps ic_ops = {
    .read       = ic_read,
    .write      = ic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_ic_reset(DeviceState *dev)
{
    PIC32MKICState *s = PIC32MK_IC(dev);
    s->con     = 0;
    s->pkt_len = 0;
    ic_fifo_reset(s);
}

static void pic32mk_ic_realize(DeviceState *dev, Error **errp)
{
    PIC32MKICState *s = PIC32MK_IC(dev);

    /* Register in global table so the chardev dispatcher can find us */
    if (s->index >= 1 && s->index <= 16) {
        g_ic_instances[s->index] = s;
    }

    /* Only IC1 owns the chardev — it dispatches packets to all instances */
    if (qemu_chr_fe_backend_connected(&s->chr)) {
        qemu_chr_fe_set_handlers(&s->chr,
                                 ic_chr_can_receive,
                                 ic_chr_receive,
                                 NULL, NULL, s, NULL, true);
    }
}

static void pic32mk_ic_init(Object *obj)
{
    PIC32MKICState *s = PIC32MK_IC(obj);

    memory_region_init_io(&s->mr, obj, &ic_ops, s,
                          TYPE_PIC32MK_IC, PIC32MK_IC_BLOCK_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_capture);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_error);
}

static Property pic32mk_ic_properties[] = {
    DEFINE_PROP_UINT8("index",   PIC32MKICState, index, 1),
    DEFINE_PROP_CHR("chardev",   PIC32MKICState, chr),
};

static void pic32mk_ic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = pic32mk_ic_realize;
    device_class_set_legacy_reset(dc, pic32mk_ic_reset);
    device_class_set_props(dc, pic32mk_ic_properties);
}

static const TypeInfo pic32mk_ic_info = {
    .name          = TYPE_PIC32MK_IC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKICState),
    .instance_init = pic32mk_ic_init,
    .class_init    = pic32mk_ic_class_init,
};

static void pic32mk_ic_register_types(void)
{
    type_register_static(&pic32mk_ic_info);
}

type_init(pic32mk_ic_register_types)
