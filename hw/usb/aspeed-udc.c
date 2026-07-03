/*
 * ASPEED USB Device Controller (UDC)
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Models the ASPEED USB Device Controller (UDC), driven by the Linux
 * "aspeed_udc" gadget driver. It implements one control endpoint (EP0) and
 * 4 programmable endpoints.
 *
 * The model has two faces:
 *   - a SysBus device exposing the MMIO register interface, the interrupt and
 *     the integrated DMA engine to the guest gadget driver (the "system bus
 *     device" section below);
 *   - a USBDevice presented on a host controller's bus, which forwards host
 *     transactions to the gadget by raising the matching controller interrupts
 *     and completes them once the gadget responds via MMIO (the "USB device"
 *     section below).
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "hw/usb/aspeed-udc.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "system/dma.h"
#include "system/address-spaces.h"
#include "trace.h"

#define AST_UDC_EP0_MAXPKT      64

/* Root / Global registers (offset from the controller base) */
REG32(UDC_FUNC_CTRL, 0x00)
    FIELD(UDC_FUNC_CTRL, UPSTREAM_EN,       0, 1)
    FIELD(UDC_FUNC_CTRL, UPSTREAM_FS,       1, 1)
    FIELD(UDC_FUNC_CTRL, STOP_CLK_SUSPEND,  2, 1)
    FIELD(UDC_FUNC_CTRL, AUTO_REMOTE_WKUP,  3, 1)
    FIELD(UDC_FUNC_CTRL, REMOTE_WKUP_EN,    4, 1)
    FIELD(UDC_FUNC_CTRL, TEST_MODE,         8, 3)
    FIELD(UDC_FUNC_CTRL, PHY_RESET_DIS,    11, 1)
    FIELD(UDC_FUNC_CTRL, EP_LONG_DESC,     18, 1)
    FIELD(UDC_FUNC_CTRL, PHY_CLK_EN,       31, 1)
REG32(UDC_CONFIG, 0x04)
    FIELD(UDC_CONFIG, DEV_ADDR,             0, 7)
REG32(UDC_IER, 0x08)
REG32(UDC_ISR, 0x0C)
    FIELD(UDC_ISR, EP0_SETUP,               0, 1)
    FIELD(UDC_ISR, EP0_OUT_ACK,             1, 1)
    FIELD(UDC_ISR, EP0_OUT_NAK,             2, 1)
    FIELD(UDC_ISR, EP0_IN_ACK,              3, 1)
    FIELD(UDC_ISR, EP0_IN_NAK,              4, 1)
    FIELD(UDC_ISR, BUS_RESET,               6, 1)
    FIELD(UDC_ISR, SUSPEND,                 7, 1)
    FIELD(UDC_ISR, RESUME,                  8, 1)
    FIELD(UDC_ISR, EP_POOL_ACK,            16, 1)
    FIELD(UDC_ISR, EP_POOL_NAK,            17, 1)
REG32(UDC_EP_ACK_IER, 0x10)
REG32(UDC_EP_NAK_IER, 0x14)
REG32(UDC_EP_ACK_ISR, 0x18)
REG32(UDC_EP_NAK_ISR, 0x1C)
REG32(UDC_DEV_RESET, 0x20)
    FIELD(UDC_DEV_RESET, ROOT,              0, 1)
    FIELD(UDC_DEV_RESET, DMA,               8, 1)
    FIELD(UDC_DEV_RESET, EP_POOL,           9, 1)
REG32(UDC_STS, 0x24)
    FIELD(UDC_STS, HIGHSPEED,              27, 1)
REG32(UDC_EP_DATA, 0x28)
REG32(UDC_ISO_TX_FAIL, 0x2C)
REG32(UDC_EP0_CTRL, 0x30)
    FIELD(UDC_EP0_CTRL, STALL,              0, 1)
    FIELD(UDC_EP0_CTRL, TX_RDY,             1, 1)
    FIELD(UDC_EP0_CTRL, RX_RDY,             2, 1)
    FIELD(UDC_EP0_CTRL, TX_LEN,             8, 7)
    FIELD(UDC_EP0_CTRL, RX_LEN,            16, 7)
REG32(UDC_EP0_DATA_BUFF, 0x34)
/* EP0 SETUP packet buffer: SETUP0 = bytes 0...3, SETUP1 = bytes 4...7 */
REG32(UDC_SETUP0, 0x80)
REG32(UDC_SETUP1, 0x84)

/* Per programmable-endpoint registers (offset from the EP register base) */
REG32(EP_CONFIG, 0x00)
    FIELD(EP_CONFIG, ENABLE,                0, 1)
    FIELD(EP_CONFIG, DIR_OUT,               4, 1)
    FIELD(EP_CONFIG, TYPE,                  5, 2)
    FIELD(EP_CONFIG, EP_NUM,                8, 4)
    FIELD(EP_CONFIG, STALL,                12, 1)
    FIELD(EP_CONFIG, AUTO_TOGGLE_DIS,      13, 1)
    FIELD(EP_CONFIG, MAX_PKT,              16, 10)
REG32(EP_DMA_CTRL, 0x04)
    FIELD(EP_DMA_CTRL, DESC_OP_EN,          0, 1)
    FIELD(EP_DMA_CTRL, SINGLE_STAGE,        1, 1)
    FIELD(EP_DMA_CTRL, RESET,               2, 1)
    FIELD(EP_DMA_CTRL, IN_LONG_MODE,        3, 1)
    FIELD(EP_DMA_CTRL, PROC_STS,            4, 4)
REG32(EP_DMA_BUFF, 0x08)
REG32(EP_DMA_STS, 0x0C)
    FIELD(EP_DMA_STS, WPTR,                 0, 8)
    FIELD(EP_DMA_STS, RPTR,                 8, 8)
    FIELD(EP_DMA_STS, TX_SIZE,             16, 11)

/* DMA descriptor ring (256-stage mode) and descriptor data limits */
#define ASPEED_UDC_DESCS_COUNT  256
#define ASPEED_UDC_DESC_MAX_LEN 4096

/* DMA processing-status idle codes (EP_DMA_CTRL PROC_STS field) */
#define EP_DMA_CTRL_STS_RX_IDLE 0x0
#define EP_DMA_CTRL_STS_TX_IDLE 0x8

/* DMA descriptor (DES1) fields, in guest memory */
#define AST_EP_DESC1_IN_LEN(w1)     ((w1) & 0x1fff)
/* interrupt-on-completion */
#define AST_EP_DESC1_INTR           BIT(31)

/* Device-reset default: root, DMA and EP-pool soft-reset bits set (0x301) */
#define UDC_DEV_RESET_DEFAULT \
    (R_UDC_DEV_RESET_ROOT_MASK | R_UDC_DEV_RESET_DMA_MASK | \
     R_UDC_DEV_RESET_EP_POOL_MASK)

static bool aspeed_udc_ep_serve_in(AspeedUDCState *s, unsigned ep,
                                   USBPacket *p, bool *complete);
static bool aspeed_udc_ep_deliver_out(AspeedUDCState *s, unsigned ep,
                                      USBPacket *p);

static void aspeed_udc_update_irq(AspeedUDCState *s)
{
    bool level;

    level = (s->regs[R_UDC_ISR] & s->regs[R_UDC_IER]) ||
            (s->regs[R_UDC_EP_ACK_ISR] & s->regs[R_UDC_EP_ACK_IER]) ||
            (s->regs[R_UDC_EP_NAK_ISR] & s->regs[R_UDC_EP_NAK_IER]);

    trace_aspeed_udc_irq(s->regs[R_UDC_ISR], s->regs[R_UDC_IER], level);
    qemu_set_irq(s->irq, level);
}

static void aspeed_udc_raise_isr(AspeedUDCState *s, uint32_t mask)
{
    s->regs[R_UDC_ISR] |= mask;
    aspeed_udc_update_irq(s);
}

/* Signal completion of a programmable-endpoint transaction to the gadget */
static void aspeed_udc_raise_ep_ack(AspeedUDCState *s, unsigned ep)
{
    trace_aspeed_udc_ep_ack(ep);
    s->regs[R_UDC_EP_ACK_ISR] |= BIT(ep);
    s->regs[R_UDC_ISR] |= R_UDC_ISR_EP_POOL_ACK_MASK;
    aspeed_udc_update_irq(s);
}

/*
 * System bus device: MMIO register interface (guest gadget-driver facing)
 */

/* Connect/disconnect the gadget from the host bus (UBD00 upstream enable) */
static void aspeed_udc_set_pullup(AspeedUDCState *s, bool on)
{
    USBDevice *udev;
    Error *err = NULL;

    if (!s->usbgadget) {
        /* no gadget device bound to this controller */
        return;
    }

    udev = USB_DEVICE(s->usbgadget);
    if (!udev->port) {
        /* not attached to a host controller bus */
        return;
    }

    trace_aspeed_udc_pullup(on, udev->attached);
    if (on && !udev->attached) {
        usb_device_attach(udev, &err);
        if (err) {
            warn_report_err(err);
        }
    } else if (!on && udev->attached) {
        usb_device_detach(udev);
    }
}

/* Complete the in-flight EP0 control transfer back to the host */
static void aspeed_udc_ep0_complete(AspeedUDCState *s, uint32_t len)
{
    USBPacket *p = s->ep0_packet;

    if (!p) {
        return;
    }

    s->ep0_packet = NULL;
    p->actual_length = s->ep0_dir_in ? MIN(len, s->ep0_setup_len)
                                     : s->ep0_setup_len;
    p->status = USB_RET_SUCCESS;
    trace_aspeed_udc_ep0_complete(s->ep0_dir_in, p->actual_length);
    usb_generic_async_ctrl_complete(USB_DEVICE(s->usbgadget), p);
}

/*
 * The gadget driver drives EP0 by writing UBD30. Translate those writes into
 * data movement to/from the deferred host control packet plus the matching
 * ACK interrupts the gadget expects.
 */
static void aspeed_udc_ep0_ctrl_write(AspeedUDCState *s, uint32_t val)
{
    uint32_t buf_addr = s->regs[R_UDC_EP0_DATA_BUFF];
    uint32_t store = val & R_UDC_EP0_CTRL_STALL_MASK;
    uint32_t txlen;
    USBPacket *p;
    uint32_t n;

    trace_aspeed_udc_ep0_ctrl_write(val, s->ep0_dir_in, s->ep0_offset);

    /* Gadget stalled EP0: fail the pending control transfer */
    if (val & R_UDC_EP0_CTRL_STALL_MASK) {
        p = s->ep0_packet;

        if (p) {
            s->ep0_packet = NULL;
            p->status = USB_RET_STALL;
            usb_generic_async_ctrl_complete(USB_DEVICE(s->usbgadget), p);
        }
        s->regs[R_UDC_EP0_CTRL] = store;
        return;
    }

    if (val & R_UDC_EP0_CTRL_TX_RDY_MASK) {
        txlen = FIELD_EX32(val, UDC_EP0_CTRL, TX_LEN);

        if (s->ep0_dir_in && s->ep0_packet) {
            /* IN data stage: copy a chunk from the gadget's DMA buffer */
            n = MIN(txlen, s->ep0_setup_len - s->ep0_offset);

            if (n) {
                dma_memory_read(&address_space_memory, buf_addr,
                                s->ep0_data + s->ep0_offset, n,
                                MEMTXATTRS_UNSPECIFIED);
            }
            s->ep0_offset += n;
            aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_IN_ACK_MASK);

            if (txlen < AST_UDC_EP0_MAXPKT ||
                s->ep0_offset >= s->ep0_setup_len) {
                aspeed_udc_ep0_complete(s, s->ep0_offset);
            }
        } else {
            /* Zero-length status IN for an OUT / no-data control transfer */
            aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_IN_ACK_MASK);
            aspeed_udc_ep0_complete(s, s->ep0_offset);
        }

    } else if (val & R_UDC_EP0_CTRL_RX_RDY_MASK) {
        if (s->ep0_dir_in) {
            /* Status stage OUT (zero length) following IN data */
            aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_OUT_ACK_MASK);
        } else if (s->ep0_packet) {
            /* OUT data stage: hand a chunk of host data to the gadget */
            n = MIN(s->ep0_setup_len - s->ep0_offset,
                    AST_UDC_EP0_MAXPKT);

            if (n) {
                dma_memory_write(&address_space_memory, buf_addr,
                                 s->ep0_data + s->ep0_offset, n,
                                 MEMTXATTRS_UNSPECIFIED);
            }
            s->ep0_offset += n;
            store = FIELD_DP32(store, UDC_EP0_CTRL, RX_LEN, n);
            aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_OUT_ACK_MASK);
        }
    }

    s->regs[R_UDC_EP0_CTRL] = store;
}

/* The upstream-enable bit connects/disconnects the gadget from the host bus */
static void aspeed_udc_func_ctrl_write(AspeedUDCState *s, uint32_t val)
{
    bool was_on = FIELD_EX32(s->regs[R_UDC_FUNC_CTRL],
                             UDC_FUNC_CTRL, UPSTREAM_EN);
    bool now_on = FIELD_EX32(val, UDC_FUNC_CTRL, UPSTREAM_EN);

    s->regs[R_UDC_FUNC_CTRL] = val;
    if (now_on != was_on) {
        aspeed_udc_set_pullup(s, now_on);
    }
}

static uint64_t aspeed_udc_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedUDCState *s = ASPEED_UDC(opaque);
    uint64_t val = s->regs[offset >> 2];

    trace_aspeed_udc_read(offset, val);
    return val;
}

static void aspeed_udc_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size)
{
    AspeedUDCState *s = ASPEED_UDC(opaque);
    uint32_t reg = offset >> 2;
    uint32_t val = data;

    trace_aspeed_udc_write(offset, val);

    switch (reg) {
    case R_UDC_ISR:
    case R_UDC_EP_ACK_ISR:
    case R_UDC_EP_NAK_ISR:
        /* Status registers are write-1-to-clear */
        s->regs[reg] &= ~val;
        break;
    case R_UDC_FUNC_CTRL:
        aspeed_udc_func_ctrl_write(s, val);
        break;
    case R_UDC_EP0_CTRL:
        aspeed_udc_ep0_ctrl_write(s, val);
        return;
    default:
        s->regs[reg] = val;
        break;
    }

    aspeed_udc_update_irq(s);
}

static const MemoryRegionOps aspeed_udc_ops = {
    .read = aspeed_udc_read,
    .write = aspeed_udc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * Descriptor-mode IN kick: the gadget wrote EP_DMA_STS to advance its write
 * pointer (queue more IN data). Adopt the write pointer, keep our read pointer
 * (the DMA engine owns it), then serve and complete a parked IN poll if any.
 */
static void aspeed_udc_ep_dma_kick(AspeedUDCState *s, unsigned ep, uint32_t val)
{
    AspeedUDCEP *e = &s->ep[ep];
    uint32_t rptr = FIELD_EX32(e->regs[R_EP_DMA_STS], EP_DMA_STS, RPTR);
    uint32_t rptr_w = FIELD_EX32(val, EP_DMA_STS, RPTR);
    uint32_t wptr = FIELD_EX32(val, EP_DMA_STS, WPTR);
    uint32_t sts = e->regs[R_EP_DMA_STS];
    USBPacket *p = e->pkt;
    bool complete;

    sts = FIELD_DP32(sts, EP_DMA_STS, WPTR, wptr);

    /*
     * A normal kick writes only the write pointer (bits[7:0]); the read-pointer
     * field is zero and is ours to advance. Only an explicit non-zero read
     * pointer equal to the write pointer (the driver draining the ring, e.g. on
     * reset) resets our read pointer. Testing rptr==wptr alone would wrongly
     * match a normal kick whose write pointer just wrapped to 0, resetting the
     * ring and stranding a parked packet until the host times out.
     */
    if (rptr_w != 0 && rptr_w == wptr) {
        rptr = rptr_w;
        e->desc_off = 0;
    }
    e->regs[R_EP_DMA_STS] = FIELD_DP32(sts, EP_DMA_STS, RPTR, rptr);

    if (p && rptr != wptr) {
        if (aspeed_udc_ep_serve_in(s, ep, p, &complete)) {
            aspeed_udc_raise_ep_ack(s, ep);
        }
        if (complete) {
            e->pkt = NULL;
            p->status = USB_RET_SUCCESS;
            usb_packet_complete(USB_DEVICE(s->usbgadget), p);
        }
    }
}

/*
 * Single-stage OUT arm: the gadget wrote EP_DMA_STS to offer a receive
 * buffer. If a host OUT packet is parked waiting for it,
 * deliver into the buffer now and complete it (or keep it parked if the host
 * data phase is larger than this one armed chunk).
 */
static void aspeed_udc_ep_out_kick(AspeedUDCState *s, unsigned ep)
{
    AspeedUDCEP *e = &s->ep[ep];
    USBPacket *p = e->pkt;

    if (!p || !FIELD_EX32(e->regs[R_EP_DMA_STS], EP_DMA_STS, WPTR)) {
        return;
    }
    if (aspeed_udc_ep_deliver_out(s, ep, p)) {
        e->pkt = NULL;
        p->status = USB_RET_SUCCESS;
        usb_packet_complete(USB_DEVICE(s->usbgadget), p);
    }
}

static uint64_t aspeed_udc_ep_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedUDCEP *e = opaque;
    uint64_t val = e->regs[offset >> 2];

    trace_aspeed_udc_ep_read(e->index, offset, val);
    return val;
}

static void aspeed_udc_ep_write(void *opaque, hwaddr offset, uint64_t data,
                                unsigned size)
{
    AspeedUDCEP *e = opaque;
    AspeedUDCState *s = e->udc;
    uint32_t reg = offset >> 2;
    uint32_t val = data;
    uint32_t ctrl;

    trace_aspeed_udc_ep_write(e->index, offset, val);

    /*
     * A write to EP_DMA_STS is a DMA kick. In descriptor mode (IN) it advances
     * the ring and resumes a parked IN poll; in single-stage mode (OUT) it
     * arms a receive buffer and delivers a parked OUT packet. Every other
     * EP register is plain storage.
     */
    if (reg == R_EP_DMA_STS) {
        ctrl = e->regs[R_EP_DMA_CTRL];

        if (FIELD_EX32(ctrl, EP_DMA_CTRL, DESC_OP_EN)) {
            /* descriptor-mode IN */
            aspeed_udc_ep_dma_kick(s, e->index, val);
        } else {
            /* single-stage OUT */
            e->regs[reg] = val;
            aspeed_udc_ep_out_kick(s, e->index);
        }
    } else {
        e->regs[reg] = val;
    }
}

static const MemoryRegionOps aspeed_udc_ep_ops = {
    .read = aspeed_udc_ep_read,
    .write = aspeed_udc_ep_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void aspeed_udc_realize(DeviceState *dev, Error **errp)
{
    AspeedUDCState *s = ASPEED_UDC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    s->regs = g_new0(uint32_t, ASPEED_UDC_NR_REGS);

    memory_region_init(&s->iomem, OBJECT(s), TYPE_ASPEED_UDC,
                       ASPEED_UDC_REG_SIZE);

    /* Root/global registers occupy the low part of the window */
    memory_region_init_io(&s->reg_mr, OBJECT(s), &aspeed_udc_ops, s,
                          TYPE_ASPEED_UDC ".regs", ASPEED_UDC_NR_REGS << 2);
    memory_region_add_subregion(&s->iomem, 0, &s->reg_mr);

    /* Each programmable endpoint has its own register bank */
    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        g_autofree char *name = g_strdup_printf(TYPE_ASPEED_UDC ".ep%d", i);

        s->ep[i].udc = s;
        s->ep[i].index = i;
        s->ep[i].regs = g_new0(uint32_t, ASPEED_UDC_EP_NR_REGS);
        memory_region_init_io(&s->ep[i].mr, OBJECT(s), &aspeed_udc_ep_ops,
                              &s->ep[i], name, ASPEED_UDC_EP_NR_REGS << 2);
        memory_region_add_subregion(&s->iomem, ASPEED_UDC_EP_REG_BASE +
                                    i * ASPEED_UDC_EP_REG_SIZE, &s->ep[i].mr);
    }

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void aspeed_udc_reset_hold(Object *obj, ResetType type)
{
    AspeedUDCState *s = ASPEED_UDC(obj);
    int i;

    memset(s->regs, 0, ASPEED_UDC_NR_REGS * sizeof(uint32_t));
    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        memset(s->ep[i].regs, 0, ASPEED_UDC_EP_NR_REGS * sizeof(uint32_t));
        s->ep[i].pkt = NULL;
        s->ep[i].desc_off = 0;
    }
    s->regs[R_UDC_DEV_RESET] = UDC_DEV_RESET_DEFAULT;
    s->ep0_packet = NULL;

    /*
     * A machine reset (e.g. a guest reboot) wipes the gadget driver, so a
     * device still attached to the host bus from before the reset is now
     * dead. Detach it, otherwise the rebooted host tries to re-enumerate a
     * gadget with no driver behind it and fails with -110. It re-attaches
     * when the new gadget asserts pull-up (UBD00 upstream-enable).
     */
    if (s->usbgadget) {
        USBDevice *udev = USB_DEVICE(s->usbgadget);

        if (udev->attached) {
            usb_device_detach(udev);
        }
    }
}

static void aspeed_udc_unrealize(DeviceState *dev)
{
    AspeedUDCState *s = ASPEED_UDC(dev);
    int i;

    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        g_free(s->ep[i].regs);
    }
    g_free(s->regs);
}

static void aspeed_udc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "ASPEED USB Device Controller";
    dc->realize = aspeed_udc_realize;
    dc->unrealize = aspeed_udc_unrealize;
    rc->phases.hold = aspeed_udc_reset_hold;
}

/*
 * USB device: gadget presented on a host controller's bus
 *
 * These callbacks run in the context of the host controller. They translate
 * host transactions into the controller interrupts/state the gadget driver
 * expects, then defer (USB_RET_ASYNC) until the driver responds through the
 * MMIO register interface above.
 */

/* Locate the programmable endpoint matching a host transaction */
static int aspeed_udc_find_ep(AspeedUDCState *s, int ep_nr, bool is_out)
{
    uint32_t cfg;
    int i;

    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        cfg = s->ep[i].regs[R_EP_CONFIG];

        if (!FIELD_EX32(cfg, EP_CONFIG, ENABLE) ||
            FIELD_EX32(cfg, EP_CONFIG, EP_NUM) != ep_nr) {
            continue;
        }
        if (!!FIELD_EX32(cfg, EP_CONFIG, DIR_OUT) == is_out) {
            return i;
        }
    }
    return -1;
}

/*
 * IN endpoint, descriptor-list mode: serve the descriptors the gadget queued
 * (read pointer .. write pointer) into the host IN packet.
 *
 * The host controller coalesces a multi-packet bulk IN into one packet whose
 * iovec may span several descriptors, so drain as many queued descriptors as
 * fit. A descriptor bigger than the remaining room is served partially and
 * resumed on the next poll via desc_off; the read pointer only advances once a
 * descriptor is fully consumed (so the gadget driver, which reads the read
 * pointer back, learns exactly what completed).
 *
 * *complete is set true when the host transaction is finished (packet full, or
 * a short/zero-length descriptor ended the gadget's transfer) and false when
 * the ring ran dry with the packet still short - the caller then keeps the
 * packet parked and resumes on the next kick, rather than completing it short
 * (a maxpacket-multiple short read is not seen as end-of-transfer by the host,
 * which would then wait forever for data the gadget considers already sent).
 *
 * Returns true when the gadget should be signalled (EP ACK): the ring drained
 * (its descriptor handler waits for read==write) or a descriptor asked for an
 * interrupt.
 */
static bool aspeed_udc_ep_serve_in(AspeedUDCState *s, unsigned ep,
                                   USBPacket *p, bool *complete)
{
    QEMUIOVector *pktiov = p->combined ? &p->combined->iov : &p->iov;
    AspeedUDCEP *e = &s->ep[ep];
    uint32_t mps = FIELD_EX32(e->regs[R_EP_CONFIG], EP_CONFIG, MAX_PKT);
    uint32_t base = e->regs[R_EP_DMA_BUFF];
    uint32_t sts = e->regs[R_EP_DMA_STS];
    uint32_t wptr = FIELD_EX32(sts, EP_DMA_STS, WPTR);
    uint32_t rptr = FIELD_EX32(sts, EP_DMA_STS, RPTR);
    uint32_t remaining;
    uint8_t buf[1024];
    /* des_0: buffer base, des_1: control/len */
    uint32_t desc[2];
    uint32_t copied;
    uint32_t chunk;
    uint32_t dlen;
    uint32_t room;
    uint32_t seg;
    uint32_t off;
    uint32_t w0;
    uint32_t w1;
    bool done = false;
    bool ack = false;

    if (mps == 0) {
        /* a MAX_PKT field of 0 means 1024 bytes */
        mps = 1024;
    }

    trace_aspeed_udc_ep_data_in(ep, rptr, wptr, pktiov->size);

    while (rptr != wptr) {
        copied = 0;
        dma_memory_read(&address_space_memory, base + rptr * sizeof(desc),
                        desc, sizeof(desc), MEMTXATTRS_UNSPECIFIED);
        w0 = le32_to_cpu(desc[0]);
        w1 = le32_to_cpu(desc[1]);
        dlen = AST_EP_DESC1_IN_LEN(w1);
        off = e->desc_off;
        remaining = dlen > off ? dlen - off : 0;
        room = pktiov->size > (uint32_t)p->actual_length ?
               pktiov->size - (uint32_t)p->actual_length : 0;
        chunk = MIN(remaining, room);

        while (copied < chunk) {
            seg = MIN(chunk - copied, sizeof(buf));
            dma_memory_read(&address_space_memory, w0 + off + copied, buf, seg,
                            MEMTXATTRS_UNSPECIFIED);
            usb_packet_copy(p, buf, seg);
            copied += seg;
        }
        e->desc_off = off + chunk;

        if (e->desc_off < dlen) {
            /* Room exhausted mid-descriptor: the packet is full */
            done = true;
            break;
        }

        /* Descriptor fully served: advance the read pointer */
        rptr = (rptr + 1) % ASPEED_UDC_DESCS_COUNT;
        e->desc_off = 0;
        if (w1 & AST_EP_DESC1_INTR) {
            ack = true;
        }
        /* short/zero-length stage ends the transfer */
        if (dlen < mps) {
            done = true;
            break;
        }
        if ((uint32_t)p->actual_length >= pktiov->size) {
            done = true;
            break;
        }
    }

    e->regs[R_EP_DMA_STS] = FIELD_DP32(sts, EP_DMA_STS, RPTR, rptr);
    e->regs[R_EP_DMA_CTRL] = FIELD_DP32(e->regs[R_EP_DMA_CTRL], EP_DMA_CTRL,
                                        PROC_STS, EP_DMA_CTRL_STS_TX_IDLE);
    /* The gadget driver completes its request when the ring drains */
    if (rptr == wptr) {
        ack = true;
    }

    *complete = done;
    return ack;
}

static void aspeed_udc_ep_data_in(AspeedUDCState *s, unsigned ep, USBPacket *p)
{
    AspeedUDCEP *e = &s->ep[ep];
    uint32_t sts = e->regs[R_EP_DMA_STS];
    uint32_t rptr = FIELD_EX32(sts, EP_DMA_STS, RPTR);
    uint32_t wptr = FIELD_EX32(sts, EP_DMA_STS, WPTR);
    bool complete;

    if (rptr == wptr) {
        /*
         * Nothing queued yet. Park the packet as ASYNC rather than NAKing: the
         * gadget will kick shortly and the kick handler serves and completes
         * it, waking the host immediately. NAK would leave the host to re-poll
         * on its much slower async-schedule timer, collapsing bulk throughput.
         */
        e->pkt = p;
        p->status = USB_RET_ASYNC;
        return;
    }

    if (aspeed_udc_ep_serve_in(s, ep, p, &complete)) {
        aspeed_udc_raise_ep_ack(s, ep);
    }
    if (complete) {
        p->status = USB_RET_SUCCESS;
    } else {
        /*
         * Ring ran dry before the request was satisfied: keep it parked and
         * resume from the kick that queues the next chunk.
         */
        e->pkt = p;
        p->status = USB_RET_ASYNC;
    }
}

/*
 * OUT endpoint, single-stage mode: deliver host OUT data into the buffer the
 * gadget armed (EP_DMA_BUFF + EP_DMA_STS). The host controller may
 * hand the whole OUT data phase as one packet larger than the armed chunk, so
 * copy up to TX_SIZE bytes starting where the previous call left off
 * (p->actual_length); the caller keeps the packet parked across re-arms until
 * it is fully drained. Returns true once the packet is fully delivered.
 */
static bool aspeed_udc_ep_deliver_out(AspeedUDCState *s, unsigned ep,
                                      USBPacket *p)
{
    AspeedUDCEP *e = &s->ep[ep];
    uint32_t buf_addr = e->regs[R_EP_DMA_BUFF];
    uint32_t chunk = FIELD_EX32(e->regs[R_EP_DMA_STS], EP_DMA_STS, TX_SIZE);
    uint32_t remaining = p->iov.size - (uint32_t)p->actual_length;
    uint32_t len = MIN(remaining, chunk);
    uint8_t buf[ASPEED_UDC_DESC_MAX_LEN];

    if (buf_addr && len) {
        len = MIN(len, sizeof(buf));
        usb_packet_copy(p, buf, len);
        dma_memory_write(&address_space_memory, buf_addr, buf, len,
                         MEMTXATTRS_UNSPECIFIED);
    }

    /* Report the received length; clearing WPTR disarms the receive buffer */
    e->regs[R_EP_DMA_STS] = FIELD_DP32(0, EP_DMA_STS, TX_SIZE, len);
    e->regs[R_EP_DMA_CTRL] = FIELD_DP32(e->regs[R_EP_DMA_CTRL], EP_DMA_CTRL,
                                        PROC_STS, EP_DMA_CTRL_STS_RX_IDLE);
    aspeed_udc_raise_ep_ack(s, ep);

    return (uint32_t)p->actual_length >= p->iov.size;
}

static void aspeed_udc_ep_data_out(AspeedUDCState *s, unsigned ep, USBPacket *p)
{
    AspeedUDCEP *e = &s->ep[ep];
    uint32_t sts = e->regs[R_EP_DMA_STS];

    trace_aspeed_udc_ep_data_out(ep, FIELD_EX32(sts, EP_DMA_STS, WPTR),
                                 FIELD_EX32(sts, EP_DMA_STS, TX_SIZE),
                                 p->iov.size);
    if (!FIELD_EX32(sts, EP_DMA_STS, WPTR)) {
        /*
         * No receive buffer armed yet. Park the packet as ASYNC rather than
         * NAKing: delivering into a stale buffer would lose the packet (e.g. a
         * mass-storage CBW), and NAK would fall back to the slow async re-poll.
         * aspeed_udc_ep_out_kick() delivers it once the gadget arms a buffer.
         */
        e->pkt = p;
        p->status = USB_RET_ASYNC;
        return;
    }

    if (aspeed_udc_ep_deliver_out(s, ep, p)) {
        p->status = USB_RET_SUCCESS;
    } else {
        e->pkt = p;
        p->status = USB_RET_ASYNC;
    }
}

static void aspeed_udc_gadget_handle_reset(USBDevice *udev)
{
    AspeedUDCState *s = ASPEED_UDC_GADGET(udev)->udc;

    s->ep0_packet = NULL;
    s->ep0_offset = 0;
    /* EHCI is high speed; let the gadget read the link speed from UBD24 */
    s->regs[R_UDC_STS] = R_UDC_STS_HIGHSPEED_MASK;
    trace_aspeed_udc_reset(s->regs[R_UDC_IER]);
    aspeed_udc_raise_isr(s, R_UDC_ISR_BUS_RESET_MASK);
}

static void aspeed_udc_gadget_handle_control(USBDevice *udev, USBPacket *p,
                                          int request, int value, int index,
                                          int length, uint8_t *data)
{
    AspeedUDCState *s = ASPEED_UDC_GADGET(udev)->udc;
    uint8_t type = request >> 8;
    uint8_t req = request & 0xff;

    /*
     * Reconstruct the 8-byte SETUP packet into the SETUP data buffer where
     * the gadget driver reads it from (controller offset 0x80).
     */
    s->regs[R_UDC_SETUP0] = type | (req << 8) | ((value & 0xffff) << 16);
    s->regs[R_UDC_SETUP1] = (index & 0xffff) | ((length & 0xffff) << 16);

    s->ep0_packet = p;
    s->ep0_data = data;
    s->ep0_setup_len = length;
    s->ep0_offset = 0;
    s->ep0_dir_in = (type & USB_DIR_IN);

    trace_aspeed_udc_ep0_setup(type, req, value, index, length,
                               s->ep0_dir_in, udev->addr);

    /*
     * SET_ADDRESS: adopt the address and complete the (zero-length) status
     * synchronously, exactly like QEMU's generic control handler. The gadget
     * is still notified so its own state machine advances, but the host
     * transfer must not depend on that asynchronous round-trip.
     */
    if (type == 0 && req == USB_REQ_SET_ADDRESS) {
        udev->addr = value;
        s->ep0_packet = NULL;
        aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_SETUP_MASK);
        p->status = USB_RET_SUCCESS;
        return;
    }

    aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_SETUP_MASK);
    p->status = USB_RET_ASYNC;
}

static void aspeed_udc_gadget_handle_data(USBDevice *udev, USBPacket *p)
{
    AspeedUDCState *s = ASPEED_UDC_GADGET(udev)->udc;
    bool is_out = (p->pid == USB_TOKEN_OUT);
    int ep = aspeed_udc_find_ep(s, p->ep->nr, is_out);

    trace_aspeed_udc_handle_data(p->ep->nr, is_out ? "OUT" : "IN",
                                 p->iov.size, ep);
    if (ep < 0) {
        p->status = USB_RET_STALL;
        return;
    }

    if (is_out) {
        aspeed_udc_ep_data_out(s, ep, p);
    } else {
        aspeed_udc_ep_data_in(s, ep, p);
    }
}

static void aspeed_udc_gadget_cancel_packet(USBDevice *udev, USBPacket *p)
{
    AspeedUDCState *s = ASPEED_UDC_GADGET(udev)->udc;
    int i;

    if (s->ep0_packet == p) {
        s->ep0_packet = NULL;
    }
    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        if (s->ep[i].pkt == p) {
            s->ep[i].pkt = NULL;
        }
    }
}

static void aspeed_udc_gadget_realize(USBDevice *udev, Error **errp)
{
    AspeedUDCGadget *dev = ASPEED_UDC_GADGET(udev);

    if (!dev->udc) {
        error_setg(errp, "aspeed-udc-gadget: 'udc' link is not set");
        return;
    }
    /* Bind this gadget to its controller */
    dev->udc->usbgadget = dev;

    /* Connection to the host is driven by the gadget via UBD00 upstream-en */
    udev->auto_attach = 0;
    /* The ASPEED UDC is USB 2.0, so it only runs at High-Speed for now */
    udev->speed = USB_SPEED_HIGH;
    udev->speedmask = USB_SPEED_MASK_HIGH;
}

static const Property aspeed_udc_gadget_props[] = {
    DEFINE_PROP_LINK("udc", AspeedUDCGadget, udc, TYPE_ASPEED_UDC,
                     AspeedUDCState *),
};

static void aspeed_udc_gadget_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    dc->desc           = "ASPEED UDC gadget device";
    uc->product_desc   = "ASPEED UDC gadget";
    uc->realize        = aspeed_udc_gadget_realize;
    uc->handle_reset   = aspeed_udc_gadget_handle_reset;
    uc->handle_control = aspeed_udc_gadget_handle_control;
    uc->handle_data    = aspeed_udc_gadget_handle_data;
    uc->cancel_packet  = aspeed_udc_gadget_cancel_packet;
    device_class_set_props(dc, aspeed_udc_gadget_props);
}

static const TypeInfo aspeed_udc_types[] = {
    {
        .name          = TYPE_ASPEED_UDC,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AspeedUDCState),
        .class_init    = aspeed_udc_class_init,
    },
    {
        .name          = TYPE_ASPEED_UDC_GADGET,
        .parent        = TYPE_USB_DEVICE,
        .instance_size = sizeof(AspeedUDCGadget),
        .class_init    = aspeed_udc_gadget_class_init,
    },
};

DEFINE_TYPES(aspeed_udc_types)
