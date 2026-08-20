/*
 * TI OMAP on-chip I2C controller.  Only "new I2C" mode supported.
 *
 * Copyright (C) 2007 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/arm/omap.h"
#include "hw/core/sysbus.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

struct OMAPI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq drq[2];
    I2CBus *bus;

    uint8_t revision;
    uint8_t mmio_version;
    void *iclk;
    void *fclk;

    uint8_t mask;
    uint16_t stat;
    uint16_t dma;
    uint16_t count;
    int count_cur;
    uint32_t fifo;
    int rxlen;
    int txlen;
    uint16_t control;
    uint16_t addr[2];
    uint8_t divider;
    uint8_t times[2];
    uint16_t test;
};

#define OMAP2_INTR_REV  0x34
#define OMAP2_GC_REV    0x34

/*
 * MMIO register layout selector.  The classic OMAP1/OMAP2 "IP V1" map is the
 * default and is what every existing OMAP board relies on.  "IP V2" is the
 * OMAP4-and-later layout ("ti,omap4-i2c" / "ti,am64-i2c"), which relocates the
 * registers and adds the IRQSTATUS_RAW / IRQENABLE_SET / IRQENABLE_CLR set.
 * The transfer/reset/NACK engine is shared; only the address decode differs.
 */
#define OMAP_I2C_MMIO_V1  0
#define OMAP_I2C_MMIO_V2  2

/* IP V2 register offsets, as used from OMAP4 onwards. */
#define OMAP_I2C_V2_REVNB_LO       0x00
#define OMAP_I2C_V2_REVNB_HI       0x04
#define OMAP_I2C_V2_SYSC           0x10
#define OMAP_I2C_V2_IRQSTATUS_RAW  0x24
#define OMAP_I2C_V2_IRQSTATUS      0x28
#define OMAP_I2C_V2_IRQENABLE_SET  0x2c
#define OMAP_I2C_V2_IRQENABLE_CLR  0x30
#define OMAP_I2C_V2_WE             0x34
#define OMAP_I2C_V2_SYSS           0x90
#define OMAP_I2C_V2_BUF            0x94
#define OMAP_I2C_V2_CNT            0x98
#define OMAP_I2C_V2_DATA           0x9c
#define OMAP_I2C_V2_CON            0xa4
#define OMAP_I2C_V2_OA             0xa8
#define OMAP_I2C_V2_SA             0xac
#define OMAP_I2C_V2_PSC            0xb0
#define OMAP_I2C_V2_SCLL           0xb4
#define OMAP_I2C_V2_SCLH           0xb8
#define OMAP_I2C_V2_SYSTEST        0xbc
#define OMAP_I2C_V2_BUFSTAT        0xc0

/*
 * Translate an IP-V2 offset for a register whose semantics are identical to
 * the V1 model into the V1 offset the shared read/write switch decodes.
 * Returns -1 for offsets that have no direct V1 equivalent (those are handled
 * inline by the V2 front-end).
 */
static int omap_i2c_v2_to_v1(int offset)
{
    switch (offset) {
    case OMAP_I2C_V2_BUF:     return 0x14;
    case OMAP_I2C_V2_CNT:     return 0x18;
    /* DATA (0x9c) is handled inline byte-wise by the V2 front-end. */
    case OMAP_I2C_V2_CON:     return 0x24;
    case OMAP_I2C_V2_OA:      return 0x28;
    case OMAP_I2C_V2_SA:      return 0x2c;
    case OMAP_I2C_V2_PSC:     return 0x30;
    case OMAP_I2C_V2_SCLL:    return 0x34;
    case OMAP_I2C_V2_SCLH:    return 0x38;
    case OMAP_I2C_V2_SYSTEST: return 0x3c;
    default:                  return -1;
    }
}

static void omap_i2c_interrupts_update(OMAPI2CState *s)
{
    qemu_set_irq(s->irq, s->stat & s->mask);
    if ((s->dma >> 15) & 1)                             /* RDMA_EN */
        qemu_set_irq(s->drq[0], (s->stat >> 3) & 1);    /* RRDY */
    if ((s->dma >> 7) & 1)                              /* XDMA_EN */
        qemu_set_irq(s->drq[1], (s->stat >> 4) & 1);    /* XRDY */
}

static void omap_i2c_fifo_run(OMAPI2CState *s)
{
    int ack = 1;

    if (!i2c_bus_busy(s->bus))
        return;

    if ((s->control >> 2) & 1) {                /* RM */
        if ((s->control >> 1) & 1) {            /* STP */
            i2c_end_transfer(s->bus);
            s->control &= ~(1 << 1);            /* STP */
            s->count_cur = s->count;
            s->txlen = 0;
        } else if ((s->control >> 9) & 1) {     /* TRX */
            while (ack && s->txlen)
                ack = (i2c_send(s->bus,
                                        (s->fifo >> ((-- s->txlen) << 3)) &
                                        0xff) >= 0);
            s->stat |= 1 << 4;                  /* XRDY */
        } else {
            while (s->rxlen < 4)
                s->fifo |= i2c_recv(s->bus) << ((s->rxlen ++) << 3);
            s->stat |= 1 << 3;                  /* RRDY */
        }
    } else {
        if ((s->control >> 9) & 1) {            /* TRX */
            while (ack && s->count_cur && s->txlen) {
                ack = (i2c_send(s->bus,
                                        (s->fifo >> ((-- s->txlen) << 3)) &
                                        0xff) >= 0);
                s->count_cur --;
            }
            if (ack && s->count_cur)
                s->stat |= 1 << 4;              /* XRDY */
            else
                s->stat &= ~(1 << 4);           /* XRDY */
            if (!s->count_cur) {
                s->stat |= 1 << 2;              /* ARDY */
                s->control &= ~(1 << 10);       /* MST */
            }
        } else {
            while (s->count_cur && s->rxlen < 4) {
                s->fifo |= i2c_recv(s->bus) << ((s->rxlen ++) << 3);
                s->count_cur --;
            }
            if (s->rxlen)
                s->stat |= 1 << 3;              /* RRDY */
            else
                s->stat &= ~(1 << 3);           /* RRDY */
        }
        if (!s->count_cur) {
            if ((s->control >> 1) & 1) {        /* STP */
                i2c_end_transfer(s->bus);
                s->control &= ~(1 << 1);        /* STP */
                s->count_cur = s->count;
                s->txlen = 0;
            } else {
                s->stat |= 1 << 2;              /* ARDY */
                s->control &= ~(1 << 10);       /* MST */
            }
        }
    }

    s->stat |= (!ack) << 1;                     /* NACK */
    if (!ack)
        s->control &= ~(1 << 1);                /* STP */
}

static void omap_i2c_reset(DeviceState *dev)
{
    OMAPI2CState *s = OMAP_I2C(dev);

    s->mask = 0;
    s->stat = 0;
    s->dma = 0;
    s->count = 0;
    s->count_cur = 0;
    s->fifo = 0;
    s->rxlen = 0;
    s->txlen = 0;
    s->control = 0;
    s->addr[0] = 0;
    s->addr[1] = 0;
    s->divider = 0;
    s->times[0] = 0;
    s->times[1] = 0;
    s->test = 0;
}

static uint32_t omap_i2c_read(void *opaque, hwaddr addr)
{
    OMAPI2CState *s = opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t ret;

    if (s->mmio_version == OMAP_I2C_MMIO_V2) {
        switch (offset) {
        case OMAP_I2C_V2_REVNB_LO:
            return s->revision;
        case OMAP_I2C_V2_REVNB_HI:
            return 0;
        case OMAP_I2C_V2_SYSC:
            return 0;
        case OMAP_I2C_V2_IRQSTATUS_RAW:
        case OMAP_I2C_V2_IRQSTATUS:     /* STAT mirrors IRQSTATUS_RAW */
            return s->stat | (i2c_bus_busy(s->bus) << 12);
        case OMAP_I2C_V2_IRQENABLE_SET:
        case OMAP_I2C_V2_IRQENABLE_CLR:
            return s->mask;
        case OMAP_I2C_V2_WE:
            return 0;
        case OMAP_I2C_V2_SYSS:
            /* reset is instantaneous in the model: RDONE always reads set */
            return 1;
        case OMAP_I2C_V2_BUFSTAT:
            return 0;
        case OMAP_I2C_V2_DATA: {
            /*
             * The OMAP4/AM64x driver accesses the DATA register one byte per
             * MMIO access (readw of a single byte), unlike the classic V1
             * 16-bit FIFO convention.  Pop exactly one byte (oldest first,
             * FIFO is filled LSB-first by omap_i2c_fifo_run()).
             */
            uint8_t b = s->fifo & 0xff;
            if (s->rxlen > 0) {
                s->fifo >>= 8;
                s->rxlen--;
            }
            /*
             * Refill from the slave while the transfer is still live (this
             * may complete count_cur and issue the STOP, leaving the last
             * few prefetched bytes buffered in the FIFO with the bus idle).
             */
            omap_i2c_fifo_run(s);
            /*
             * Drive the RRDY/ARDY handshake directly off the FIFO drain
             * state so it keeps working after the bus has gone idle: RRDY
             * stays asserted while buffered bytes remain, and ARDY is raised
             * once the last byte has been consumed (master-receive).
             */
            if (s->rxlen > 0) {
                s->stat |= 1 << 3;                          /* RRDY */
            } else {
                s->stat &= ~(1 << 3);                       /* RRDY */
                if (((s->control >> 10) & 1) &&             /* MST */
                    ((~s->control >> 9) & 1)) {             /* TRX (receive) */
                    s->stat |= 1 << 2;                      /* ARDY */
                    s->control &= ~(1 << 10);               /* MST */
                    /*
                     * DCOUNT decrements to 0 on real hardware and stays
                     * there; leave it at 0 so a following address-only probe
                     * (which programs no CNT) starts from 0 and completes
                     * with ARDY instead of waiting for phantom TX bytes.
                     */
                    s->count = 0;
                    s->count_cur = 0;
                }
            }
            s->stat &= ~(1 << 11);                          /* ROVR */
            omap_i2c_interrupts_update(s);
            return b;
        }
        default:
            offset = omap_i2c_v2_to_v1(offset);
            if (offset < 0) {
                OMAP_BAD_REG(addr);
                return 0;
            }
            break;
        }
    }

    switch (offset) {
    case 0x00:  /* I2C_REV */
        return s->revision;                     /* REV */

    case 0x04:  /* I2C_IE */
        return s->mask;

    case 0x08:  /* I2C_STAT */
        return s->stat | (i2c_bus_busy(s->bus) << 12);

    case 0x0c:  /* I2C_IV */
        if (s->revision >= OMAP2_INTR_REV)
            break;
        ret = ctz32(s->stat & s->mask);
        if (ret != 32) {
            s->stat ^= 1 << ret;
            ret++;
        } else {
            ret = 0;
        }
        omap_i2c_interrupts_update(s);
        return ret;

    case 0x10:  /* I2C_SYSS */
        return (s->control >> 15) & 1;  /* I2C_EN */

    case 0x14:  /* I2C_BUF */
        return s->dma;

    case 0x18:  /* I2C_CNT */
        return s->count_cur;    /* DCOUNT */

    case 0x1c:  /* I2C_DATA */
        ret = 0;
        if (s->control & (1 << 14)) {   /* BE */
            ret |= ((s->fifo >> 0) & 0xff) << 8;
            ret |= ((s->fifo >> 8) & 0xff) << 0;
        } else {
            ret |= ((s->fifo >> 8) & 0xff) << 8;
            ret |= ((s->fifo >> 0) & 0xff) << 0;
        }
        if (s->rxlen == 1) {
            s->stat |= 1 << 15;                 /* SBD */
            s->rxlen = 0;
        } else if (s->rxlen > 1) {
            if (s->rxlen > 2)
                s->fifo >>= 16;
            s->rxlen -= 2;
        } else {
            /* XXX: remote access (qualifier) error - what's that?  */
        }
        if (!s->rxlen) {
            s->stat &= ~(1 << 3);                           /* RRDY */
            if (((s->control >> 10) & 1) &&                 /* MST */
                            ((~s->control >> 9) & 1)) {     /* TRX */
                s->stat |= 1 << 2;                          /* ARDY */
                s->control &= ~(1 << 10);                   /* MST */
            }
        }
        s->stat &= ~(1 << 11);                              /* ROVR */
        omap_i2c_fifo_run(s);
        omap_i2c_interrupts_update(s);
        return ret;

    case 0x20:  /* I2C_SYSC */
        return 0;

    case 0x24:  /* I2C_CON */
        return s->control;

    case 0x28:  /* I2C_OA */
        return s->addr[0];

    case 0x2c:  /* I2C_SA */
        return s->addr[1];

    case 0x30:  /* I2C_PSC */
        return s->divider;

    case 0x34:  /* I2C_SCLL */
        return s->times[0];

    case 0x38:  /* I2C_SCLH */
        return s->times[1];

    case 0x3c:  /* I2C_SYSTEST */
        if (s->test & (1 << 15)) {              /* ST_EN */
            s->test ^= 0xa;
            return s->test;
        } else
            return s->test & ~0x300f;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_i2c_write(void *opaque, hwaddr addr,
                uint32_t value)
{
    OMAPI2CState *s = opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    int nack;

    if (s->mmio_version == OMAP_I2C_MMIO_V2) {
        switch (offset) {
        case OMAP_I2C_V2_SYSC:
            if (value & 2) {                    /* SRST */
                omap_i2c_reset(DEVICE(s));
            }
            return;
        case OMAP_I2C_V2_DATA:
            /*
             * The OMAP4/AM64x driver writes the DATA register one byte per
             * MMIO access (writew of a single byte).  Push exactly one byte,
             * mirroring the classic 8-bit FIFO path (omap_i2c_writeb()).
             */
            if (s->txlen <= 2) {
                s->fifo <<= 8;
                s->txlen += 1;
                s->fifo |= value & 0xff;
                s->stat &= ~(1 << 10);                  /* XUDF */
                if (s->txlen > 2) {
                    s->stat &= ~(1 << 4);               /* XRDY */
                }
                omap_i2c_fifo_run(s);
                /*
                 * If the transmit finished and issued its STOP, leave DCOUNT
                 * at 0 (see the DATA read path) so the next probe/transfer
                 * that does not reprogram CNT is not tricked into expecting
                 * stale phantom bytes.
                 */
                if (!i2c_bus_busy(s->bus)) {
                    s->count = 0;
                    s->count_cur = 0;
                }
                omap_i2c_interrupts_update(s);
            }
            return;
        case OMAP_I2C_V2_IRQSTATUS_RAW:
        case OMAP_I2C_V2_IRQSTATUS:             /* write-1-to-clear */
            s->stat &= ~(value & 0x7fff);
            /*
             * XRDY/RRDY are level events: after the driver clears them it
             * expects them to re-assert while the transfer still has room /
             * data.  Re-run the FIFO engine for a live transfer, then
             * re-assert RRDY if bytes remain buffered even after the bus has
             * gone idle (the tail of a receive drains from the FIFO).
             */
            omap_i2c_fifo_run(s);
            if (s->rxlen > 0) {
                s->stat |= 1 << 3;              /* RRDY */
            }
            omap_i2c_interrupts_update(s);
            return;
        case OMAP_I2C_V2_IRQENABLE_SET:
            s->mask |= value & 0xff;
            omap_i2c_interrupts_update(s);
            return;
        case OMAP_I2C_V2_IRQENABLE_CLR:
            s->mask &= ~(value & 0xff);
            omap_i2c_interrupts_update(s);
            return;
        case OMAP_I2C_V2_WE:
            return;                             /* wakeup enable: ignored */
        case OMAP_I2C_V2_REVNB_LO:
        case OMAP_I2C_V2_REVNB_HI:
        case OMAP_I2C_V2_SYSS:
        case OMAP_I2C_V2_BUFSTAT:
            OMAP_RO_REG(addr);
            return;
        default:
            offset = omap_i2c_v2_to_v1(offset);
            if (offset < 0) {
                OMAP_BAD_REG(addr);
                return;
            }
            break;
        }
    }

    switch (offset) {
    case 0x00:  /* I2C_REV */
    case 0x0c:  /* I2C_IV */
    case 0x10:  /* I2C_SYSS */
        OMAP_RO_REG(addr);
        return;

    case 0x04:  /* I2C_IE */
        s->mask = value & (s->revision < OMAP2_GC_REV ? 0x1f : 0x3f);
        break;

    case 0x08:  /* I2C_STAT */
        if (s->revision < OMAP2_INTR_REV) {
            OMAP_RO_REG(addr);
            return;
        }

        /* RRDY and XRDY are reset by hardware. (in all versions???) */
        s->stat &= ~(value & 0x27);
        omap_i2c_interrupts_update(s);
        break;

    case 0x14:  /* I2C_BUF */
        s->dma = value & 0x8080;
        if (value & (1 << 15))                  /* RDMA_EN */
            s->mask &= ~(1 << 3);               /* RRDY_IE */
        if (value & (1 << 7))                   /* XDMA_EN */
            s->mask &= ~(1 << 4);               /* XRDY_IE */
        break;

    case 0x18:  /* I2C_CNT */
        s->count = value;                   /* DCOUNT */
        break;

    case 0x1c:  /* I2C_DATA */
        if (s->txlen > 2) {
            /* XXX: remote access (qualifier) error - what's that?  */
            break;
        }
        s->fifo <<= 16;
        s->txlen += 2;
        if (s->control & (1 << 14)) {           /* BE */
            s->fifo |= ((value >> 8) & 0xff) << 8;
            s->fifo |= ((value >> 0) & 0xff) << 0;
        } else {
            s->fifo |= ((value >> 0) & 0xff) << 8;
            s->fifo |= ((value >> 8) & 0xff) << 0;
        }
        s->stat &= ~(1 << 10);                  /* XUDF */
        if (s->txlen > 2)
            s->stat &= ~(1 << 4);               /* XRDY */
        omap_i2c_fifo_run(s);
        omap_i2c_interrupts_update(s);
        break;

    case 0x20:  /* I2C_SYSC */
        if (s->revision < OMAP2_INTR_REV) {
            OMAP_BAD_REG(addr);
            return;
        }

        if (value & 2) {
            omap_i2c_reset(DEVICE(s));
        }
        break;

    case 0x24:  /* I2C_CON */
        s->control = value & 0xcf87;
        if (~value & (1 << 15)) {               /* I2C_EN */
            if (s->revision < OMAP2_INTR_REV) {
                omap_i2c_reset(DEVICE(s));
            }
            break;
        }
        if ((value & (1 << 15)) && !(value & (1 << 10))) {    /* MST */
            qemu_log_mask(LOG_UNIMP, "%s: I^2C slave mode not supported\n",
                          __func__);
            break;
        }
        if ((value & (1 << 15)) && value & (1 << 8)) {        /* XA */
            qemu_log_mask(LOG_UNIMP,
                          "%s: 10-bit addressing mode not supported\n",
                          __func__);
            break;
        }
        if ((value & (1 << 15)) && value & (1 << 0)) {      /* STT */
            nack = !!i2c_start_transfer(s->bus, s->addr[1], /* SA */
                            (~value >> 9) & 1);             /* TRX */
            s->stat |= nack << 1;                           /* NACK */
            s->control &= ~(1 << 0);                        /* STT */
            s->fifo = 0;
            if (nack)
                s->control &= ~(1 << 1);                    /* STP */
            else {
                s->count_cur = s->count;
                omap_i2c_fifo_run(s);
            }
            omap_i2c_interrupts_update(s);
        }
        break;

    case 0x28:  /* I2C_OA */
        s->addr[0] = value & 0x3ff;
        break;

    case 0x2c:  /* I2C_SA */
        s->addr[1] = value & 0x3ff;
        break;

    case 0x30:  /* I2C_PSC */
        s->divider = value;
        break;

    case 0x34:  /* I2C_SCLL */
        s->times[0] = value;
        break;

    case 0x38:  /* I2C_SCLH */
        s->times[1] = value;
        break;

    case 0x3c:  /* I2C_SYSTEST */
        s->test = value & 0xf80f;
        if (value & (1 << 11))                  /* SBB */
            if (s->revision >= OMAP2_INTR_REV) {
                s->stat |= 0x3f;
                omap_i2c_interrupts_update(s);
            }
        if (value & (1 << 15)) {                /* ST_EN */
            qemu_log_mask(LOG_UNIMP,
                          "%s: System Test not supported\n", __func__);
        }
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static void omap_i2c_writeb(void *opaque, hwaddr addr,
                uint32_t value)
{
    OMAPI2CState *s = opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (s->mmio_version == OMAP_I2C_MMIO_V2 && offset == OMAP_I2C_V2_DATA) {
        offset = 0x1c;                          /* I2C_DATA */
    }

    switch (offset) {
    case 0x1c:  /* I2C_DATA */
        if (s->txlen > 2) {
            /* XXX: remote access (qualifier) error - what's that?  */
            break;
        }
        s->fifo <<= 8;
        s->txlen += 1;
        s->fifo |= value & 0xff;
        s->stat &= ~(1 << 10);                  /* XUDF */
        if (s->txlen > 2)
            s->stat &= ~(1 << 4);               /* XRDY */
        omap_i2c_fifo_run(s);
        omap_i2c_interrupts_update(s);
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

static uint64_t omap_i2c_readfn(void *opaque, hwaddr addr,
                                unsigned size)
{
    switch (size) {
    case 2:
        return omap_i2c_read(opaque, addr);
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read at offset 0x%" HWADDR_PRIx
                      " with bad width %d\n", __func__, addr, size);
        return 0;
    }
}

static void omap_i2c_writefn(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        /* Only the last fifo write can be 8 bit. */
        omap_i2c_writeb(opaque, addr, value);
        break;
    case 2:
        omap_i2c_write(opaque, addr, value);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write at offset 0x%" HWADDR_PRIx
                      " with bad width %d\n", __func__, addr, size);
        break;
    }
}

static const MemoryRegionOps omap_i2c_ops = {
    .read = omap_i2c_readfn,
    .write = omap_i2c_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_i2c_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    OMAPI2CState *s = OMAP_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->drq[0]);
    sysbus_init_irq(sbd, &s->drq[1]);
    sysbus_init_mmio(sbd, &s->iomem);
    s->bus = i2c_init_bus(dev, NULL);
}

static void omap_i2c_realize(DeviceState *dev, Error **errp)
{
    OMAPI2CState *s = OMAP_I2C(dev);
    uint64_t size;

    if (s->mmio_version == OMAP_I2C_MMIO_V2) {
        size = 0x100;                           /* AM64x main_i2c reg length */
    } else {
        size = (s->revision < OMAP2_INTR_REV) ? 0x800 : 0x1000;
    }
    memory_region_init_io(&s->iomem, OBJECT(dev), &omap_i2c_ops, s, "omap.i2c",
                          size);

    /*
     * The IP-V2 wiring (e.g. TI AM64x) drives the module from the SoC clock
     * tree rather than the legacy omap_clk pointer stubs, so the fclk/iclk
     * requirement only applies to the classic OMAP boards.
     */
    if (s->mmio_version == OMAP_I2C_MMIO_V2) {
        return;
    }
    if (!s->fclk) {
        error_setg(errp, "omap_i2c: fclk not connected");
        return;
    }
    if (s->revision >= OMAP2_INTR_REV && !s->iclk) {
        /* Note that OMAP1 doesn't have a separate interface clock */
        error_setg(errp, "omap_i2c: iclk not connected");
        return;
    }
}

void omap_i2c_set_iclk(OMAPI2CState *i2c, omap_clk clk)
{
    i2c->iclk = clk;
}

void omap_i2c_set_fclk(OMAPI2CState *i2c, omap_clk clk)
{
    i2c->fclk = clk;
}

static const Property omap_i2c_properties[] = {
    DEFINE_PROP_UINT8("revision", OMAPI2CState, revision, 0),
    DEFINE_PROP_UINT8("mmio-version", OMAPI2CState, mmio_version,
                      OMAP_I2C_MMIO_V1),
};

static void omap_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, omap_i2c_properties);
    device_class_set_legacy_reset(dc, omap_i2c_reset);
    /* Reason: pointer properties "iclk", "fclk" */
    dc->user_creatable = false;
    dc->realize = omap_i2c_realize;
}

static const TypeInfo omap_i2c_info = {
    .name = TYPE_OMAP_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OMAPI2CState),
    .instance_init = omap_i2c_init,
    .class_init = omap_i2c_class_init,
};

static void omap_i2c_register_types(void)
{
    type_register_static(&omap_i2c_info);
}

I2CBus *omap_i2c_bus(DeviceState *omap_i2c)
{
    OMAPI2CState *s = OMAP_I2C(omap_i2c);
    return s->bus;
}

type_init(omap_i2c_register_types)
