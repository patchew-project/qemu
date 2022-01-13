/*
 * USB Printer Device emulation
 *
 * Copyright (c) 2022 ByteDance, Inc.
 *
 * Author:
 *   Ruien Zhang <zhangruien@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * References:
 *   Universal Serial Bus Device Class Definition for Printing Devices,
 *   version 1.1
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "hw/usb/printer.h"
#include "printer/printer.h"
#include "desc.h"
#include "trace.h"

#define USBPRINTER_VENDOR_NUM     0x46f4 /* CRC16() of "QEMU" */
#define USBPRINTER_PRODUCT_NUM    0xa1f3

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_CONFIG_FULL,
    STR_CONFIG_HIGH,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER] = "QEMU",
    [STR_PRODUCT]      = "QEMU USB Printer",
    [STR_SERIALNUMBER] = "1",
    [STR_CONFIG_FULL]  = "Full speed config (usb 1.1)",
    [STR_CONFIG_HIGH]  = "High speed config (usb 2.0)",
};

/*
 * 5. Standard Descriptors
 *
 * "Printer Class devices support the following standard USB descriptors:
 *  - Device. Each printer has one device descriptor.
 *  - Configuration. Each device has one default configuration descriptor which
 *    supports at least one interface.
 *  - Interface. A printer device has a single data interface with possible
 *    alternates.
 *  - Endpoint. A printer device supports the following endpoints:
 *  - Bulk OUT endpoint. Used for transfer of PDL/PCP data.
 *  - Optional Bulk IN endpoint. Provides status and other return information."
 */
static const USBDescIface desc_iface_full = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = EP_NUMS_2,
    .bInterfaceClass               = USB_CLASS_PRINTER,
    .bInterfaceSubClass            = SC_PRINTERS,
    .bInterfaceProtocol            = PC_PROTOCOL_BIDIR_1284_4,
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_OUT | EP_NUM_BULK_OUT,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },{
            .bEndpointAddress      = USB_DIR_IN | EP_NUM_BULK_IN,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },
    },
};

static const USBDescDevice desc_device_full = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_FULL,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .nif = 1,
            .ifs = &desc_iface_full,
        },
    },
};

static const USBDescIface desc_iface_high = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = EP_NUMS_2,
    .bInterfaceClass               = USB_CLASS_PRINTER,
    .bInterfaceSubClass            = SC_PRINTERS,
    .bInterfaceProtocol            = PC_PROTOCOL_BIDIR_1284_4,
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_OUT | EP_NUM_BULK_OUT,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
        },{
            .bEndpointAddress      = USB_DIR_IN | EP_NUM_BULK_IN,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
        },
    },
};

static const USBDescDevice desc_device_high = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_HIGH,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .nif = 1,
            .ifs = &desc_iface_high,
        },
    },
};

static const USBDesc desc_printer = {
    .id = {
        .idVendor          = USB_CLASS_PRINTER,
        .idProduct         = USBPRINTER_PRODUCT_NUM,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full  = &desc_device_full,
    .high  = &desc_device_high,
    .str   = desc_strings,
};

struct USBPrinterState {
    /* qemu interfaces */
    USBDevice dev;

    /* state */
    QEMUPrinter *printer;

    /* properties */
    char *printerdev;
    char *terminal;
};

#define TYPE_USB_PRINTER "usb-printer"
OBJECT_DECLARE_SIMPLE_TYPE(USBPrinterState, USB_PRINTER)

static void usb_printer_handle_reset(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    trace_usb_printer_handle_reset(bus->busnr, dev->addr);
}

/*
 * 4.2.1 GET_DEVICE_ID (bRequest = 0)
 * "This class-specific request returns a device ID string that is compatible
 *  with IEEE 1284. See IEEE 1284 for syntax and formatting information."
 */
#define USB_PRINTER_DEVICE_ID_QEMU "QEMU Printer"
#define USB_PRINTER_DEVICE_ID_QEMU_LEN \
    strlen(USB_PRINTER_DEVICE_ID_QEMU)
#define USB_PRINTER_DEVICE_ID_QEMU_LEN_IEEE_1284 \
    (2 + USB_PRINTER_DEVICE_ID_QEMU_LEN)

static const USBPrinterDeviceIDStrings usb_printer_device_ids = {
    [USB_PRINTER_DEVICE_ID_DEFAULT] = USB_PRINTER_DEVICE_ID_QEMU,
};

static int usb_printer_get_device_id(USBDevice *dev, int request, int value,
                                 int index, int length, uint8_t *data)
{
    USBBus *bus = usb_bus_from_device(dev);

    *((uint16_t *)data) = cpu_to_be16(USB_PRINTER_DEVICE_ID_QEMU_LEN_IEEE_1284);
    memcpy(data + 2, usb_printer_device_ids[USB_PRINTER_DEVICE_ID_DEFAULT],
        USB_PRINTER_DEVICE_ID_QEMU_LEN);

    trace_usb_printer_get_device_id(bus->busnr, dev->addr);

    return USB_PRINTER_DEVICE_ID_QEMU_LEN_IEEE_1284;
}

/*
 * 4.2.2 GET_PORT_STATUS (bRequest = 1)
 *
 * "Note: Some USB printers may not always be able to determine this
 *  information. In this case, they should return benign status of
 *  “Paper Not Empty,” “Selected,” and “No Error.”"
 */
static int usb_printer_get_port_status(USBDevice *dev, int request, int value,
                                 int index, int length, uint8_t *data)
{
    USBBus *bus = usb_bus_from_device(dev);

    *((uint8_t *)data) = PAPER_NOT_EMPTY | SELECTED | NO_ERROR;
    trace_usb_printer_get_port_status(bus->busnr, dev->addr);
    return 1;
}

/*
 * TODO: 4.2.3 SOFT_RESET (bRequest = 2)
 *
 * "This class-specific request flushes all buffers and resets the Bulk OUT
 *  and Bulk IN pipes to their default states. This request clears all stall
 *  conditions. This reset does NOT change the USB addressing or USB
 *  configuration."
 */
static int usb_printer_handle_soft_reset(USBDevice *dev, int request, int value,
                                 int index, int length, uint8_t *data)
{
    USBBus *bus = usb_bus_from_device(dev);

    trace_usb_printer_handle_soft_reset(bus->busnr, dev->addr);
    return 0;
}

static void usb_printer_handle_control(USBDevice *dev, USBPacket *p,
                                    int request, int value, int index,
                                    int length, uint8_t *data)
{
    USBBus *bus = usb_bus_from_device(dev);
    int ret = 0;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case ClassInterfaceRequest | USBPRINTER_GET_DEVICE_ID:
        ret = usb_printer_get_device_id(dev, request, value, index,
                                        length, data);
        if (ret < 0) {
            goto error;
        }
        break;

    case ClassInterfaceRequest | USBPRINTER_GET_PORT_STATUS:
        ret = usb_printer_get_port_status(dev, request, value, index,
                                          length, data);
        if (ret < 0) {
            goto error;
        }
        break;

    case ClassInterfaceOutRequestCompat1_0 | USBPRINTER_SOFT_RESET:
        /* fall through */
    case ClassInterfaceOutRequest | USBPRINTER_SOFT_RESET:
        ret = usb_printer_handle_soft_reset(dev, request, value, index,
                                            length, data);
        if (ret < 0) {
            goto error;
        }
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: request %x not implemented\n",
                      TYPE_USB_PRINTER, request);
        goto error;
    }

    p->actual_length = ret;
    p->status = USB_RET_SUCCESS;
    return;

error:
    trace_usb_printer_handle_control_error(bus->busnr, dev->addr, request,
        value, index, length);
    p->status = USB_RET_STALL;
}

static void usb_printer_handle_data_out(USBDevice *dev, USBPacket *p)
{
    USBBus *bus = usb_bus_from_device(dev);
    QEMUIOVector *iov = p->combined ? &p->combined->iov : &p->iov;

    p->status = USB_RET_SUCCESS;
    trace_usb_printer_handle_data_out(bus->busnr, dev->addr, iov->size);
}

/*
 * 5.4.2 Bulk IN Endpoint
 *
 * "The Bulk IN endpoint is used to return any data generated by the PDL
 *  or PCP to the host. If the printer supports a PCP, such as IEEE-1284.1
 *  or IEEE-1284.4, this endpoint will return status or other printer-related
 *  information."
 */
static void usb_printer_handle_data_in(USBDevice *dev, USBPacket *p)
{
    USBBus *bus = usb_bus_from_device(dev);
    QEMUIOVector *iov = p->combined ? &p->combined->iov : &p->iov;

    p->status = USB_RET_SUCCESS;
    trace_usb_printer_handle_data_in(bus->busnr, dev->addr, iov->size);
}

static void usb_printer_handle_data(USBDevice *dev, USBPacket *p)
{
    USBBus *bus = usb_bus_from_device(dev);

    switch (p->pid) {
    case USB_TOKEN_OUT:
        switch (p->ep->nr) {
        case EP_NUM_BULK_OUT:
            usb_printer_handle_data_out(dev, p);
            return;

        default:
            goto fail;
        }
        break;

    case USB_TOKEN_IN:
        switch (p->ep->nr) {
        case EP_NUM_BULK_IN:
            usb_printer_handle_data_in(dev, p);
            return;

        default:
            goto fail;
        }
        break;

    default:
    fail:
        p->status = USB_RET_STALL;
        break;
    }

    if (p->status == USB_RET_STALL) {
        fprintf(stderr, "usbprinter: failed data transaction: "
                        "pid 0x%x ep 0x%x len 0x%zx\n",
                        p->pid, p->ep->nr, p->iov.size);
    }

    trace_usb_printer_handle_data(bus->busnr, dev->addr, p->pid, p->ep->nr);
}

static void usb_printer_unrealize(USBDevice *dev)
{
}

static void usb_printer_realize(USBDevice *dev, Error **errp)
{
    USBPrinterState *s = USB_PRINTER(dev);
    if (!s->terminal || strcmp(s->terminal, "printer")) {
        error_setg(errp, "%s: support terminal printer only", TYPE_USB_PRINTER);
        return;
    }

    s->printer = qemu_printer_by_id(s->printerdev);
    if (!s->printer) {
        error_setg(errp, "%s: invalid printerdev %s",
                   TYPE_USB_PRINTER, s->printerdev);
        return;
    }

    dev->usb_desc = &desc_printer;

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->dev.opaque = s;
}

/* TODO: set alternates on IPP-over-USB */
static void usb_printer_set_interface(USBDevice *dev, int iface,
                                    int old, int value)
{
    USBBus *bus = usb_bus_from_device(dev);
    trace_usb_printer_set_interface(bus->busnr, dev->addr, iface, old, value);
}

static Property usb_printer_properties[] = {
    DEFINE_PROP_STRING("printerdev", USBPrinterState, printerdev),
    DEFINE_PROP_STRING("terminal", USBPrinterState, terminal),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_printer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *k = USB_DEVICE_CLASS(klass);

    device_class_set_props(dc, usb_printer_properties);
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    k->product_desc   = "QEMU USB Printer Interface";
    k->realize        = usb_printer_realize;
    k->handle_reset   = usb_printer_handle_reset;
    k->handle_control = usb_printer_handle_control;
    k->handle_data    = usb_printer_handle_data;
    k->unrealize      = usb_printer_unrealize;
    k->set_interface = usb_printer_set_interface;
}

static const TypeInfo usb_printer_info = {
    .name          = TYPE_USB_PRINTER,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBPrinterState),
    .class_init    = usb_printer_class_init,
};

static void usb_printer_register_types(void)
{
    type_register_static(&usb_printer_info);
}

type_init(usb_printer_register_types)
