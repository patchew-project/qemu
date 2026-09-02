/*
 * ASPEED USB Device Controller (UDC)
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Models the ASPEED USB Device Controller (UDC). It implements one control
 * endpoint (EP0) and 4 programmable endpoints.
 *
 * The model has two faces:
 *   - a SysBus device exposing the MMIO register interface, the interrupt and
 *     the integrated DMA engine to the guest gadget driver;
 *   - a USBDevice presented on a host controller's bus, which forwards host
 *     transactions to the guest gadget driver by raising the matching
 *     controller interrupts and completes them once the guest gadget driver
 *     responds via MMIO.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "hw/usb/aspeed-udc.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "system/dma.h"
#include "system/address-spaces.h"
#include "trace.h"

/* Root / Global registers (offset from the controller base) */
REG32(UDC_FUNC_CTRL, 0x00)
    FIELD(UDC_FUNC_CTRL, UPSTREAM_EN,       0, 1)
REG32(UDC_IER, 0x08)
REG32(UDC_ISR, 0x0C)
    FIELD(UDC_ISR, EP_POOL_ACK,            16, 1)
    FIELD(UDC_ISR, BUS_RESET,               6, 1)
    FIELD(UDC_ISR, EP0_IN_ACK,              3, 1)
    FIELD(UDC_ISR, EP0_OUT_ACK,             1, 1)
    FIELD(UDC_ISR, EP0_SETUP,               0, 1)
REG32(UDC_EP_ACK_IER, 0x10)
REG32(UDC_EP_NAK_IER, 0x14)
REG32(UDC_EP_ACK_ISR, 0x18)
REG32(UDC_EP_NAK_ISR, 0x1C)
REG32(UDC_DEV_RESET, 0x20)
    FIELD(UDC_DEV_RESET, EP_POOL,           9, 1)
    FIELD(UDC_DEV_RESET, DMA,               8, 1)
    FIELD(UDC_DEV_RESET, ROOT,              0, 1)
REG32(UDC_STS, 0x24)
    FIELD(UDC_STS, HIGHSPEED,              27, 1)
REG32(UDC_EP0_CTRL, 0x30)
    FIELD(UDC_EP0_CTRL, RX_LEN,            16, 7)
    FIELD(UDC_EP0_CTRL, TX_LEN,             8, 7)
    FIELD(UDC_EP0_CTRL, RX_RDY,             2, 1)
    FIELD(UDC_EP0_CTRL, TX_RDY,             1, 1)
    FIELD(UDC_EP0_CTRL, STALL,              0, 1)
REG32(UDC_EP0_DATA_BUFF, 0x34)
    FIELD(UDC_EP0_DATA_BUFF, BASE_ADDR,     0, 31)
/* EP0 SETUP packet buffer: SETUP0 = bytes 0...3, SETUP1 = bytes 4...7 */
REG32(UDC_SETUP0, 0x80)
REG32(UDC_SETUP1, 0x84)

/* Per programmable-endpoint registers (offset from the EP register base) */
REG32(EP_CONFIG, 0x00)
    FIELD(EP_CONFIG, MAX_PKT,              16, 10)
    FIELD(EP_CONFIG, EP_NUM,                8, 4)
    FIELD(EP_CONFIG, DIR_OUT,               4, 1)
    FIELD(EP_CONFIG, ENABLE,                0, 1)
REG32(EP_DMA_CTRL, 0x04)
    FIELD(EP_DMA_CTRL, PROC_STS,            4, 4)
    FIELD(EP_DMA_CTRL, DESC_OP_EN,          0, 1)
REG32(EP_DMA_BUFF, 0x08)
REG32(EP_DMA_STS, 0x0C)
    FIELD(EP_DMA_STS, PKT_SIZE,            16, 11)
    FIELD(EP_DMA_STS, RPTR,                 8, 8)
    FIELD(EP_DMA_STS, WPTR,                 0, 8)

#define ASPEED_UDC_EP0_MAXPKT      64

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

/*
 * System bus device: MMIO register interface (guest gadget-driver facing)
 */

/* Connect/disconnect the gadget device from the host bus */
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

static void aspeed_udc_ep0_tx_ready(AspeedUDCState *s, uint32_t val)
{
    uint32_t txlen = FIELD_EX32(val, UDC_EP0_CTRL, TX_LEN);
    uint32_t data_buf_addr = s->regs[R_UDC_EP0_DATA_BUFF];
    USBPacket *p;
    uint32_t n;

    if (!s->ep0_dir_in) {
        /* Status stage IN (zero length) for an OUT / no-data transfer */
        aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_IN_ACK_MASK);
        aspeed_udc_ep0_complete(s, s->ep0_offset);
        return;
    }
    /* no control transfer is waiting: nothing to send */
    if (!s->ep0_packet) {
        return;
    }

    /* IN data stage: copy from the guest gadget driver's DMA buffer */
    n = MIN(txlen, s->ep0_setup_len - s->ep0_offset);
    if (n && address_space_read(&s->dram_as, data_buf_addr,
                                MEMTXATTRS_UNSPECIFIED,
                                s->ep0_data + s->ep0_offset,
                                n) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: EP0 IN DMA read failed\n", __func__);
        p = s->ep0_packet;
        s->ep0_packet = NULL;
        p->status = USB_RET_IOERROR;
        usb_generic_async_ctrl_complete(USB_DEVICE(s->usbgadget), p);
        return;
    }
    s->ep0_offset += n;
    aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_IN_ACK_MASK);
    if (txlen < ASPEED_UDC_EP0_MAXPKT || s->ep0_offset >= s->ep0_setup_len) {
        aspeed_udc_ep0_complete(s, s->ep0_offset);
    }
}

static void aspeed_udc_ep0_rx_ready(AspeedUDCState *s)
{
    uint32_t data_buf_addr = s->regs[R_UDC_EP0_DATA_BUFF];
    USBPacket *p;
    uint32_t n;

    if (s->ep0_dir_in) {
        /* Status stage OUT (zero length) for an IN transfer */
        aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_OUT_ACK_MASK);
        return;
    }
    /* no control transfer is waiting: nothing to receive */
    if (!s->ep0_packet) {
        return;
    }

    /* OUT data stage: hand host data to the guest gadget driver */
    n = MIN(s->ep0_setup_len - s->ep0_offset, ASPEED_UDC_EP0_MAXPKT);
    if (n && address_space_write(&s->dram_as, data_buf_addr,
                                 MEMTXATTRS_UNSPECIFIED,
                                 s->ep0_data + s->ep0_offset,
                                 n) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: EP0 OUT DMA write failed\n", __func__);
        p = s->ep0_packet;
        s->ep0_packet = NULL;
        p->status = USB_RET_IOERROR;
        usb_generic_async_ctrl_complete(USB_DEVICE(s->usbgadget), p);
        return;
    }
    s->ep0_offset += n;
    s->regs[R_UDC_EP0_CTRL] = FIELD_DP32(s->regs[R_UDC_EP0_CTRL],
                                         UDC_EP0_CTRL, RX_LEN, n);
    aspeed_udc_raise_isr(s, R_UDC_ISR_EP0_OUT_ACK_MASK);
}

/*
 * The guest gadget driver drives EP0 by writing UDC_EP0_CTRL. Translate
 * those writes into data movement to/from the deferred host control packet
 * plus the matching ACK interrupts the guest gadget driver expects.
 */
static void aspeed_udc_ep0_ctrl_write(AspeedUDCState *s, uint32_t val)
{
    USBPacket *p;

    trace_aspeed_udc_ep0_ctrl_write(val, s->ep0_dir_in, s->ep0_offset);

    if (val & R_UDC_EP0_CTRL_STALL_MASK) {
        /* Gadget stalled EP0: fail the pending control transfer */
        if (s->ep0_packet) {
            p = s->ep0_packet;
            s->ep0_packet = NULL;
            p->status = USB_RET_STALL;
            usb_generic_async_ctrl_complete(USB_DEVICE(s->usbgadget), p);
        }
    } else if (val & R_UDC_EP0_CTRL_TX_RDY_MASK) {
        s->regs[R_UDC_EP0_CTRL] &= ~R_UDC_EP0_CTRL_TX_RDY_MASK;
        aspeed_udc_ep0_tx_ready(s, val);
    } else if (val & R_UDC_EP0_CTRL_RX_RDY_MASK) {
        s->regs[R_UDC_EP0_CTRL] &= ~R_UDC_EP0_CTRL_RX_RDY_MASK;
        aspeed_udc_ep0_rx_ready(s);
    }
}

/* The upstream-enable bit connects/disconnects the gadget device */
static void aspeed_udc_func_ctrl_write(AspeedUDCState *s, uint32_t val)
{
    bool was_on = FIELD_EX32(s->regs[R_UDC_FUNC_CTRL],
                             UDC_FUNC_CTRL, UPSTREAM_EN);
    bool now_on = FIELD_EX32(val, UDC_FUNC_CTRL, UPSTREAM_EN);

    if (now_on != was_on) {
        aspeed_udc_set_pullup(s, now_on);
    }
}

static uint64_t aspeed_udc_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedUDCState *s = ASPEED_UDC(opaque);
    uint32_t reg = offset >> 2;
    uint32_t val;

    val = s->regs[reg];
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
    case R_UDC_FUNC_CTRL:
        val &= 0x000e1fff;
        aspeed_udc_func_ctrl_write(s, val);
        s->regs[R_UDC_FUNC_CTRL] = val;
        break;
    case R_UDC_IER:
    case R_UDC_EP_ACK_IER:
    case R_UDC_EP_NAK_IER:
        s->regs[reg] = val;
        aspeed_udc_update_irq(s);
        break;
    case R_UDC_ISR:
    case R_UDC_EP_ACK_ISR:
    case R_UDC_EP_NAK_ISR:
        s->regs[reg] &= ~val;
        aspeed_udc_update_irq(s);
        break;
    case R_UDC_EP0_CTRL:
        s->regs[reg] = val & (R_UDC_EP0_CTRL_STALL_MASK |
                              R_UDC_EP0_CTRL_TX_RDY_MASK |
                              R_UDC_EP0_CTRL_RX_RDY_MASK |
                              R_UDC_EP0_CTRL_TX_LEN_MASK);
        aspeed_udc_ep0_ctrl_write(s, val);
        break;
    case R_UDC_EP0_DATA_BUFF:
        s->regs[reg] = val & R_UDC_EP0_DATA_BUFF_BASE_ADDR_MASK;
        break;
    default:
        s->regs[reg] = val;
        break;
    }
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

static uint64_t aspeed_udc_ep_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedUDCEP *e = opaque;
    uint32_t reg = offset >> 2;
    uint32_t val;

    val = e->regs[reg];
    trace_aspeed_udc_ep_read(e->index, offset, val);

    return val;
}

static void aspeed_udc_ep_write(void *opaque, hwaddr offset, uint64_t data,
                                unsigned size)
{
    AspeedUDCEP *e = opaque;
    uint32_t reg = offset >> 2;

    trace_aspeed_udc_ep_write(e->index, offset, data);
    e->regs[reg] = data;
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

static void aspeed_udc_reset_hold(Object *obj, ResetType type)
{
    AspeedUDCState *s = ASPEED_UDC(obj);
    USBDevice *udev;
    int i;

    memset(s->regs, 0, sizeof(s->regs));
    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        memset(s->ep[i].regs, 0, sizeof(s->ep[i].regs));
    }

    /* Device-reset default: root, DMA and EP-pool soft-reset bits set */
    s->regs[R_UDC_DEV_RESET] = (R_UDC_DEV_RESET_ROOT_MASK |
                                R_UDC_DEV_RESET_DMA_MASK |
                                R_UDC_DEV_RESET_EP_POOL_MASK);
    s->ep0_packet = NULL;

    /*
     * A guest reboot resets the controller but leaves the USB device
     * attached to the host bus with no guest gadget driver behind it.
     * Detach it, otherwise the rebooted host fails to re-enumerate the
     * driverless gadget device; it re-attaches when the new driver asserts
     * pull-up.
     */
    if (s->usbgadget) {
        udev = USB_DEVICE(s->usbgadget);
        if (udev->attached) {
            usb_device_detach(udev);
        }
    }
}

static void aspeed_udc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedUDCState *s = ASPEED_UDC(dev);
    int i;

    if (!s->dram_mr) {
        error_setg(errp, TYPE_ASPEED_UDC ": 'dram' link not set");
        return;
    }
    address_space_init(&s->dram_as, s->dram_mr, "dram");

    memory_region_init(&s->udc_container, OBJECT(s), TYPE_ASPEED_UDC,
                       ASPEED_UDC_MEM_SIZE);
    memory_region_init_io(&s->root_mr, OBJECT(s), &aspeed_udc_ops, s,
                          TYPE_ASPEED_UDC ".root",
                          ASPEED_UDC_ROOT_NR_REGS << 2);
    memory_region_add_subregion(&s->udc_container, 0, &s->root_mr);

    /* Each programmable endpoint has its own register bank */
    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        g_autofree char *name = g_strdup_printf(TYPE_ASPEED_UDC ".ep%d", i);

        s->ep[i].index = i;
        memory_region_init_io(&s->ep[i].mr, OBJECT(s), &aspeed_udc_ep_ops,
                              &s->ep[i], name, ASPEED_UDC_EP_NR_REGS << 2);
        memory_region_add_subregion(&s->udc_container,
                                    ASPEED_UDC_EP_REG_BASE +
                                    i * (ASPEED_UDC_EP_NR_REGS << 2),
                                    &s->ep[i].mr);
    }

    sysbus_init_mmio(sbd, &s->udc_container);
    sysbus_init_irq(sbd, &s->irq);
}

static const Property aspeed_udc_properties[] = {
    DEFINE_PROP_LINK("dram", AspeedUDCState, dram_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void aspeed_udc_init(Object *obj)
{
    AspeedUDCState *s = ASPEED_UDC(obj);

    object_property_add_link(obj, "usbgadget", TYPE_ASPEED_UDC_GADGET,
                             (Object **)&s->usbgadget,
                             object_property_allow_set_link, 0);
}

static void aspeed_udc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "ASPEED USB Device Controller";
    dc->realize = aspeed_udc_realize;
    rc->phases.hold = aspeed_udc_reset_hold;
    device_class_set_props(dc, aspeed_udc_properties);
}

/*
 * USB device: gadget device presented on a host controller's bus
 *
 * These callbacks run in the context of the host controller. They translate
 * host transactions into the controller interrupts/state the guest gadget
 * driver expects, then defer (USB_RET_ASYNC) until the driver responds
 * through the MMIO register interface above.
 */

static void aspeed_udc_gadget_handle_reset(USBDevice *udev)
{
    AspeedUDCState *s = ASPEED_UDC_GADGET(udev)->udc;

    s->ep0_packet = NULL;
    s->ep0_offset = 0;
    /* The EHCI host is High-Speed; advertise it to the guest gadget driver */
    s->regs[R_UDC_STS] = R_UDC_STS_HIGHSPEED_MASK;
    trace_aspeed_udc_reset(s->regs[R_UDC_IER]);
    aspeed_udc_raise_isr(s, R_UDC_ISR_BUS_RESET_MASK);
}

static void aspeed_udc_gadget_handle_control(USBDevice *udev, USBPacket *p,
                                          int request, int value, int index,
                                          int length, uint8_t *data)
{
    AspeedUDCState *s = ASPEED_UDC_GADGET(udev)->udc;
    uint8_t req = request & 0xff;
    uint8_t type = request >> 8;

    /*
     * Reconstruct the 8-byte SETUP packet into the SETUP data buffer where
     * the guest gadget driver reads it from.
     */
    s->regs[R_UDC_SETUP0] = type | (req << 8) | ((value & 0xffff) << 16);
    s->regs[R_UDC_SETUP1] = (index & 0xffff) | ((length & 0xffff) << 16);

    /* A new SETUP clears the EP0 STALL condition */
    s->regs[R_UDC_EP0_CTRL] &= ~R_UDC_EP0_CTRL_STALL_MASK;

    s->ep0_packet = p;
    s->ep0_data = data;
    s->ep0_setup_len = length;
    s->ep0_offset = 0;
    s->ep0_dir_in = (type & USB_DIR_IN);

    trace_aspeed_udc_ep0_setup(type, req, value, index, length,
                               s->ep0_dir_in, udev->addr);

    /*
     * SET_ADDRESS is delivered while the device still answers at the default
     * address 0 and carries the new address in wValue. The host controller
     * keeps this transfer's queue bound to address 0 until it completes, so
     * apply the new address synchronously as the transfer completes.
     * Completing it asynchronously (USB_RET_ASYNC) would change udev->addr
     * while the queue is still bound to 0; the host controller sees the
     * mismatch, tears the queue down and enumeration breaks. The guest gadget
     * driver is still notified so its state machine advances.
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
    /* Programmable endpoint (bulk) transfers are added in a later patch. */
    p->status = USB_RET_STALL;
}

static void aspeed_udc_gadget_cancel_packet(USBDevice *udev, USBPacket *p)
{
    AspeedUDCState *s = ASPEED_UDC_GADGET(udev)->udc;

    if (s->ep0_packet == p) {
        s->ep0_packet = NULL;
    }
}

static void aspeed_udc_gadget_realize(USBDevice *udev, Error **errp)
{
    AspeedUDCGadget *dev = ASPEED_UDC_GADGET(udev);

    if (!dev->udc) {
        error_setg(errp, TYPE_ASPEED_UDC_GADGET ": 'udc' link is not set");
        return;
    }
    /* Bind this gadget to its controller through the link property */
    object_property_set_link(OBJECT(dev->udc), "usbgadget", OBJECT(dev),
                             &error_abort);

    udev->auto_attach = 0;
    /* The ASPEED UDC is USB 2.0, so it only runs at High-Speed for now */
    udev->speed = USB_SPEED_HIGH;
    udev->speedmask = USB_SPEED_MASK_HIGH;
}

static void aspeed_udc_gadget_unrealize(USBDevice *udev)
{
    AspeedUDCGadget *dev = ASPEED_UDC_GADGET(udev);

    if (dev->udc && dev->udc->usbgadget == dev) {
        object_property_set_link(OBJECT(dev->udc), "usbgadget", NULL,
                                 &error_abort);
    }
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
    uc->unrealize      = aspeed_udc_gadget_unrealize;
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
        .instance_init = aspeed_udc_init,
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
