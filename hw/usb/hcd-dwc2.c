/*
 * dwc-hsotg (dwc2) USB host controller emulation
 *
 * Based on hw/usb/hcd-ehci.c and hw/usb/hcd-ohci.c
 *
 * Copyright (c) 2020 Paul Zimmerman <pauldzim@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/usb/dwc2-regs.h"
#include "hw/usb/hcd-dwc2.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

//#define DWC2_DEBUG      1

#ifdef DWC2_DEBUG
#define DPRINTF(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

#define DWC2_DO_SOFS    1

#define USB_HZ_FS       12000000
#define USB_HZ_HS       96000000

/* nifty macros from Arnon's EHCI version  */
#define get_field(data, field) \
    (((data) & field##_MASK) >> field##_SHIFT)

#define set_field(data, newval, field) do { \
    uint32_t val = *data; \
    val &= ~ field##_MASK; \
    val |= ((newval) << field##_SHIFT) & field##_MASK; \
    *data = val; \
} while (0)

#define get_bit(data, bitmask) \
    (!!((data) & bitmask))

/* update irq line */
static inline void dwc2_update_irq(DWC2State *s)
{
    static int oldlevel;
    int level = 0;

    if ((s->gintsts & s->gintmsk) && (s->gahbcfg & GAHBCFG_GLBL_INTR_EN))
        level = 1;
    if (level != oldlevel) {
        /*DPRINTF("dwc2_update_irq, sts 0x%08x msk 0x%08x level %d\n",
                s->gintsts, s->gintmsk, level);*/
        oldlevel = level;
        qemu_set_irq(s->irq, level);
    }
}

/* flag interrupt condition */
static inline void dwc2_raise_global_irq(DWC2State *s, uint32_t intr)
{
    /*DPRINTF("dwc2_raise_global_irq, 0x%08x\n", intr);*/
    s->gintsts |= intr;
    dwc2_update_irq(s);
}

static inline void dwc2_lower_global_irq(DWC2State *s, uint32_t intr)
{
    /*DPRINTF("dwc2_lower_global_irq, 0x%08x\n", intr);*/
    s->gintsts &= ~intr;
    dwc2_update_irq(s);
}

static inline void dwc2_raise_host_irq(DWC2State *s, uint32_t intr)
{
    /*DPRINTF("dwc2_raise_host_irq, 0x%04x\n", intr);*/
    s->haint |= intr;
    s->haint &= 0xffff;
    if (s->haint & s->haintmsk) {
        dwc2_raise_global_irq(s, GINTSTS_HCHINT);
    }
}

static inline void dwc2_lower_host_irq(DWC2State *s, uint32_t intr)
{
    /*DPRINTF("dwc2_lower_host_irq, 0x%04x\n", intr);*/
    s->haint &= ~intr;
    if (!(s->haint & s->haintmsk)) {
        dwc2_lower_global_irq(s, GINTSTS_HCHINT);
    }
}

static inline void dwc2_update_hc_irq(DWC2State *s, int index)
{
    uint32_t intr = 1 << (index >> 3);

    /*DPRINTF("dwc2_update_hc_irq, hcint%d 0x%04x hcintmsk%d 0x%04x\n",
            index >> 3, s->hreg1[index + 2], index >> 3, s->hreg1[index + 3]);*/
    if (s->hreg1[index + 2] & s->hreg1[index + 3]) {
        dwc2_raise_host_irq(s, intr);
    } else {
        dwc2_lower_host_irq(s, intr);
    }
}

/* set a timer for EOF */
static void dwc2_eof_timer(DWC2State *s)
{
#ifdef DWC2_DO_SOFS
    timer_mod(s->eof_timer, s->sof_time + s->usb_frame_time);
#endif
}

#ifdef DWC2_DO_SOFS
/* Set a timer for EOF and generate a SOF event */
static void dwc2_sof(DWC2State *s)
{
    s->sof_time += s->usb_frame_time;
    dwc2_eof_timer(s);
    dwc2_raise_global_irq(s, GINTSTS_SOF);
}

/* Do frame processing on frame boundary */
static void dwc2_frame_boundary(void *opaque)
{
    DWC2State *s = opaque;

    /* Frame boundary, so do EOF stuff here */

    /* Increment frame number */
    s->frame_number = (s->frame_number + 1) & 0xffff;
    s->hfnum = (s->hfnum & ~HFNUM_FRNUM_MASK) |
               (s->frame_number & HFNUM_MAX_FRNUM);

    /* Do SOF stuff here */
    dwc2_sof(s);
}
#endif

/* Start sending SOF tokens across the USB bus, lists are processed in
 * next frame
 */
static int dwc2_bus_start(DWC2State *s)
{
    /* Delay the first SOF event by one frame time as
     * linux driver is not ready to receive it and
     * can meet some race conditions
     */

    s->sof_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    dwc2_eof_timer(s);

    return 1;
}

/* Stop sending SOF tokens on the bus */
static void dwc2_bus_stop(DWC2State *s)
{
#ifdef DWC2_DO_SOFS
    timer_del(s->eof_timer);
#endif
}

static USBDevice *dwc2_find_device(DWC2State *s, uint8_t addr)
{
    USBDevice *dev;
    USBPort *port;
    int i;

    DPRINTF("dwc2_find_device\n");
    for (i = 0; i < NB_PORTS; i++) {
        port = &s->ports[i];
        if (!(s->hprt0 & HPRT0_ENA)) {
            DPRINTF("Port %d not enabled\n", i);
            continue;
        }
        dev = usb_find_device(port, addr);
        if (dev != NULL) {
            DPRINTF("found device\n");
            return dev;
        }
    }
    DPRINTF("device NOT found\n");
    return NULL;
}

static const char *pstatus[] = {
    "USB_RET_SUCCESS", "USB_RET_NODEV", "USB_RET_NAK", "USB_RET_STALL",
    "USB_RET_BABBLE", "USB_RET_IOERROR", "USB_RET_ASYNC",
    "USB_RET_ADD_TO_QUEUE", "USB_RET_REMOVE_FROM_QUEUE"
};

static uint32_t pintr[] = {
    HCINTMSK_XFERCOMPL, HCINTMSK_XACTERR, HCINTMSK_NAK, HCINTMSK_STALL,
    HCINTMSK_BBLERR, HCINTMSK_XACTERR, HCINTMSK_XACTERR, HCINTMSK_XACTERR,
    HCINTMSK_XACTERR
};

#ifdef DWC2_DEBUG
static const char *types[] = {
    "Ctrl", "Isoc", "Bulk", "Intr"
};

static const char *dirs[] = {
    "Out", "In"
};
#endif

static void dwc2_handle_packet(DWC2State *s, USBDevice *dev, USBEndpoint *ep,
                               uint32_t index, bool send) {
    DWC2Packet *p;
    uint32_t hcchar = s->hreg1[index];
    uint32_t hctsiz = s->hreg1[index + 4];
    uint32_t hcdma = s->hreg1[index + 5];
    uint32_t chan, epnum, epdir, eptype, mps, pid, pcnt, len, tlen, intr = 0;
    uint32_t tpcnt, stsidx, actual = 0;
    int i;
    bool done = false;

    epnum = get_field(hcchar, HCCHAR_EPNUM);
    epdir = get_bit(hcchar, HCCHAR_EPDIR);
    eptype = get_field(hcchar, HCCHAR_EPTYPE);
    mps = get_field(hcchar, HCCHAR_MPS);
    pid = get_field(hctsiz, TSIZ_SC_MC_PID);
    pcnt = get_field(hctsiz, TSIZ_PKTCNT);
    len = get_field(hctsiz, TSIZ_XFERSIZE);
    assert(len <= MAX_XFER_SIZE);
    chan = index >> 3;
    p = &s->packet[chan];

    DPRINTF("dwc2_handle_packet,"
            " ch %d dev %p pkt %p ep %d type %s dir %s mps %d len %d pcnt %d\n",
            chan, dev, &p->packet, epnum, types[eptype], dirs[epdir], mps, len,
            pcnt);

    if (eptype == USB_ENDPOINT_XFER_CONTROL && pid == TSIZ_SC_MC_PID_SETUP) {
        pid = USB_TOKEN_SETUP;
    } else {
        pid = epdir ? USB_TOKEN_IN : USB_TOKEN_OUT;
    }

    tlen = len;
    if (p->small) {
        if (tlen > mps) {
            tlen = mps;
        }
    }

    if (send) {
        if (pid != USB_TOKEN_IN) {
            DPRINTF("calling dma_memory_read, len %u\n", tlen);
            if (dma_memory_read(&s->dma_as, hcdma,
                                s->usb_buf[chan], tlen) != MEMTX_OK) {
                fprintf(stderr, "dma_memory_read failed\n");
            }
            if (tlen > 0) {
                for (i = 0; i < 8; i++)
                    DPRINTF(" %02x", s->usb_buf[chan][i]);
                DPRINTF("\n");
            }
        }

        usb_packet_init(&p->packet);
        usb_packet_setup(&p->packet, pid, ep, 0, hcdma,
                         pid != USB_TOKEN_IN, true);
        usb_packet_addbuf(&p->packet, s->usb_buf[chan], tlen);
        p->async = DWC2_ASYNC_NONE;
        usb_handle_packet(dev, &p->packet);
    }

    stsidx = -p->packet.status;
    assert(stsidx < sizeof(pstatus) / sizeof(*pstatus));
    DPRINTF("packet status %s len %d\n", pstatus[stsidx],
            p->packet.actual_length);
    if (p->packet.status != USB_RET_SUCCESS &&
            p->packet.status != USB_RET_NAK &&
            p->packet.status != USB_RET_STALL) {
        fprintf(stderr, "dwc2_handle_packet: packet status %s\n",
                pstatus[stsidx]);
    }

    if (p->packet.status == USB_RET_ASYNC) {
        usb_device_flush_ep_queue(dev, ep);
        assert(p->async != DWC2_ASYNC_INFLIGHT);
        p->dev = dev;
        p->ep = ep;
        p->index = index;
        p->epnum = epnum;
        p->mps = mps;
        p->pid = pid;
        p->pcnt = pcnt;
        p->len = tlen;
        p->needs_service = false;
        p->async = DWC2_ASYNC_INFLIGHT;
        return;
    }

    if (p->packet.status == USB_RET_SUCCESS) {
        actual = p->packet.actual_length;
        if (pid == USB_TOKEN_IN) {
            DPRINTF("calling dma_memory_write, len %u\n", actual);
            if (dma_memory_write(&s->dma_as, hcdma, s->usb_buf[chan],
                                 actual) != MEMTX_OK) {
                fprintf(stderr, "dma_memory_write failed\n");
            }
            if (actual > 0) {
                for (i = 0; i < 8; i++)
                    DPRINTF(" %02x", s->usb_buf[chan][i]);
                DPRINTF("\n");
            }
        }

        tpcnt = actual / mps;
        if (actual % mps) {
            tpcnt++;
            if (pid == USB_TOKEN_IN)
                done = true;
        }

        pcnt -= tpcnt < pcnt ? tpcnt : pcnt;
        set_field(&hctsiz, pcnt, TSIZ_PKTCNT);
        len -= actual < len ? actual : len;
        set_field(&hctsiz, len, TSIZ_XFERSIZE);
        s->hreg1[index + 4] = hctsiz;

        hcdma += actual;
        s->hreg1[index + 5] = hcdma;

        if (!pcnt || len == 0 || actual == 0) {
            done = true;
        }
    } else {
        intr |= pintr[stsidx];
        if (p->packet.status == USB_RET_NAK &&
            (eptype == USB_ENDPOINT_XFER_CONTROL ||
             eptype == USB_ENDPOINT_XFER_BULK)) {
            /* for ctrl/bulk, automatically retry on NAK,
               but send the interrupt anyway */
            intr &= ~HCINTMSK_RESERVED14_31;
            s->hreg1[index + 2] |= intr;
        } else {
            intr |= HCINTMSK_CHHLTD;
            done = true;
        }
    }

    usb_packet_cleanup(&p->packet);

    if (done) {
        hcchar &= ~HCCHAR_CHENA;
        s->hreg1[index] = hcchar;
        if (!(intr & HCINTMSK_CHHLTD)) {
            intr |= HCINTMSK_CHHLTD | HCINTMSK_XFERCOMPL;
        }
        intr &= ~HCINTMSK_RESERVED14_31;
        s->hreg1[index + 2] |= intr;
        p->needs_service = false;
        DPRINTF("done %s len %d actual %d pcnt %d\n", pstatus[stsidx], len, actual, pcnt);
        dwc2_update_hc_irq(s, index);
        return;
    }

    p->dev = dev;
    p->ep = ep;
    p->index = index;
    p->epnum = epnum;
    p->mps = mps;
    p->pid = pid;
    p->pcnt = pcnt;
    p->len = tlen;
    p->needs_service = true;
    DPRINTF("cont %s len %d actual %d pcnt %d\n", pstatus[stsidx], len, actual, pcnt);
}

/* Attach or detach a device on root hub */

static void dwc2_attach(USBPort *port)
{
    DWC2State *s = port->opaque;
    int hispd = 0;

    DPRINTF("dwc2_attach, port %p\n", port);
    assert(port->index < NB_PORTS);

    if (!port->dev || !port->dev->attached)
        return;

    s->hprt0 &= ~HPRT0_SPD_MASK;

    switch (port->dev->speed) {
    case USB_SPEED_LOW:
        DPRINTF("low-speed device attached\n");
        s->hprt0 |= HPRT0_SPD_LOW_SPEED << HPRT0_SPD_SHIFT;
        break;
    case USB_SPEED_FULL:
        DPRINTF("full-speed device attached\n");
        s->hprt0 |= HPRT0_SPD_FULL_SPEED << HPRT0_SPD_SHIFT;
        break;
    case USB_SPEED_HIGH:
        DPRINTF("high-speed device attached\n");
        s->hprt0 |= HPRT0_SPD_HIGH_SPEED << HPRT0_SPD_SHIFT;
        hispd = 1;
        break;
    }

    if (hispd) {
        s->usb_frame_time = NANOSECONDS_PER_SECOND / 8000;        /* 125000 */
        if (NANOSECONDS_PER_SECOND >= USB_HZ_HS) {
            s->usb_bit_time = NANOSECONDS_PER_SECOND / USB_HZ_HS; /* 10.4 */
        } else {
            s->usb_bit_time = 1;
        }
    } else {
        s->usb_frame_time = NANOSECONDS_PER_SECOND / 1000;        /* 1000000 */
        if (NANOSECONDS_PER_SECOND >= USB_HZ_FS) {
            s->usb_bit_time = NANOSECONDS_PER_SECOND / USB_HZ_FS; /* 83.3 */
        } else {
            s->usb_bit_time = 1;
        }
    }

    s->fi = 11999;
    s->hprt0 |= HPRT0_CONNDET | HPRT0_CONNSTS;

    dwc2_bus_start(s);
    dwc2_raise_global_irq(s, GINTSTS_PRTINT);
}

static void dwc2_detach(USBPort *port)
{
    DWC2State *s = port->opaque;

    DPRINTF("dwc2_detach, port %p\n", port);
    assert(port->index < NB_PORTS);

    dwc2_bus_stop(s);

    s->hprt0 &= ~(HPRT0_SPD_MASK | HPRT0_SUSP | HPRT0_ENA | HPRT0_CONNSTS);
    s->hprt0 |= HPRT0_CONNDET | HPRT0_ENACHG;

    dwc2_raise_global_irq(s, GINTSTS_PRTINT);
}

static void dwc2_child_detach(USBPort *port, USBDevice *child)
{
    DPRINTF("dwc2_child_detach, port %p child %p\n", port, child);
    assert(port->index < NB_PORTS);
}

static void dwc2_wakeup(USBPort *port)
{
    DWC2State *s = port->opaque;

    DPRINTF("dwc2_wakeup, port %p\n", port);
    assert(port->index < NB_PORTS);

    if (s->hprt0 & HPRT0_SUSP) {
        s->hprt0 |= HPRT0_RES;
        dwc2_raise_global_irq(s, GINTSTS_PRTINT);
    }

    qemu_bh_schedule(s->async_bh);
}

static void dwc2_async_complete_packet(USBPort *port, USBPacket *packet)
{
    DWC2State *s = port->opaque;
    DWC2Packet *p;

    DPRINTF("dwc2_async_complete_packet, port %p packet %p\n", port, packet);
    assert(port->index < NB_PORTS);

    p = container_of(packet, DWC2Packet, packet);
    DPRINTF("ch %d dev %p epnum %d\n", p->index >> 3, p->dev, p->epnum);
    assert(p->async == DWC2_ASYNC_INFLIGHT);

    if (packet->status == USB_RET_REMOVE_FROM_QUEUE) {
        usb_packet_cleanup(packet);
        return;
    }

    dwc2_handle_packet(s, p->dev, p->ep, p->index, false);

    p->async = DWC2_ASYNC_FINISHED;
    qemu_bh_schedule(s->async_bh);
}

static USBPortOps dwc2_port_ops = {
    .attach = dwc2_attach,
    .detach = dwc2_detach,
    .child_detach = dwc2_child_detach,
    .wakeup = dwc2_wakeup,
    .complete = dwc2_async_complete_packet,
};

static uint32_t dwc2_get_frame_remaining(DWC2State *s)
{
    uint32_t fr = 0;
    int64_t tks;

    tks = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->sof_time;
    if (tks < 0) {
        tks = 0;
    }

    /* avoid muldiv if possible */
    if (tks >= s->usb_frame_time || tks < s->usb_bit_time) {
        goto out;
    }

    /* tks = number of ns since SOF, divided by 83 (fs) or 10 (hs) */
    tks = tks / s->usb_bit_time;
    if (tks >= (int64_t)s->fi) {
        goto out;
    }

    /* remaining = frame interval minus tks */
    fr = (uint32_t)((int64_t)s->fi - tks);

out:
    return fr;
}

static void dwc2_work_bh(void *opaque)
{
    DWC2State *s = opaque;
    DWC2Packet *p;
    int64_t t_now, expire_time;
    int chan;
    bool done = false, need_timer = false;

    DPRINTF("dwc2_work_bh\n");
    if (s->working) {
        return;
    }
    s->working = true;

    t_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    chan = s->next_chan;

    while (true) {
        p = &s->packet[chan];
        if (p->needs_service) {
            DPRINTF("start %d servicing ch %d dev %p epnum %d\n",
                    s->next_chan, chan, p->dev, p->epnum);
            dwc2_handle_packet(s, p->dev, p->ep, p->index, true);
            need_timer = true;
            done = true;
        }
        if (++chan == NB_CHAN) {
            chan = 0;
        }
        if (done) {
            s->next_chan = chan;
            DPRINTF("next %d\n", chan);
            break;
        }
        if (chan == s->next_chan) {
            break;
        }
    }

    if (need_timer) {
        expire_time = t_now + NANOSECONDS_PER_SECOND / 4000;
        timer_mod(s->frame_timer, expire_time);
    }
    s->working = false;
}

static void dwc2_enable_chan(DWC2State *s,  uint32_t index)
{
    USBDevice *dev;
    USBEndpoint *ep;
    uint32_t hcchar;
    uint32_t hctsiz;
    uint32_t devadr, epnum, epdir, eptype, pid, len;
    DWC2Packet *p;

    assert((index >> 3) < NB_CHAN);
    p = &s->packet[index >> 3];
    hcchar = s->hreg1[index];
    hctsiz = s->hreg1[index + 4];
    devadr = get_field(hcchar, HCCHAR_DEVADDR);
    epnum = get_field(hcchar, HCCHAR_EPNUM);
    epdir = get_bit(hcchar, HCCHAR_EPDIR);
    eptype = get_field(hcchar, HCCHAR_EPTYPE);
    pid = get_field(hctsiz, TSIZ_SC_MC_PID);
    len = get_field(hctsiz, TSIZ_XFERSIZE);

    dev = dwc2_find_device(s, devadr);

    DPRINTF("dwc2_enable_chan, ch %d dev %p pkt %p epnum %d\n",
            index >> 3, dev, &p->packet, epnum);
    if (dev == NULL) {
        fprintf(stderr, "no device found\n");
        return;
    }

    if (eptype == USB_ENDPOINT_XFER_CONTROL && pid == TSIZ_SC_MC_PID_SETUP) {
        pid = USB_TOKEN_SETUP;
    } else {
        pid = epdir ? USB_TOKEN_IN : USB_TOKEN_OUT;
    }

    ep = usb_ep_get(dev, pid, epnum);

    /* Hack: Networking doesn't like us delivering large transfers, it kind
     * of works but the latency is horrible. So if the tansfer is <= the mtu
     * size, we take that as a hint that this might be a network transfer,
     * and do the transfer packet-by-packet.
     */
    if (len > 1536) {
        p->small = false;
    } else {
        p->small = true;
    }

    dwc2_handle_packet(s, dev, ep, index, true);
    qemu_bh_schedule(s->async_bh);
}

#ifdef DWC2_DEBUG
static const char *glbregnm[] = {
    "GOTGCTL  ", "GOTGINT  ", "GAHBCFG  ", "GUSBCFG  ", "GRSTCTL  ", "GINTSTS  ",
    "GINTMSK  ", "GRXSTSR  ", "GRXSTSP  ", "GRXFSIZ  ", "GNPTXFSIZ", "GNPTXSTS ",
    "GI2CCTL  ", "GPVNDCTL ", "GGPIO    ", "GUID     ", "GSNPSID  ", "GHWCFG1  ",
    "GHWCFG2  ", "GHWCFG3  ", "GHWCFG4  ", "GLPMCFG  ", "GPWRDN   ", "GDFIFOCFG",
    "GADPCTL  ", "GREFCLK  ", "GINTMSK2 ", "GINTSTS2 "
};
#endif

static uint64_t dwc2_glbreg_read(void *ptr, hwaddr addr, unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->glbregbase + addr;
    uint32_t val;

    assert(reg <= GINTSTS2);
    val = s->glbreg[addr >> 2];

    switch (reg) {
    case GRSTCTL:
        /* clear any self-clearing bits that were set */
        val &= ~(GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH | GRSTCTL_IN_TKNQ_FLSH |
                 GRSTCTL_FRMCNTRRST | GRSTCTL_HSFTRST | GRSTCTL_CSFTRST);
        s->glbreg[addr >> 2] = val;
        break;
    default:
        break;
    }

    if (reg != GAHBCFG && reg != GINTSTS && reg != GINTMSK && reg != GSNPSID) {
        DPRINTF("dwc2_glbreg_read  0x%04lx %s val 0x%08x\n",
                addr, glbregnm[addr >> 2], val);
    }

    return val;
}

static void dwc2_glbreg_write(void *ptr, hwaddr addr, uint64_t val,
                              unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->glbregbase + addr;
    uint32_t *mmio;
    uint32_t old;
    int iflg = 0;

    assert(reg <= GINTSTS2);
    mmio = &s->glbreg[addr >> 2];
    old = *mmio;

    if (reg != GINTSTS && reg != GINTMSK) {
        DPRINTF("dwc2_glbreg_write 0x%04lx %s val 0x%08lx old 0x%08x ",
                addr, glbregnm[addr >> 2], val, old);
    }

    switch (reg) {
    case GOTGCTL:
        /* don't allow setting of read-only bits */
        val &= ~(GOTGCTL_MULT_VALID_BC_MASK | GOTGCTL_BSESVLD |
                 GOTGCTL_ASESVLD | GOTGCTL_DBNC_SHORT | GOTGCTL_CONID_B |
                 GOTGCTL_HSTNEGSCS | GOTGCTL_SESREQSCS);
        /* don't allow clearing of read-only bits */
        val |= old & (GOTGCTL_MULT_VALID_BC_MASK | GOTGCTL_BSESVLD |
                      GOTGCTL_ASESVLD | GOTGCTL_DBNC_SHORT | GOTGCTL_CONID_B |
                      GOTGCTL_HSTNEGSCS | GOTGCTL_SESREQSCS);
        break;
    case GAHBCFG:
        if ((val & GAHBCFG_GLBL_INTR_EN) && !(old & GAHBCFG_GLBL_INTR_EN)) {
            iflg = 1;
        }
        break;
    case GRSTCTL:
        val |= GRSTCTL_AHBIDLE;
        val &= ~GRSTCTL_DMAREQ;
        if (!(old & GRSTCTL_TXFFLSH) && (val & GRSTCTL_TXFFLSH)) {
                /* TODO - TX fifo flush */
        }
        if (!(old & GRSTCTL_RXFFLSH) && (val & GRSTCTL_RXFFLSH)) {
                /* TODO - RX fifo flush */
        }
        if (!(old & GRSTCTL_IN_TKNQ_FLSH) && (val & GRSTCTL_IN_TKNQ_FLSH)) {
                /* TODO - device IN token queue flush */
        }
        if (!(old & GRSTCTL_FRMCNTRRST) && (val & GRSTCTL_FRMCNTRRST)) {
                /* TODO - host frame counter reset */
        }
        if (!(old & GRSTCTL_HSFTRST) && (val & GRSTCTL_HSFTRST)) {
                /* TODO - ? soft reset */
        }
        if (!(old & GRSTCTL_CSFTRST) && (val & GRSTCTL_CSFTRST)) {
                /* TODO - core soft reset */
        }
        /* don't allow clearing of self-clearing bits */
        val |= old & (GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH |
                      GRSTCTL_IN_TKNQ_FLSH | GRSTCTL_FRMCNTRRST |
                      GRSTCTL_HSFTRST | GRSTCTL_CSFTRST);
        break;
    case GINTSTS:
        /* clear the write-1-to-clear bits */
        val |= ~old;
        val = ~val;
        /* don't allow clearing of read-only bits */
        val |= old & (GINTSTS_PTXFEMP | GINTSTS_HCHINT | GINTSTS_PRTINT |
                      GINTSTS_OEPINT | GINTSTS_IEPINT | GINTSTS_GOUTNAKEFF |
                      GINTSTS_GINNAKEFF | GINTSTS_NPTXFEMP | GINTSTS_RXFLVL |
                      GINTSTS_OTGINT | GINTSTS_CURMODE_HOST);
        iflg = 1;
        break;
    case GINTMSK:
        iflg = 1;
        break;
    default:
        break;
    }

    val &= 0xffffffff;
    if (reg != GINTSTS && reg != GINTMSK) {
        DPRINTF("result 0x%08lx\n", val);
    }
    *mmio = val;
    if (iflg) {
        dwc2_update_irq(s);
    }
}

static uint64_t dwc2_fszreg_read(void *ptr, hwaddr addr, unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->fszregbase + addr;
    uint32_t val;

    assert(reg <= HPTXFSIZ);
    val = s->fszreg[addr >> 2];

    DPRINTF("dwc2_fszreg_read  0x%04lx HPTXFSIZ  val 0x%08x\n",
            addr, val);
    return val;
}

static void dwc2_fszreg_write(void *ptr, hwaddr addr, uint64_t val,
                              unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->fszregbase + addr;
    uint32_t *mmio;
#ifdef DWC2_DEBUG
    uint32_t old;
#endif

    assert(reg <= HPTXFSIZ);
    mmio = &s->fszreg[addr >> 2];
#ifdef DWC2_DEBUG
    old = *mmio;
#endif

    DPRINTF("dwc2_fszreg_write 0x%04lx HPTXFSIZ  val 0x%08lx old 0x%08x ",
            addr, val, old);
    val &= 0xffffffff;
    DPRINTF("result 0x%lx\n", val);
    *mmio = val;
}

#ifdef DWC2_DEBUG
static const char *hreg0nm[] = {
    "HCFG     ", "HFIR     ", "HFNUM    ", "<rsvd>   ", "HPTXSTS  ", "HAINT    ",
    "HAINTMSK ", "HFLBADDR ", "<rsvd>   ", "<rsvd>   ", "<rsvd>   ", "<rsvd>   ",
    "<rsvd>   ", "<rsvd>   ", "<rsvd>   ", "<rsvd>   ", "HPRT0    "
};
#endif

static uint64_t dwc2_hreg0_read(void *ptr, hwaddr addr, unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->hreg0base + addr;
    uint32_t val;

    assert(reg <= HPRT0);
    val = s->hreg0[addr >> 2];

    switch (reg) {
    case HFNUM:
        val = (dwc2_get_frame_remaining(s) << HFNUM_FRREM_SHIFT) |
              ((s->frame_number & HFNUM_MAX_FRNUM) << HFNUM_FRNUM_SHIFT);
        break;
    default:
        break;
    }

    if (reg != HFNUM) {
        DPRINTF("dwc2_hreg0_read   0x%04lx %s val 0x%08x\n",
                addr, hreg0nm[addr >> 2], val);
    }
    return val;
}

static void dwc2_hreg0_write(void *ptr, hwaddr addr, uint64_t val,
                             unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->hreg0base + addr;
    USBDevice *dev = s->ports[0].dev;
    uint32_t *mmio;
    uint32_t tval, told, old;
    int prst = 0;
    int iflg = 0;

    assert(reg <= HPRT0);
    mmio = &s->hreg0[addr >> 2];
    old = *mmio;

    DPRINTF("dwc2_hreg0_write  0x%04lx %s val 0x%08lx old 0x%08x ",
            addr, hreg0nm[addr >> 2], val, old);

    switch (reg) {
    case HFIR:
        break;
    case HFNUM:
    case HPTXSTS:
    case HAINT:
        DPRINTF("**write to read-only register**\n");
        return;
    case HAINTMSK:
        val &= 0xffff;
        break;
    case HPRT0:
        /* don't allow clearing of read-only bits */
        val |= old & (HPRT0_SPD_MASK | HPRT0_LNSTS_MASK | HPRT0_OVRCURRACT |
                      HPRT0_CONNSTS);
        /* don't allow clearing of self-clearing bits */
        val |= old & (HPRT0_SUSP | HPRT0_RES);
        /* don't allow setting of self-setting bits */
        if (!(old & HPRT0_ENA) && (val & HPRT0_ENA)) {
            val &= ~HPRT0_ENA;
        }
        /* clear the write-1-to-clear bits */
        tval = val & (HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_ENA | HPRT0_CONNDET);
        told = old & (HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_ENA | HPRT0_CONNDET);
        tval |= ~told;
        tval = ~tval;
        tval &= (HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_ENA | HPRT0_CONNDET);
        val &= ~(HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_ENA | HPRT0_CONNDET);
        val |= tval;
        if (!(val & HPRT0_RST) && (old & HPRT0_RST)) {
            if (dev && dev->attached) {
                val |= HPRT0_ENA | HPRT0_ENACHG;
                prst = 1;
            }
        }
        if (val & (HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_CONNDET)) {
            iflg = 1;
        } else {
            iflg = -1;
        }
        break;
    default:
        break;
    }

    if (prst) {
        DPRINTF("call usb_port_reset\n");
        usb_port_reset(&s->ports[0]);
        val &= ~HPRT0_CONNDET;
    }
    val &= 0xffffffff;
    DPRINTF("result 0x%08lx\n", val);
    *mmio = val;
    if (iflg) {
        if (iflg > 0) {
            DPRINTF("enable PRTINT\n");
            dwc2_raise_global_irq(s, GINTSTS_PRTINT);
        } else {
            DPRINTF("disable PRTINT\n");
            dwc2_lower_global_irq(s, GINTSTS_PRTINT);
        }
    }
}

#ifdef DWC2_DEBUG
static const char *hreg1nm[] = {
    "HCCHAR  ", "HCSPLT  ", "HCINT   ", "HCINTMSK", "HCTSIZ  ", "HCDMA   ",
    "<rsvd>  ", "HCDMAB  "
};
#endif

static uint64_t dwc2_hreg1_read(void *ptr, hwaddr addr, unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->hreg1base + addr;
    uint32_t val;

    assert(reg <= HCDMAB(NB_CHAN - 1));
    val = s->hreg1[addr >> 2];

    DPRINTF("dwc2_hreg1_read   0x%04lx %s%ld val 0x%08x\n",
            addr, hreg1nm[(addr >> 2) & 7], addr >> 5, val);
    assert(s->hreg1base + (addr & 0x1c) <= HCDMAB(NB_CHAN));
    return val;
}

static void dwc2_hreg1_write(void *ptr, hwaddr addr, uint64_t val,
                             unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->hreg1base + addr;
    uint32_t *mmio;
    uint32_t old;
    int iflg = 0;
    int enflg = 0;
    int disflg = 0;

    assert(reg <= HCDMAB(NB_CHAN - 1));
    mmio = &s->hreg1[addr >> 2];
    old = *mmio;

    DPRINTF("dwc2_hreg1_write  0x%04lx %s%ld val 0x%08lx old 0x%08x ",
            addr, hreg1nm[(addr >> 2) & 7], addr >> 5, val, old);

    switch (s->hreg1base + (addr & 0x1c)) {
    case HCCHAR(0):
        if ((val & HCCHAR_CHDIS) && !(old & HCCHAR_CHDIS)) {
            val &= ~(HCCHAR_CHENA | HCCHAR_CHDIS);
            disflg = 1;
        } else {
            val |= old & HCCHAR_CHDIS;
            if ((val & HCCHAR_CHENA) && !(old & HCCHAR_CHENA)) {
                val &= ~HCCHAR_CHDIS;
                enflg = 1;
            } else {
                val |= old & HCCHAR_CHENA;
            }
        }
        break;
    case HCINT(0):
        /* clear the write-1-to-clear bits */
        val |= ~old;
        val = ~val;
        val &= ~HCINTMSK_RESERVED14_31;
        iflg = 1;
        break;
    case HCINTMSK(0):
        val &= ~HCINTMSK_RESERVED14_31;
        iflg = 1;
        break;
    case HCDMAB(0):
        DPRINTF("**write to read-only register**\n");
        return;
    default:
        break;
    }

    val &= 0xffffffff;
    DPRINTF("result 0x%08lx\n", val);
    *mmio = val;
    if (disflg) {
        /* set ChHltd in HCINT */
        s->hreg1[((addr >> 2) & ~7) + 2] |= HCINTMSK_CHHLTD;
        iflg = 1;
    }
    if (enflg) {
        dwc2_enable_chan(s, (addr >> 2) & ~7);
    }
    if (iflg) {
        dwc2_update_hc_irq(s, (addr >> 2) & ~7);
    }
}

#ifdef DWC2_DEBUG
static const char *pcgregnm[] = {
        "PCGCTL   ", "PCGCCTL1 "
};
#endif

static uint64_t dwc2_pcgreg_read(void *ptr, hwaddr addr, unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->pcgregbase + addr;
    uint32_t val;

    assert(reg <= PCGCCTL1);
    val = s->pcgreg[addr >> 2];

    DPRINTF("dwc2_pcgreg_read  0x%04lx %s val 0x%08x\n",
            addr, pcgregnm[addr >> 2], val);
    return val;
}

static void dwc2_pcgreg_write(void *ptr, hwaddr addr, uint64_t val,
                              unsigned size)
{
    DWC2State *s = ptr;
    uint32_t reg = s->pcgregbase + addr;
    uint32_t *mmio;
#ifdef DWC2_DEBUG
    uint32_t old;
#endif

    assert(reg <= PCGCCTL1);
    mmio = &s->pcgreg[addr >> 2];
#ifdef DWC2_DEBUG
    old = *mmio;
#endif

    DPRINTF("dwc2_pcgreg_write 0x%04lx %s val 0x%08lx old 0x%08x ",
            addr, pcgregnm[addr >> 2], val, old);
    val &= 0xffffffff;
    DPRINTF("result 0x%08lx\n", val);
    *mmio = val;
}

static uint64_t dwc2_hreg2_read(void *ptr, hwaddr addr, unsigned size)
{
    /* TODO - implement FIFOs to support slave mode */
    DPRINTF("dwc2_hreg2_read   0x%04lx FIFO%ld     val 0x%08x\n",
            addr, addr >> 12, 0);
    return 0;
}

static void dwc2_hreg2_write(void *ptr, hwaddr addr, uint64_t val,
                             unsigned size)
{
    /* TODO - implement FIFOs to support slave mode */
    DPRINTF("dwc2_hreg2_write  0x%04lx FIFO%ld     val 0x%08lx ",
            addr, addr >> 12, val);
    val &= 0xffffffff;
    DPRINTF("result 0x%08lx\n", val);
}

static const MemoryRegionOps dwc2_mmio_glbreg_ops = {
    .read = dwc2_glbreg_read,
    .write = dwc2_glbreg_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps dwc2_mmio_fszreg_ops = {
    .read = dwc2_fszreg_read,
    .write = dwc2_fszreg_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps dwc2_mmio_hreg0_ops = {
    .read = dwc2_hreg0_read,
    .write = dwc2_hreg0_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps dwc2_mmio_hreg1_ops = {
    .read = dwc2_hreg1_read,
    .write = dwc2_hreg1_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps dwc2_mmio_pcgreg_ops = {
    .read = dwc2_pcgreg_read,
    .write = dwc2_pcgreg_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps dwc2_mmio_hreg2_ops = {
    .read = dwc2_hreg2_read,
    .write = dwc2_hreg2_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void dwc2_wakeup_endpoint(USBBus *bus, USBEndpoint *ep,
                                 unsigned int stream)
{
    DWC2State *s = container_of(bus, DWC2State, bus);

    /* TODO - do something here? */
    qemu_bh_schedule(s->async_bh);
}

static USBBusOps dwc2_bus_ops = {
    .wakeup_endpoint = dwc2_wakeup_endpoint,
};

static void dwc2_work_timer(void *opaque)
{
    DWC2State *s = opaque;

    DPRINTF("dwc2_work_timer\n");
    qemu_bh_schedule(s->async_bh);
}

/* host controller initialization */
static void dwc2_reset(DWC2State *s)
{
    USBDevice *devs[NB_PORTS];
    int i;

    DPRINTF("dwc2_reset, s %p\n", s);
    timer_del(s->frame_timer);
    qemu_bh_cancel(s->async_bh);

    for (i = 0; i < NB_PORTS; i++) {
        devs[i] = s->ports[i].dev;
        if (devs[i] && devs[i]->attached) {
            usb_detach(&s->ports[i]);
        }
    }

    dwc2_bus_stop(s);

    s->gotgctl = GOTGCTL_BSESVLD | GOTGCTL_ASESVLD | GOTGCTL_CONID_B;
    s->gotgint = 0;
    s->gahbcfg = 0;
    s->gusbcfg = 5 << GUSBCFG_USBTRDTIM_SHIFT;
    s->grstctl = GRSTCTL_AHBIDLE;
    s->gintsts = GINTSTS_CONIDSTSCHNG | GINTSTS_PTXFEMP | GINTSTS_NPTXFEMP |
                 GINTSTS_CURMODE_HOST;
    s->gintmsk = 0;
    s->grxstsr = 0;
    s->grxstsp = 0;
    s->grxfsiz = 1024;
    s->gnptxfsiz = 1024 << FIFOSIZE_DEPTH_SHIFT;
    s->gnptxsts = (4 << FIFOSIZE_DEPTH_SHIFT) | 1024;
    s->gi2cctl = GI2CCTL_I2CDATSE0 | GI2CCTL_ACK;
    s->gpvndctl = 0;
    s->ggpio = 0;
    s->guid = 0;
    s->gsnpsid = 0x4f54294a;
    s->ghwcfg1 = 0;
    s->ghwcfg2 = (8 << GHWCFG2_DEV_TOKEN_Q_DEPTH_SHIFT) |
                 (4 << GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT) |
                 (4 << GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT) |
                 GHWCFG2_DYNAMIC_FIFO |
                 GHWCFG2_PERIO_EP_SUPPORTED |
                 ((NB_CHAN - 1) << GHWCFG2_NUM_HOST_CHAN_SHIFT) |
                 (GHWCFG2_INT_DMA_ARCH << GHWCFG2_ARCHITECTURE_SHIFT) |
                 (GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST << GHWCFG2_OP_MODE_SHIFT);
    s->ghwcfg3 = (4096 << GHWCFG3_DFIFO_DEPTH_SHIFT) |
                 (4 << GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT) |
                 (4 << GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT);
    s->ghwcfg4 = 0;
    s->glpmcfg = 0;
    s->gpwrdn = GPWRDN_PWRDNRSTN;
    s->gdfifocfg = 0;
    s->gadpctl = 0;
    s->grefclk = 0;
    s->gintmsk2 = 0;
    s->gintsts2 = 0;

    s->hptxfsiz = 500 << FIFOSIZE_DEPTH_SHIFT;

    s->hcfg = 2 << HCFG_RESVALID_SHIFT;
    s->hfir = 60000;
    s->hfnum = 0x3fff;
    s->hptxsts = (16 << TXSTS_QSPCAVAIL_SHIFT) | 32768;
    s->haint = 0;
    s->haintmsk = 0;
    s->hprt0 = 0;

    memset(s->hreg1, 0, sizeof(s->hreg1));
    memset(s->pcgreg, 0, sizeof(s->pcgreg));

    s->sof_time = 0;
    s->fsmps = 0x2778;
    s->fi = 11999;
    s->frame_number = 0;

    for (i = 0; i < NB_CHAN; i++) {
        s->packet[i].needs_service = false;
    }

    dwc2_update_irq(s);

    for (i = 0; i < NB_PORTS; i++) {
        s->hprt0 = HPRT0_PWR;
        if (devs[i] && devs[i]->attached) {
            usb_attach(&s->ports[i]);
            usb_device_reset(devs[i]);
        }
    }
}

static void dwc2_realize(DWC2State *s, DeviceState *dev, Error **errp)
{
    Object *obj;
    Error *err = NULL;
    int i;

    DPRINTF("dwc2_realize, s %p dev %p\n", s, dev);
    if (s->portnr > NB_PORTS) {
        error_setg(errp, "Too many ports! Max port number is %d",
                   NB_PORTS);
        return;
    }

    obj = object_property_get_link(OBJECT(dev), "dma-mr", &err);
    if (err || obj == NULL) {
        error_setg(errp, "dwc2: required dma-mr link not found: %s",
                   error_get_pretty(err));
        return;
    }

    s->dma_mr = MEMORY_REGION(obj);
    address_space_init(&s->dma_as, s->dma_mr, NULL);

    usb_bus_new(&s->bus, sizeof(s->bus), &dwc2_bus_ops, dev);
    for (i = 0; i < s->portnr; i++) {
        usb_register_port(&s->bus, &s->ports[i], s, i, &dwc2_port_ops,
                          USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL |
                          USB_SPEED_MASK_HIGH);
        s->ports[i].dev = 0;
    }

    s->frame_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, dwc2_work_timer, s);
    s->async_bh = qemu_bh_new(dwc2_work_bh, s);
    s->working = false;
    s->next_chan = 0;
    s->device = dev;
}

static void dwc2_init(DWC2State *s, DeviceState *dev)
{
    DPRINTF("dwc2_init, s %p dev %p\n", s, dev);

    s->usb_frame_time = NANOSECONDS_PER_SECOND / 1000;          /* 1000000 */
    if (NANOSECONDS_PER_SECOND >= USB_HZ_FS) {
        s->usb_bit_time = NANOSECONDS_PER_SECOND / USB_HZ_FS;   /* 83.3 */
    } else {
        s->usb_bit_time = 1;
    }

    s->fi = 11999;

    memory_region_init(&s->mem, OBJECT(dev), "dwc2", DWC2_MMIO_SIZE);
    memory_region_init_io(&s->mem_glbreg, OBJECT(dev), &dwc2_mmio_glbreg_ops, s,
                          "global", 0x70);
    memory_region_init_io(&s->mem_fszreg, OBJECT(dev), &dwc2_mmio_fszreg_ops, s,
                          "hptxfsiz", 0x4);
    memory_region_init_io(&s->mem_hreg0, OBJECT(dev), &dwc2_mmio_hreg0_ops, s,
                          "host", 0x44);
    memory_region_init_io(&s->mem_hreg1, OBJECT(dev), &dwc2_mmio_hreg1_ops, s,
                          "host channels", 0x20 * NB_CHAN);
    memory_region_init_io(&s->mem_pcgreg, OBJECT(dev), &dwc2_mmio_pcgreg_ops, s,
                          "power/clock", 0x8);
    memory_region_init_io(&s->mem_hreg2, OBJECT(dev), &dwc2_mmio_hreg2_ops, s,
                          "host fifos", NB_CHAN * 0x1000);

    memory_region_add_subregion(&s->mem, s->glbregbase, &s->mem_glbreg);
    memory_region_add_subregion(&s->mem, s->fszregbase, &s->mem_fszreg);
    memory_region_add_subregion(&s->mem, s->hreg0base, &s->mem_hreg0);
    memory_region_add_subregion(&s->mem, s->hreg1base, &s->mem_hreg1);
    memory_region_add_subregion(&s->mem, s->pcgregbase, &s->mem_pcgreg);
    memory_region_add_subregion(&s->mem, s->hreg2base, &s->mem_hreg2);

#ifdef DWC2_DO_SOFS
    s->eof_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                dwc2_frame_boundary, s);
#endif
}

static void dwc2_sysbus_reset(DeviceState *dev)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    DWC2State *s = DWC2_USB(d);

    DPRINTF("dwc2_sysbus_reset, dev %p d %p s %p\n", dev, d, s);
    dwc2_reset(s);
}

static void dwc2_sysbus_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    DWC2State *s = DWC2_USB(dev);

    DPRINTF("dwc2_sysbus_realize, dev %p d %p s %p\n", dev, d, s);
    s->glbregbase = 0;
    s->fszregbase = 0x0100;
    s->hreg0base = 0x0400;
    s->hreg1base = 0x0500;
    s->pcgregbase = 0x0e00;
    s->hreg2base = 0x1000;
    s->portnr = NB_PORTS;
    s->as = &address_space_memory;

    DPRINTF("0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", s->glbregbase, s->fszregbase,
            s->hreg0base, s->hreg1base, s->pcgregbase, s->hreg2base);
    dwc2_realize(s, dev, errp);
    dwc2_init(s, dev);
    sysbus_init_irq(d, &s->irq);
    sysbus_init_mmio(d, &s->mem);
}

static void dwc2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    DPRINTF("dwc2_class_init, class %p dc %p\n", klass, dc);
    dc->realize = dwc2_sysbus_realize;
    dc->reset = dwc2_sysbus_reset;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo dwc2_usb_type_info = {
     .name          = TYPE_DWC2_USB,
     .parent        = TYPE_SYS_BUS_DEVICE,
     .instance_size = sizeof(DWC2State),
     .class_init    = dwc2_class_init,
};

static void dwc2_usb_register_types(void)
{
    DPRINTF("dwc2_usb_register_types\n");
    type_register_static(&dwc2_usb_type_info);
}

type_init(dwc2_usb_register_types)
