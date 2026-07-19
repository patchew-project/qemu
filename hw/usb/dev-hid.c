/*
 * QEMU USB HID devices
 *
 * Copyright (c) 2005 Fabrice Bellard
 * Copyright (c) 2007 OpenMoko, Inc.  (andrew@openedhand.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/input.h"
#include "hw/usb/usb.h"
#include "migration/vmstate.h"
#include "desc.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/input/hid.h"
#include "hw/usb/hid.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"

struct USBHIDState {
    USBDevice dev;
    USBEndpoint *intr;
    HIDState hid;
    uint32_t usb_version;
    char *display;
    uint32_t head;
};

#define TYPE_USB_HID "usb-hid"
OBJECT_DECLARE_SIMPLE_TYPE(USBHIDState, USB_HID)

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT_MOUSE,
    STR_PRODUCT_TABLET,
    STR_PRODUCT_KEYBOARD,
    STR_SERIAL_COMPAT,
    STR_CONFIG_MOUSE,
    STR_CONFIG_TABLET,
    STR_CONFIG_KEYBOARD,
    STR_SERIAL_MOUSE,
    STR_SERIAL_TABLET,
    STR_SERIAL_KEYBOARD,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]     = "QEMU",
    [STR_PRODUCT_MOUSE]    = "QEMU USB Mouse",
    [STR_PRODUCT_TABLET]   = "QEMU USB Tablet",
    [STR_PRODUCT_KEYBOARD] = "QEMU USB Keyboard",
    [STR_SERIAL_COMPAT]    = "42",
    [STR_CONFIG_MOUSE]     = "HID Mouse",
    [STR_CONFIG_TABLET]    = "HID Tablet",
    [STR_CONFIG_KEYBOARD]  = "HID Keyboard",
    [STR_SERIAL_MOUSE]     = "89126",
    [STR_SERIAL_TABLET]    = "28754",
    [STR_SERIAL_KEYBOARD]  = "68284",
};

static const USBDescIface desc_iface_mouse = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 1,
    .bInterfaceClass               = USB_CLASS_HID,
    .bInterfaceSubClass            = 0x01, /* boot */
    .bInterfaceProtocol            = 0x02,
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* HID descriptor */
            .data = (uint8_t[]) {
                0x09,          /*  u8  bLength */
                USB_DT_HID,    /*  u8  bDescriptorType */
                0x01, 0x00,    /*  u16 HID_class */
                0x00,          /*  u8  country_code */
                0x01,          /*  u8  num_descriptors */
                USB_DT_REPORT, /*  u8  type: Report */
                52, 0,         /*  u16 len */
            },
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 4,
            .bInterval             = 0x0a,
        },
    },
};

static const USBDescIface desc_iface_mouse2 = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 1,
    .bInterfaceClass               = USB_CLASS_HID,
    .bInterfaceSubClass            = 0x01, /* boot */
    .bInterfaceProtocol            = 0x02,
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* HID descriptor */
            .data = (uint8_t[]) {
                0x09,          /*  u8  bLength */
                USB_DT_HID,    /*  u8  bDescriptorType */
                0x01, 0x00,    /*  u16 HID_class */
                0x00,          /*  u8  country_code */
                0x01,          /*  u8  num_descriptors */
                USB_DT_REPORT, /*  u8  type: Report */
                52, 0,         /*  u16 len */
            },
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 4,
            .bInterval             = 7, /* 2 ^ (8-1) * 125 usecs = 8 ms */
        },
    },
};

static const USBDescIface desc_iface_tablet = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 1,
    .bInterfaceClass               = USB_CLASS_HID,
    .bInterfaceProtocol            = 0x00,
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* HID descriptor */
            .data = (uint8_t[]) {
                0x09,          /*  u8  bLength */
                USB_DT_HID,    /*  u8  bDescriptorType */
                0x01, 0x00,    /*  u16 HID_class */
                0x00,          /*  u8  country_code */
                0x01,          /*  u8  num_descriptors */
                USB_DT_REPORT, /*  u8  type: Report */
                74, 0,         /*  u16 len */
            },
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 8,
            .bInterval             = 0x0a,
        },
    },
};

static const USBDescIface desc_iface_tablet2 = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 1,
    .bInterfaceClass               = USB_CLASS_HID,
    .bInterfaceProtocol            = 0x00,
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* HID descriptor */
            .data = (uint8_t[]) {
                0x09,          /*  u8  bLength */
                USB_DT_HID,    /*  u8  bDescriptorType */
                0x01, 0x00,    /*  u16 HID_class */
                0x00,          /*  u8  country_code */
                0x01,          /*  u8  num_descriptors */
                USB_DT_REPORT, /*  u8  type: Report */
                74, 0,         /*  u16 len */
            },
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 8,
            .bInterval             = 4, /* 2 ^ (4-1) * 125 usecs = 1 ms */
        },
    },
};

static const USBDescIface desc_iface_keyboard = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 1,
    .bInterfaceClass               = USB_CLASS_HID,
    .bInterfaceSubClass            = 0x01, /* boot */
    .bInterfaceProtocol            = 0x01, /* keyboard */
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* HID descriptor */
            .data = (uint8_t[]) {
                0x09,          /*  u8  bLength */
                USB_DT_HID,    /*  u8  bDescriptorType */
                0x11, 0x01,    /*  u16 HID_class */
                0x00,          /*  u8  country_code */
                0x01,          /*  u8  num_descriptors */
                USB_DT_REPORT, /*  u8  type: Report */
                0x3f, 0,       /*  u16 len */
            },
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 8,
            .bInterval             = 0x0a,
        },
    },
};

static const USBDescIface desc_iface_keyboard2 = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 1,
    .bInterfaceClass               = USB_CLASS_HID,
    .bInterfaceSubClass            = 0x01, /* boot */
    .bInterfaceProtocol            = 0x01, /* keyboard */
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* HID descriptor */
            .data = (uint8_t[]) {
                0x09,          /*  u8  bLength */
                USB_DT_HID,    /*  u8  bDescriptorType */
                0x11, 0x01,    /*  u16 HID_class */
                0x00,          /*  u8  country_code */
                0x01,          /*  u8  num_descriptors */
                USB_DT_REPORT, /*  u8  type: Report */
                0x3f, 0,       /*  u16 len */
            },
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 8,
            .bInterval             = 7, /* 2 ^ (8-1) * 125 usecs = 8 ms */
        },
    },
};

static const USBDescDevice desc_device_mouse = {
    .bcdUSB                        = 0x0100,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_MOUSE,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_mouse,
        },
    },
};

static const USBDescDevice desc_device_mouse2 = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_MOUSE,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_mouse2,
        },
    },
};

static const USBDescDevice desc_device_tablet = {
    .bcdUSB                        = 0x0100,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_TABLET,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_tablet,
        },
    },
};

static const USBDescDevice desc_device_tablet2 = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_TABLET,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_tablet2,
        },
    },
};

static const USBDescDevice desc_device_keyboard = {
    .bcdUSB                        = 0x0100,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_KEYBOARD,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_keyboard,
        },
    },
};

static const USBDescDevice desc_device_keyboard2 = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_KEYBOARD,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_keyboard2,
        },
    },
};

static const USBDescMSOS desc_msos_suspend = {
    .SelectiveSuspendEnabled = true,
};

static const USBDesc desc_mouse = {
    .id = {
        .idVendor          = 0x0627,
        .idProduct         = 0x0001,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_MOUSE,
        .iSerialNumber     = STR_SERIAL_MOUSE,
    },
    .full = &desc_device_mouse,
    .str  = desc_strings,
    .msos = &desc_msos_suspend,
};

static const USBDesc desc_mouse2 = {
    .id = {
        .idVendor          = 0x0627,
        .idProduct         = 0x0001,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_MOUSE,
        .iSerialNumber     = STR_SERIAL_MOUSE,
    },
    .full = &desc_device_mouse,
    .high = &desc_device_mouse2,
    .str  = desc_strings,
    .msos = &desc_msos_suspend,
};

static const USBDesc desc_tablet = {
    .id = {
        .idVendor          = 0x0627,
        .idProduct         = 0x0001,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_TABLET,
        .iSerialNumber     = STR_SERIAL_TABLET,
    },
    .full = &desc_device_tablet,
    .str  = desc_strings,
    .msos = &desc_msos_suspend,
};

static const USBDesc desc_tablet2 = {
    .id = {
        .idVendor          = 0x0627,
        .idProduct         = 0x0001,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_TABLET,
        .iSerialNumber     = STR_SERIAL_TABLET,
    },
    .full = &desc_device_tablet,
    .high = &desc_device_tablet2,
    .str  = desc_strings,
    .msos = &desc_msos_suspend,
};

static const USBDesc desc_keyboard = {
    .id = {
        .idVendor          = 0x0627,
        .idProduct         = 0x0001,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_KEYBOARD,
        .iSerialNumber     = STR_SERIAL_KEYBOARD,
    },
    .full = &desc_device_keyboard,
    .str  = desc_strings,
    .msos = &desc_msos_suspend,
};

static const USBDesc desc_keyboard2 = {
    .id = {
        .idVendor          = 0x0627,
        .idProduct         = 0x0001,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_KEYBOARD,
        .iSerialNumber     = STR_SERIAL_KEYBOARD,
    },
    .full = &desc_device_keyboard,
    .high = &desc_device_keyboard2,
    .str  = desc_strings,
    .msos = &desc_msos_suspend,
};

static const uint8_t qemu_mouse_hid_report_descriptor[] = {
    0x05, 0x01,		/* Usage Page (Generic Desktop) */
    0x09, 0x02,		/* Usage (Mouse) */
    0xa1, 0x01,		/* Collection (Application) */
    0x09, 0x01,		/*   Usage (Pointer) */
    0xa1, 0x00,		/*   Collection (Physical) */
    0x05, 0x09,		/*     Usage Page (Button) */
    0x19, 0x01,		/*     Usage Minimum (1) */
    0x29, 0x05,		/*     Usage Maximum (5) */
    0x15, 0x00,		/*     Logical Minimum (0) */
    0x25, 0x01,		/*     Logical Maximum (1) */
    0x95, 0x05,		/*     Report Count (5) */
    0x75, 0x01,		/*     Report Size (1) */
    0x81, 0x02,		/*     Input (Data, Variable, Absolute) */
    0x95, 0x01,		/*     Report Count (1) */
    0x75, 0x03,		/*     Report Size (3) */
    0x81, 0x01,		/*     Input (Constant) */
    0x05, 0x01,		/*     Usage Page (Generic Desktop) */
    0x09, 0x30,		/*     Usage (X) */
    0x09, 0x31,		/*     Usage (Y) */
    0x09, 0x38,		/*     Usage (Wheel) */
    0x15, 0x81,		/*     Logical Minimum (-0x7f) */
    0x25, 0x7f,		/*     Logical Maximum (0x7f) */
    0x75, 0x08,		/*     Report Size (8) */
    0x95, 0x03,		/*     Report Count (3) */
    0x81, 0x06,		/*     Input (Data, Variable, Relative) */
    0xc0,		/*   End Collection */
    0xc0,		/* End Collection */
};

static const uint8_t qemu_tablet_hid_report_descriptor[] = {
    0x05, 0x01,		/* Usage Page (Generic Desktop) */
    0x09, 0x02,		/* Usage (Mouse) */
    0xa1, 0x01,		/* Collection (Application) */
    0x09, 0x01,		/*   Usage (Pointer) */
    0xa1, 0x00,		/*   Collection (Physical) */
    0x05, 0x09,		/*     Usage Page (Button) */
    0x19, 0x01,		/*     Usage Minimum (1) */
    0x29, 0x05,		/*     Usage Maximum (5) */
    0x15, 0x00,		/*     Logical Minimum (0) */
    0x25, 0x01,		/*     Logical Maximum (1) */
    0x95, 0x05,		/*     Report Count (5) */
    0x75, 0x01,		/*     Report Size (1) */
    0x81, 0x02,		/*     Input (Data, Variable, Absolute) */
    0x95, 0x01,		/*     Report Count (1) */
    0x75, 0x03,		/*     Report Size (3) */
    0x81, 0x01,		/*     Input (Constant) */
    0x05, 0x01,		/*     Usage Page (Generic Desktop) */
    0x09, 0x30,		/*     Usage (X) */
    0x09, 0x31,		/*     Usage (Y) */
    0x15, 0x00,		/*     Logical Minimum (0) */
    0x26, 0xff, 0x7f,	/*     Logical Maximum (0x7fff) */
    0x35, 0x00,		/*     Physical Minimum (0) */
    0x46, 0xff, 0x7f,	/*     Physical Maximum (0x7fff) */
    0x75, 0x10,		/*     Report Size (16) */
    0x95, 0x02,		/*     Report Count (2) */
    0x81, 0x02,		/*     Input (Data, Variable, Absolute) */
    0x05, 0x01,		/*     Usage Page (Generic Desktop) */
    0x09, 0x38,		/*     Usage (Wheel) */
    0x15, 0x81,		/*     Logical Minimum (-0x7f) */
    0x25, 0x7f,		/*     Logical Maximum (0x7f) */
    0x35, 0x00,		/*     Physical Minimum (same as logical) */
    0x45, 0x00,		/*     Physical Maximum (same as logical) */
    0x75, 0x08,		/*     Report Size (8) */
    0x95, 0x01,		/*     Report Count (1) */
    0x81, 0x06,		/*     Input (Data, Variable, Relative) */
    0xc0,		/*   End Collection */
    0xc0,		/* End Collection */
};

static const uint8_t qemu_keyboard_hid_report_descriptor[] = {
    0x05, 0x01,		/* Usage Page (Generic Desktop) */
    0x09, 0x06,		/* Usage (Keyboard) */
    0xa1, 0x01,		/* Collection (Application) */
    0x75, 0x01,		/*   Report Size (1) */
    0x95, 0x08,		/*   Report Count (8) */
    0x05, 0x07,		/*   Usage Page (Key Codes) */
    0x19, 0xe0,		/*   Usage Minimum (224) */
    0x29, 0xe7,		/*   Usage Maximum (231) */
    0x15, 0x00,		/*   Logical Minimum (0) */
    0x25, 0x01,		/*   Logical Maximum (1) */
    0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
    0x95, 0x01,		/*   Report Count (1) */
    0x75, 0x08,		/*   Report Size (8) */
    0x81, 0x01,		/*   Input (Constant) */
    0x95, 0x05,		/*   Report Count (5) */
    0x75, 0x01,		/*   Report Size (1) */
    0x05, 0x08,		/*   Usage Page (LEDs) */
    0x19, 0x01,		/*   Usage Minimum (1) */
    0x29, 0x05,		/*   Usage Maximum (5) */
    0x91, 0x02,		/*   Output (Data, Variable, Absolute) */
    0x95, 0x01,		/*   Report Count (1) */
    0x75, 0x03,		/*   Report Size (3) */
    0x91, 0x01,		/*   Output (Constant) */
    0x95, 0x06,		/*   Report Count (6) */
    0x75, 0x08,		/*   Report Size (8) */
    0x15, 0x00,		/*   Logical Minimum (0) */
    0x25, 0xff,		/*   Logical Maximum (255) */
    0x05, 0x07,		/*   Usage Page (Key Codes) */
    0x19, 0x00,		/*   Usage Minimum (0) */
    0x29, 0xff,		/*   Usage Maximum (255) */
    0x81, 0x00,		/*   Input (Data, Array) */
    0xc0,		/* End Collection */
};

static void usb_hid_changed(HIDState *hs)
{
    USBHIDState *us = container_of(hs, USBHIDState, hid);

    usb_wakeup(us->intr, 0);
}

static void usb_hid_handle_reset(USBDevice *dev)
{
    USBHIDState *us = USB_HID(dev);

    hid_reset(&us->hid);
}

static void usb_hid_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBHIDState *us = USB_HID(dev);
    HIDState *hs = &us->hid;
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
        /* hid specific requests */
    case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
        switch (value >> 8) {
        case 0x22:
            if (hs->kind == HID_MOUSE) {
                memcpy(data, qemu_mouse_hid_report_descriptor,
                       sizeof(qemu_mouse_hid_report_descriptor));
                p->actual_length = sizeof(qemu_mouse_hid_report_descriptor);
            } else if (hs->kind == HID_TABLET) {
                memcpy(data, qemu_tablet_hid_report_descriptor,
                       sizeof(qemu_tablet_hid_report_descriptor));
                p->actual_length = sizeof(qemu_tablet_hid_report_descriptor);
            } else if (hs->kind == HID_KEYBOARD) {
                memcpy(data, qemu_keyboard_hid_report_descriptor,
                       sizeof(qemu_keyboard_hid_report_descriptor));
                p->actual_length = sizeof(qemu_keyboard_hid_report_descriptor);
            }
            break;
        default:
            goto fail;
        }
        break;
    case HID_GET_REPORT:
        if (hs->kind == HID_MOUSE || hs->kind == HID_TABLET) {
            p->actual_length = hid_pointer_poll(hs, data, length);
        } else if (hs->kind == HID_KEYBOARD) {
            p->actual_length = hid_keyboard_poll(hs, data, length);
        }
        break;
    case HID_SET_REPORT:
        if (hs->kind == HID_KEYBOARD) {
            p->actual_length = hid_keyboard_write(hs, data, length);
        } else {
            goto fail;
        }
        break;
    case HID_GET_PROTOCOL:
        if (hs->kind != HID_KEYBOARD && hs->kind != HID_MOUSE) {
            goto fail;
        }
        data[0] = hs->protocol;
        p->actual_length = 1;
        break;
    case HID_SET_PROTOCOL:
        if (hs->kind != HID_KEYBOARD && hs->kind != HID_MOUSE) {
            goto fail;
        }
        hs->protocol = value;
        break;
    case HID_GET_IDLE:
        data[0] = hs->idle;
        p->actual_length = 1;
        break;
    case HID_SET_IDLE:
        hs->idle = (uint8_t) (value >> 8);
        hid_set_next_idle(hs);
        if (hs->kind == HID_MOUSE || hs->kind == HID_TABLET) {
            hid_pointer_activate(hs);
        }
        break;
    default:
    fail:
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_hid_handle_data(USBDevice *dev, USBPacket *p)
{
    USBHIDState *us = USB_HID(dev);
    HIDState *hs = &us->hid;
    g_autofree uint8_t *buf = g_malloc(p->iov.size);
    int len = 0;

    switch (p->pid) {
    case USB_TOKEN_IN:
        if (p->ep->nr == 1) {
            if (hs->kind == HID_MOUSE || hs->kind == HID_TABLET) {
                hid_pointer_activate(hs);
            }
            if (!hid_has_events(hs)) {
                p->status = USB_RET_NAK;
                return;
            }
            hid_set_next_idle(hs);
            if (hs->kind == HID_MOUSE || hs->kind == HID_TABLET) {
                len = hid_pointer_poll(hs, buf, p->iov.size);
            } else if (hs->kind == HID_KEYBOARD) {
                len = hid_keyboard_poll(hs, buf, p->iov.size);
            }
            usb_packet_copy(p, buf, len);
        } else {
            goto fail;
        }
        break;
    case USB_TOKEN_OUT:
    default:
    fail:
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_hid_unrealize(USBDevice *dev)
{
    USBHIDState *us = USB_HID(dev);

    hid_free(&us->hid);
}

static void usb_hid_initfn(USBDevice *dev, int kind,
                           const USBDesc *usb1, const USBDesc *usb2,
                           Error **errp)
{
    USBHIDState *us = USB_HID(dev);
    switch (us->usb_version) {
    case 1:
        dev->usb_desc = usb1;
        break;
    case 2:
        dev->usb_desc = usb2;
        break;
    default:
        dev->usb_desc = NULL;
    }
    if (!dev->usb_desc) {
        error_setg(errp, "Invalid usb version %d for usb hid device",
                   us->usb_version);
        return;
    }

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    us->intr = usb_ep_get(dev, USB_TOKEN_IN, 1);
    hid_init(&us->hid, kind, usb_hid_changed);
    if (us->display && us->hid.s) {
        qemu_input_handler_bind(us->hid.s, us->display, us->head, NULL);
    }
}

static void usb_tablet_realize(USBDevice *dev, Error **errp)
{

    usb_hid_initfn(dev, HID_TABLET, &desc_tablet, &desc_tablet2, errp);
}

static void usb_mouse_realize(USBDevice *dev, Error **errp)
{
    usb_hid_initfn(dev, HID_MOUSE, &desc_mouse, &desc_mouse2, errp);
}

static void usb_keyboard_realize(USBDevice *dev, Error **errp)
{
    usb_hid_initfn(dev, HID_KEYBOARD, &desc_keyboard, &desc_keyboard2, errp);
}

static int usb_ptr_post_load(void *opaque, int version_id)
{
    USBHIDState *s = opaque;

    if (s->dev.remote_wakeup) {
        hid_pointer_activate(&s->hid);
    }
    return 0;
}

static const VMStateDescription vmstate_usb_ptr = {
    .name = "usb-ptr",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = usb_ptr_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, USBHIDState),
        VMSTATE_HID_POINTER_DEVICE(hid, USBHIDState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_usb_kbd = {
    .name = "usb-kbd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, USBHIDState),
        VMSTATE_HID_KEYBOARD_DEVICE(hid, USBHIDState),
        VMSTATE_END_OF_LIST()
    }
};

static void usb_hid_class_initfn(ObjectClass *klass, const void *data)
{
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->handle_reset   = usb_hid_handle_reset;
    uc->handle_control = usb_hid_handle_control;
    uc->handle_data    = usb_hid_handle_data;
    uc->unrealize      = usb_hid_unrealize;
    uc->handle_attach  = usb_desc_attach;
}

static const TypeInfo usb_hid_type_info = {
    .name = TYPE_USB_HID,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBHIDState),
    .abstract = true,
    .class_init = usb_hid_class_initfn,
};

static const Property usb_tablet_properties[] = {
        DEFINE_PROP_UINT32("usb_version", USBHIDState, usb_version, 2),
        DEFINE_PROP_STRING("display", USBHIDState, display),
        DEFINE_PROP_UINT32("head", USBHIDState, head, 0),
};

static void usb_tablet_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_tablet_realize;
    uc->product_desc   = "QEMU USB Tablet";
    dc->vmsd = &vmstate_usb_ptr;
    device_class_set_props(dc, usb_tablet_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo usb_tablet_info = {
    .name          = "usb-tablet",
    .parent        = TYPE_USB_HID,
    .class_init    = usb_tablet_class_initfn,
};

static const Property usb_mouse_properties[] = {
        DEFINE_PROP_UINT32("usb_version", USBHIDState, usb_version, 2),
};

static void usb_mouse_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_mouse_realize;
    uc->product_desc   = "QEMU USB Mouse";
    dc->vmsd = &vmstate_usb_ptr;
    device_class_set_props(dc, usb_mouse_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo usb_mouse_info = {
    .name          = "usb-mouse",
    .parent        = TYPE_USB_HID,
    .class_init    = usb_mouse_class_initfn,
};

static const Property usb_keyboard_properties[] = {
        DEFINE_PROP_UINT32("usb_version", USBHIDState, usb_version, 2),
        DEFINE_PROP_STRING("display", USBHIDState, display),
};

static void usb_keyboard_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_keyboard_realize;
    uc->product_desc   = "QEMU USB Keyboard";
    dc->vmsd = &vmstate_usb_kbd;
    device_class_set_props(dc, usb_keyboard_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo usb_keyboard_info = {
    .name          = "usb-kbd",
    .parent        = TYPE_USB_HID,
    .class_init    = usb_keyboard_class_initfn,
};


/*
 * apple-magic-keyboard
 * --------------------
 *
 * USB-mode emulator for the Apple Magic Keyboard with Numeric Keypad
 * (idVendor 0x05ac, idProduct 0x026c). Carries Apple's vendor-defined
 * HID protocol on UsagePage 0xff00 alongside a standard HID Boot
 * Keyboard interface; the descriptors and HID report descriptors are
 * byte-identical to a real Magic Keyboard captured in USB-cable mode.
 *
 * Two HID interfaces:
 *
 *   Interface 0 — Apple-vendor HID (UsagePage 0xff00). Carries three
 *     vendor input report IDs that the macOS Apple HID driver chain
 *     (AppleUSBTopCaseHIDDriver → AppleDeviceManagementHIDEventService
 *     → AppleUserHIDEventDriver) probes against:
 *
 *       0xe0  4 bytes  vendor keyboard event
 *       0x9a  1 byte   modifier-state change / short signal
 *       0x90  2 bytes  power flags + battery percent
 *
 *     A 1 Hz 0x90 heartbeat keeps the macOS userspace HID watchdog
 *     considering the device alive. Real device firmware emits the
 *     same heartbeat regardless of activity.
 *
 *   Interface 1 — standard HID Boot Keyboard (UsagePage 0x07,
 *     bInterfaceSubClass 1, bInterfaceProtocol 1). Emits the standard
 *     10-byte boot-keyboard input report (modifier byte + 7 keycode
 *     slots) on EP2 IN. Wired to QEMU's input subsystem via
 *     qemu_input_handler_register; any RFB / SPICE / SDL / HMP
 *     `sendkey` source drives it.
 *
 * The vendor-encoded keystroke format on Interface 0 (where reports
 * 0xe0 / 0x9a would carry typing data) is not generated here; macOS's
 * boot-keyboard claim path drives input via Interface 1, which is
 * sufficient for typing and modifier handling.
 */

/*
 * USB endpoint numbers within the apple-magic-keyboard device.
 * EP1 IN = vendor-defined HID reports (Interface 0).
 * EP2 IN = boot keyboard reports (Interface 1).
 */
#define AMK_EP_VENDOR_IN  1
#define AMK_EP_BOOT_IN    2

/*
 * Apple Magic Keyboard boot-keyboard input report:
 *   byte 0 — Report ID (0x01)
 *   byte 1 — modifier byte (LCtrl=0x01 LShift=0x02 LAlt=0x04 LGUI=0x08
 *            RCtrl=0x10 RShift=0x20 RAlt=0x40 RGUI=0x80)
 *   byte 2 — reserved (0)
 *   bytes 3..9 — up to 7 simultaneous HID Usage codes, 0 in unused slots
 */
#define AMK_BOOT_REPORT_ID    0x01
/*
 * Boot keyboard input report (10 bytes total — byte-for-byte the
 * Apple Magic Keyboard format captured 2026-05-08):
 *   byte 0: report ID (0x01)
 *   byte 1: modifier byte (8 bits, HID Usages 0xE0..0xE7)
 *   byte 2: reserved (always 0)
 *   bytes 3..8: 6 keycode slots (HID Usage codes, 0 in unused slots)
 *   byte 9: bit0 = Eject (Consumer Page), bits1..7 = vendor 0xff00
 *           Usage 0x03 (always 0 for our emulator).
 *
 * NOTE: real Apple boot keyboard convention is 6 keycode slots, not
 * the 7 some older docs describe. Apple's `AppleHIDKeyboardEventDriverV2`
 * match dictionary depends on this exact layout — using 7 slots makes
 * the driver decline to bind, which leaves the boot interface's
 * IOHIDInterface unclaimed and 60s busy-times out on installed macOS.
 */
#define AMK_BOOT_REPORT_LEN   10
#define AMK_BOOT_NUM_KEYS     6

typedef struct USBAppleMagicKbdState {
    USBDevice                  dev;
    USBEndpoint               *boot_intr;        /* EP2 IN */
    QemuInputHandlerState     *input_handler;
    /* Pressed-key state in HID Usage codes (UsagePage 0x07). */
    uint8_t                    boot_modifiers;   /* live modifier byte */
    uint8_t                    boot_keys[AMK_BOOT_NUM_KEYS]; /* live slots */
    bool                       boot_changed;     /* report needs sending */
} USBAppleMagicKbdState;

#define TYPE_USB_APPLE_MAGIC_KBD "apple-magic-keyboard"
OBJECT_DECLARE_SIMPLE_TYPE(USBAppleMagicKbdState, USB_APPLE_MAGIC_KBD)

enum {
    STR_AMK_MFR = 1,
    STR_AMK_PRODUCT,
    STR_AMK_SERIAL,
    STR_AMK_INTERFACE,
    STR_AMK_INTERFACE_BOOT,
};

static const USBDescStrings desc_strings_amk = {
    [STR_AMK_MFR]            = "Apple Inc.",
    [STR_AMK_PRODUCT]        = "Magic Keyboard with Numeric Keypad",
    [STR_AMK_SERIAL]         = "F0T924300PCJKNCAS",
    [STR_AMK_INTERFACE]      = "Device Management",
    [STR_AMK_INTERFACE_BOOT] = "Keyboard / Boot",
};

/*
 * HID Report Descriptor — byte-identical to a real Magic Keyboard
 * with Numeric Keypad over USB. Vendor-defined UsagePage 0xff00.
 *
 * Three input report IDs (real device's "InputReportElements"):
 *   0xe0:  4 bytes — Apple-encoded keyboard event payload (keys / mods)
 *   0x9a:  1 byte  — short-form vendor signal
 *   0x90:  3 bytes — power/battery status (charging, AC, percent)
 *
 * Decoded layout (see project_apple_magic_hid_emulator_2026_05_07.md):
 *   Application 1: UsagePage 0xff00 / Usage 0x0b — keystrokes + signals
 *   Application 2: UsagePage 0xff00 / Usage 0x14 — power/battery
 */
static const uint8_t apple_magic_kbd_hid_report_descriptor[] = {
    /* Application 1: keystroke / vendor signal */
    0x06, 0x00, 0xff,       /* USAGE_PAGE (Vendor 0xff00) */
    0x09, 0x0b,             /* USAGE (0x0b) */
    0xa1, 0x01,             /* COLLECTION (Application) */
    0x06, 0x00, 0xff,       /*   USAGE_PAGE (Vendor 0xff00) */
    0x09, 0x0b,             /*   USAGE (0x0b) */
    0x15, 0x00,             /*   LOGICAL_MINIMUM (0) */
    0x26, 0xff, 0x00,       /*   LOGICAL_MAXIMUM (255) */
    0x75, 0x08,             /*   REPORT_SIZE (8) */
    0x96, 0x04, 0x00,       /*   REPORT_COUNT (4)        Report 0xe0 = 4 bytes */
    0x85, 0xe0,             /*   REPORT_ID (0xe0) */
    0x81, 0x22,             /*   INPUT (Data,Var,Abs,NoPref) */
    0x09, 0x0b,             /*   USAGE (0x0b) */
    0x96, 0x01, 0x00,       /*   REPORT_COUNT (1)        Report 0x9a = 1 byte */
    0x85, 0x9a,             /*   REPORT_ID (0x9a) */
    0x81, 0x22,             /*   INPUT (Data,Var,Abs,NoPref) */
    0xc0,                   /* END_COLLECTION */

    /* Application 2: power / battery */
    0x06, 0x00, 0xff,       /* USAGE_PAGE (Vendor 0xff00) */
    0x09, 0x14,             /* USAGE (0x14) */
    0xa1, 0x01,             /* COLLECTION (Application) */
    0x85, 0x90,             /*   REPORT_ID (0x90) */
    0x05, 0x84,             /*   USAGE_PAGE (Power Device) */
    0x75, 0x01,             /*   REPORT_SIZE (1) */
    0x95, 0x03,             /*   REPORT_COUNT (3)        3 status bits */
    0x15, 0x00,             /*   LOGICAL_MINIMUM (0) */
    0x25, 0x01,             /*   LOGICAL_MAXIMUM (1) */
    0x09, 0x61,             /*   USAGE (AC mains) */
    0x05, 0x85,             /*   USAGE_PAGE (Battery System) */
    0x09, 0x44,             /*   USAGE (Charging) */
    0x09, 0x46,             /*   USAGE (NeedReplacement) */
    0x81, 0x02,             /*   INPUT (Data,Var,Abs) */
    0x95, 0x05,             /*   REPORT_COUNT (5)        5-bit padding */
    0x81, 0x01,             /*   INPUT (Const,Array,Abs) */
    0x75, 0x08,             /*   REPORT_SIZE (8) */
    0x95, 0x01,             /*   REPORT_COUNT (1)        battery percent byte */
    0x15, 0x00,             /*   LOGICAL_MINIMUM (0) */
    0x26, 0xff, 0x00,       /*   LOGICAL_MAXIMUM (255) */
    0x09, 0x65,             /*   USAGE (AbsoluteStateOfCharge) */
    0x81, 0x02,             /*   INPUT (Data,Var,Abs) */
    0xc0,                   /* END_COLLECTION */
};

/*
 * Interface 1 — standard USB HID Boot Keyboard.
 *
 * UsagePage 0x07 (Keyboard/Keypad), one Application collection with
 * Report ID 0x01:
 *   8 bits modifier (UsageMin 0xE0 / UsageMax 0xE7)
 *   8 bits reserved (Const)
 *   5 bits LED output (UsagePage 0x08, UsageMin 1, UsageMax 5)
 *   3 bits LED padding (Const)
 *   7 keycode slots (8 bits each, UsageMin 0, UsageMax 0xFF)
 *
 * Total input report = ReportID byte + 9 data bytes = 10 bytes.
 * Total output report (LEDs) = ReportID byte + 1 data byte = 2 bytes.
 * Real Magic Keyboards expose a similar boot-keyboard interface
 * alongside the vendor (UsagePage 0xff00) interface.
 */
/*
 * Boot keyboard report descriptor — byte-for-byte the real Apple
 * Magic Keyboard with Numeric Keypad report descriptor on Interface 1
 * (IOReg "Keyboard / Boot@1"), captured 2026-05-08 from a real
 * keyboard plugged into a Mac running macOS 15.x. Source:
 * paravirt-re/library/apple-magic-hid/captures/usb-magic/
 * 04-ioreg-usbhostdevice.txt.
 *
 * The descriptor declares FOUR application collections:
 *   App 1, Report 0x01 — boot keyboard (mod+reserved+6 keys),
 *                         Consumer eject (1 bit), Vendor 0xff00 (7 bits)
 *   App 2, Report 0x52 — consumer multimedia keys (mute, vol, etc.)
 *   App 3, Report 0x09 — generic desktop control (system on/off etc.)
 *   App 4, Report 0x3f — vendor 0xff00 64-byte feature report
 *
 * Our emulator only generates Report 0x01 (typing). The other three
 * collections must be DECLARED in the descriptor for AppleUSBTopCase
 * HIDDriver and AppleHIDKeyboardEventDriverV2 to match-dictionary
 * accept the device — we don't have to actually emit reports for
 * them.
 *
 * Using a generic single-collection boot keyboard descriptor (which
 * we did pre-2026-05-08) makes AppleHIDKeyboardEventDriverV2 decline
 * to bind, which leaves the boot iface IOHIDInterface unclaimed and
 * 60s busy-times out on installed macOS.
 */
static const uint8_t apple_magic_kbd_boot_hid_report_descriptor[] = {
    /* App 1: boot keyboard + Consumer eject + Vendor 0xff (Report 0x01) */
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05,
    0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
    0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xff,
    0x00, 0x05, 0x07, 0x19, 0x00, 0x29, 0xff, 0x81,
    0x00, 0x05, 0x0c, 0x75, 0x01, 0x95, 0x01, 0x09,
    0xb8, 0x15, 0x00, 0x25, 0x01, 0x81, 0x02, 0x05,
    0xff, 0x09, 0x03, 0x75, 0x07, 0x95, 0x01, 0x81,
    0x02, 0xc0,
    /* App 2: Consumer multimedia keys (Report 0x52) +
     *        system control feature report (Report 0x09)
     * One Application Collection containing two Report IDs. */
    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01,
    0x85, 0x52, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x01, 0x09, 0xcd, 0x81, 0x02, 0x09, 0xb3,
    0x81, 0x02, 0x09, 0xb4, 0x81, 0x02, 0x09, 0xb5,
    0x81, 0x02, 0x09, 0xb6, 0x81, 0x02, 0x81, 0x01,
    0x81, 0x01, 0x81, 0x01, 0x85, 0x09, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x08, 0x95, 0x01, 0x06, 0x01,
    0xff, 0x09, 0x0b, 0xb1, 0x02, 0x75, 0x08, 0x95,
    0x02, 0xb1, 0x01, 0xc0,
    /* App 3: Vendor 0xff00 64-byte feature blob (Report 0x3f) */
    0x06, 0x00, 0xff, 0x09,
    0x06, 0xa1, 0x01, 0x06, 0x00, 0xff, 0x09, 0x06,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95,
    0x40, 0x85, 0x3f, 0x81, 0x22, 0xc0,
};

static const USBDescIface desc_iface_apple_magic_kbd[] = {
    {
        /* Interface 0: Apple-vendor HID. */
        .bInterfaceNumber              = 0,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_HID,
        .bInterfaceSubClass            = 0x00,    /* NOT boot — vendor */
        .bInterfaceProtocol            = 0x00,    /* NOT keyboard — vendor */
        .iInterface                    = STR_AMK_INTERFACE,
        .ndesc                         = 1,
        .descs = (USBDescOther[]) {
            {
                /* HID descriptor */
                .data = (uint8_t[]) {
                    0x09,                            /* bLength */
                    USB_DT_HID,                      /* bDescriptorType */
                    0x11, 0x01,                      /* bcdHID 1.11 */
                    0x00,                            /* bCountryCode */
                    0x01,                            /* bNumDescriptors */
                    USB_DT_REPORT,                   /* bDescriptorType: Report */
                    sizeof(apple_magic_kbd_hid_report_descriptor) & 0xff,
                    sizeof(apple_magic_kbd_hid_report_descriptor) >> 8,
                },
            },
        },
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | AMK_EP_VENDOR_IN,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 8,
                .bInterval             = 7, /* 2 ^ (8-1) * 125 us = 8 ms */
            },
        },
    },
    {
        /* Interface 1: standard HID boot keyboard. */
        .bInterfaceNumber              = 1,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_HID,
        .bInterfaceSubClass            = 0x01,    /* boot */
        .bInterfaceProtocol            = 0x01,    /* keyboard */
        .iInterface                    = STR_AMK_INTERFACE_BOOT,
        .ndesc                         = 1,
        .descs = (USBDescOther[]) {
            {
                /* HID descriptor */
                .data = (uint8_t[]) {
                    0x09,                            /* bLength */
                    USB_DT_HID,                      /* bDescriptorType */
                    0x11, 0x01,                      /* bcdHID 1.11 */
                    0x00,                            /* bCountryCode */
                    0x01,                            /* bNumDescriptors */
                    USB_DT_REPORT,                   /* bDescriptorType: Report */
                    sizeof(apple_magic_kbd_boot_hid_report_descriptor) & 0xff,
                    sizeof(apple_magic_kbd_boot_hid_report_descriptor) >> 8,
                },
            },
        },
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | AMK_EP_BOOT_IN,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                /*
                 * wMaxPacketSize=64 (full-speed interrupt max) — required
                 * because the report descriptor declares Report 0x3f as
                 * a 64-byte vendor input. With a smaller wMaxPacketSize
                 * the kernel computes MaxInputReportSize > wMaxPacketSize
                 * and IOHIDInterface.start() blocks waiting for a packet
                 * size that can't be delivered. We never actually emit
                 * 64-byte frames — only the 10-byte boot kbd Report 0x01.
                 */
                .wMaxPacketSize        = 64,
                .bInterval             = 8, /* 2 ^ (8-1) * 125 us = 8 ms */
            },
        },
    },
};

static const USBDescDevice desc_device_apple_magic_kbd = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_AMK_PRODUCT,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 250, /* 500 mA — matches real */
            .nif                   = ARRAY_SIZE(desc_iface_apple_magic_kbd),
            .ifs                   = desc_iface_apple_magic_kbd,
        },
    },
};

static const USBDesc desc_apple_magic_kbd = {
    .id = {
        .idVendor          = 0x05ac,                   /* Apple Inc. */
        .idProduct         = 0x026c,                   /* Magic Keyboard with NumPad */
        .bcdDevice         = 0x0870,
        .iManufacturer     = STR_AMK_MFR,
        .iProduct          = STR_AMK_PRODUCT,
        .iSerialNumber     = STR_AMK_SERIAL,
    },
    /*
     * Real Magic Keyboard runs at USB full-speed (12 Mb/s) despite
     * declaring bcdUSB=0x0200. Advertise the same config under both
     * .full and .high so QEMU's USB stack can pick whichever speed
     * the host controller (qemu-xhci) negotiates.
     */
    .full = &desc_device_apple_magic_kbd,
    .high = &desc_device_apple_magic_kbd,
    .str  = desc_strings_amk,
};

/*
 * QKeyCode → USB HID Usage Code (UsagePage 0x07).
 *
 * Self-contained map so the apple-magic-keyboard implementation does
 * not have to share state with hw/input/hid.c (whose hid_usage_keys[]
 * is static and indexed by atset1 scancode). We only need the HID
 * Usage value here — modifier vs slot keys are distinguished by
 * value range (0xE0..0xE7 = modifier).
 *
 * Entries left at 0 are unmapped and ignored.
 */
static const uint8_t apple_magic_kbd_qcode_to_hid_usage[Q_KEY_CODE__MAX] = {
    /* Letters */
    [Q_KEY_CODE_A] = 0x04, [Q_KEY_CODE_B] = 0x05, [Q_KEY_CODE_C] = 0x06,
    [Q_KEY_CODE_D] = 0x07, [Q_KEY_CODE_E] = 0x08, [Q_KEY_CODE_F] = 0x09,
    [Q_KEY_CODE_G] = 0x0a, [Q_KEY_CODE_H] = 0x0b, [Q_KEY_CODE_I] = 0x0c,
    [Q_KEY_CODE_J] = 0x0d, [Q_KEY_CODE_K] = 0x0e, [Q_KEY_CODE_L] = 0x0f,
    [Q_KEY_CODE_M] = 0x10, [Q_KEY_CODE_N] = 0x11, [Q_KEY_CODE_O] = 0x12,
    [Q_KEY_CODE_P] = 0x13, [Q_KEY_CODE_Q] = 0x14, [Q_KEY_CODE_R] = 0x15,
    [Q_KEY_CODE_S] = 0x16, [Q_KEY_CODE_T] = 0x17, [Q_KEY_CODE_U] = 0x18,
    [Q_KEY_CODE_V] = 0x19, [Q_KEY_CODE_W] = 0x1a, [Q_KEY_CODE_X] = 0x1b,
    [Q_KEY_CODE_Y] = 0x1c, [Q_KEY_CODE_Z] = 0x1d,
    /* Top-row digits 1..0 */
    [Q_KEY_CODE_1] = 0x1e, [Q_KEY_CODE_2] = 0x1f, [Q_KEY_CODE_3] = 0x20,
    [Q_KEY_CODE_4] = 0x21, [Q_KEY_CODE_5] = 0x22, [Q_KEY_CODE_6] = 0x23,
    [Q_KEY_CODE_7] = 0x24, [Q_KEY_CODE_8] = 0x25, [Q_KEY_CODE_9] = 0x26,
    [Q_KEY_CODE_0] = 0x27,
    /* Editing / whitespace */
    [Q_KEY_CODE_RET]            = 0x28,
    [Q_KEY_CODE_ESC]            = 0x29,
    [Q_KEY_CODE_BACKSPACE]      = 0x2a,
    [Q_KEY_CODE_TAB]            = 0x2b,
    [Q_KEY_CODE_SPC]            = 0x2c,
    [Q_KEY_CODE_MINUS]          = 0x2d,
    [Q_KEY_CODE_EQUAL]          = 0x2e,
    [Q_KEY_CODE_BRACKET_LEFT]   = 0x2f,
    [Q_KEY_CODE_BRACKET_RIGHT]  = 0x30,
    [Q_KEY_CODE_BACKSLASH]      = 0x31,
    [Q_KEY_CODE_SEMICOLON]      = 0x33,
    [Q_KEY_CODE_APOSTROPHE]     = 0x34,
    [Q_KEY_CODE_GRAVE_ACCENT]   = 0x35,
    [Q_KEY_CODE_COMMA]          = 0x36,
    [Q_KEY_CODE_DOT]            = 0x37,
    [Q_KEY_CODE_SLASH]          = 0x38,
    [Q_KEY_CODE_CAPS_LOCK]      = 0x39,
    /* Function row F1..F12 */
    [Q_KEY_CODE_F1]  = 0x3a, [Q_KEY_CODE_F2]  = 0x3b, [Q_KEY_CODE_F3]  = 0x3c,
    [Q_KEY_CODE_F4]  = 0x3d, [Q_KEY_CODE_F5]  = 0x3e, [Q_KEY_CODE_F6]  = 0x3f,
    [Q_KEY_CODE_F7]  = 0x40, [Q_KEY_CODE_F8]  = 0x41, [Q_KEY_CODE_F9]  = 0x42,
    [Q_KEY_CODE_F10] = 0x43, [Q_KEY_CODE_F11] = 0x44, [Q_KEY_CODE_F12] = 0x45,
    /* Print / lock / pause */
    [Q_KEY_CODE_PRINT]       = 0x46,
    [Q_KEY_CODE_SCROLL_LOCK] = 0x47,
    [Q_KEY_CODE_PAUSE]       = 0x48,
    /* Editing block */
    [Q_KEY_CODE_INSERT]      = 0x49,
    [Q_KEY_CODE_HOME]        = 0x4a,
    [Q_KEY_CODE_PGUP]        = 0x4b,
    [Q_KEY_CODE_DELETE]      = 0x4c,
    [Q_KEY_CODE_END]         = 0x4d,
    [Q_KEY_CODE_PGDN]        = 0x4e,
    [Q_KEY_CODE_RIGHT]       = 0x4f,
    [Q_KEY_CODE_LEFT]        = 0x50,
    [Q_KEY_CODE_DOWN]        = 0x51,
    [Q_KEY_CODE_UP]          = 0x52,
    /* Keypad */
    [Q_KEY_CODE_NUM_LOCK]    = 0x53,
    [Q_KEY_CODE_KP_DIVIDE]   = 0x54,
    [Q_KEY_CODE_KP_MULTIPLY] = 0x55,
    [Q_KEY_CODE_ASTERISK]    = 0x55, /* duplicate name in qcode table */
    [Q_KEY_CODE_KP_SUBTRACT] = 0x56,
    [Q_KEY_CODE_KP_ADD]      = 0x57,
    [Q_KEY_CODE_KP_ENTER]    = 0x58,
    [Q_KEY_CODE_KP_1]        = 0x59,
    [Q_KEY_CODE_KP_2]        = 0x5a,
    [Q_KEY_CODE_KP_3]        = 0x5b,
    [Q_KEY_CODE_KP_4]        = 0x5c,
    [Q_KEY_CODE_KP_5]        = 0x5d,
    [Q_KEY_CODE_KP_6]        = 0x5e,
    [Q_KEY_CODE_KP_7]        = 0x5f,
    [Q_KEY_CODE_KP_8]        = 0x60,
    [Q_KEY_CODE_KP_9]        = 0x61,
    [Q_KEY_CODE_KP_0]        = 0x62,
    [Q_KEY_CODE_KP_DECIMAL]  = 0x63,
    [Q_KEY_CODE_LESS]        = 0x64, /* non-US backslash / ISO key */
    [Q_KEY_CODE_KP_EQUALS]   = 0x67,
    /* F13..F24 */
    [Q_KEY_CODE_F13] = 0x68, [Q_KEY_CODE_F14] = 0x69, [Q_KEY_CODE_F15] = 0x6a,
    [Q_KEY_CODE_F16] = 0x6b, [Q_KEY_CODE_F17] = 0x6c, [Q_KEY_CODE_F18] = 0x6d,
    [Q_KEY_CODE_F19] = 0x6e, [Q_KEY_CODE_F20] = 0x6f, [Q_KEY_CODE_F21] = 0x70,
    [Q_KEY_CODE_F22] = 0x71, [Q_KEY_CODE_F23] = 0x72, [Q_KEY_CODE_F24] = 0x73,
    /* Misc named keys */
    [Q_KEY_CODE_HELP]    = 0x75,
    [Q_KEY_CODE_MENU]    = 0x76,
    [Q_KEY_CODE_STOP]    = 0x78,
    [Q_KEY_CODE_AGAIN]   = 0x79,
    [Q_KEY_CODE_UNDO]    = 0x7a,
    [Q_KEY_CODE_CUT]     = 0x7b,
    [Q_KEY_CODE_COPY]    = 0x7c,
    [Q_KEY_CODE_PASTE]   = 0x7d,
    [Q_KEY_CODE_FIND]    = 0x7e,
    [Q_KEY_CODE_AUDIOMUTE]   = 0x7f,
    [Q_KEY_CODE_VOLUMEUP]    = 0x80,
    [Q_KEY_CODE_VOLUMEDOWN]  = 0x81,
    [Q_KEY_CODE_KP_COMMA]    = 0x85,
    [Q_KEY_CODE_RO]              = 0x87, /* Intl1 (Japanese RO) */
    [Q_KEY_CODE_KATAKANAHIRAGANA]= 0x88, /* Intl2 */
    [Q_KEY_CODE_YEN]             = 0x89, /* Intl3 */
    [Q_KEY_CODE_HENKAN]          = 0x8a, /* Intl4 */
    [Q_KEY_CODE_MUHENKAN]        = 0x8b, /* Intl5 */
    [Q_KEY_CODE_HIRAGANA]        = 0x91, /* LANG4 (close enough) */
    [Q_KEY_CODE_LANG1]           = 0x90,
    [Q_KEY_CODE_LANG2]           = 0x91,
    /* Modifiers — HID Usages 0xE0..0xE7 (also written into modifier byte). */
    [Q_KEY_CODE_CTRL]    = 0xe0,
    [Q_KEY_CODE_SHIFT]   = 0xe1,
    [Q_KEY_CODE_ALT]     = 0xe2,
    [Q_KEY_CODE_META_L]  = 0xe3,
    [Q_KEY_CODE_CTRL_R]  = 0xe4,
    [Q_KEY_CODE_SHIFT_R] = 0xe5,
    [Q_KEY_CODE_ALT_R]   = 0xe6,
    [Q_KEY_CODE_META_R]  = 0xe7,
};

/*
 * Pack the live boot-keyboard state into a 10-byte report payload —
 * matches real Apple Magic Keyboard Report 0x01 layout (boot keyboard
 * + Consumer Eject + Vendor 0xff Usage 0x03):
 *   buf[0]    = report ID (0x01)
 *   buf[1]    = modifier byte (HID Usages 0xE0..0xE7)
 *   buf[2]    = reserved (0)
 *   buf[3..8] = 6 keycode slots (HID Usage codes)
 *   buf[9]    = bit0 Consumer Eject + bits1..7 Vendor 0xff Usage 0x03
 *              (always 0 — emulator does not generate eject or vendor)
 */
static void apple_magic_kbd_pack_boot_report(USBAppleMagicKbdState *s,
                                             uint8_t buf[AMK_BOOT_REPORT_LEN])
{
    buf[0] = AMK_BOOT_REPORT_ID;
    buf[1] = s->boot_modifiers;
    buf[2] = 0;
    memcpy(&buf[3], s->boot_keys, AMK_BOOT_NUM_KEYS); /* 6 keycodes */
    buf[9] = 0;                                       /* eject + vendor */
}

/* Update s->boot_modifiers / s->boot_keys for one HID Usage, then mark
 * the report dirty. Returns true if state actually changed. */
static bool apple_magic_kbd_apply_usage(USBAppleMagicKbdState *s,
                                        uint8_t usage, bool down)
{
    int i;

    if (usage == 0) {
        return false;
    }

    /* Modifiers — packed bitmap into the modifier byte. */
    if (usage >= 0xe0 && usage <= 0xe7) {
        uint8_t bit = 1u << (usage - 0xe0);
        uint8_t prev = s->boot_modifiers;
        if (down) {
            s->boot_modifiers |= bit;
        } else {
            s->boot_modifiers &= ~bit;
        }
        return s->boot_modifiers != prev;
    }

    /* Slot keys — 7-slot array, no duplicates. */
    if (down) {
        for (i = 0; i < AMK_BOOT_NUM_KEYS; i++) {
            if (s->boot_keys[i] == usage) {
                return false;        /* already pressed */
            }
        }
        for (i = 0; i < AMK_BOOT_NUM_KEYS; i++) {
            if (s->boot_keys[i] == 0) {
                s->boot_keys[i] = usage;
                return true;
            }
        }
        /* Roll-over — slots full. Per HID spec, every slot should be
         * 0x01 (ErrorRollOver). For simplicity we just drop; VNC /
         * single-user input isn't going to produce 8+ chord keys in
         * normal use. */
        return false;
    } else {
        for (i = 0; i < AMK_BOOT_NUM_KEYS; i++) {
            if (s->boot_keys[i] == usage) {
                /* Compact slots so packed array stays contiguous. */
                int j;
                for (j = i; j < AMK_BOOT_NUM_KEYS - 1; j++) {
                    s->boot_keys[j] = s->boot_keys[j + 1];
                }
                s->boot_keys[AMK_BOOT_NUM_KEYS - 1] = 0;
                return true;
            }
        }
        return false;
    }
}

static void apple_magic_kbd_input_event(DeviceState *dev, QemuConsole *src,
                                        InputEvent *evt)
{
    USBAppleMagicKbdState *s = USB_APPLE_MAGIC_KBD(dev);
    InputKeyEvent *key;
    int qcode;
    uint8_t usage;

    if (evt->type != INPUT_EVENT_KIND_KEY) {
        return;
    }

    key = evt->u.key.data;
    qcode = qemu_input_key_value_to_qcode(key->key);
    if (qcode < 0 || qcode >= Q_KEY_CODE__MAX) {
        return;
    }
    usage = apple_magic_kbd_qcode_to_hid_usage[qcode];
    if (usage == 0) {
        return;
    }

    if (apple_magic_kbd_apply_usage(s, usage, key->down)) {
        s->boot_changed = true;
        if (s->boot_intr) {
            usb_wakeup(s->boot_intr, 0);
        }
    }
}

static const QemuInputHandler apple_magic_kbd_input_handler = {
    .name  = "Apple Magic Keyboard (boot)",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = apple_magic_kbd_input_event,
};

static void usb_apple_magic_kbd_realize(USBDevice *dev, Error **errp)
{
    USBAppleMagicKbdState *s = USB_APPLE_MAGIC_KBD(dev);

    /*
     * uc->usb_desc set in class_init handles dev->usb_desc selection.
     * Mirror Wacom's pattern: just set up the serial + descriptors.
     */
    usb_desc_create_serial(dev);
    usb_desc_init(dev);

    s->boot_intr = usb_ep_get(dev, USB_TOKEN_IN, AMK_EP_BOOT_IN);
    s->input_handler = qemu_input_handler_register(DEVICE(s),
                                            &apple_magic_kbd_input_handler);
    qemu_input_handler_activate(s->input_handler);
}

static const VMStateDescription vmstate_apple_magic_kbd = {
    .name = "apple-magic-keyboard",
    .unmigratable = 1,
};

static void usb_apple_magic_kbd_handle_reset(USBDevice *dev)
{
    USBAppleMagicKbdState *s = USB_APPLE_MAGIC_KBD(dev);

    s->boot_modifiers = 0;
    memset(s->boot_keys, 0, sizeof(s->boot_keys));
    s->boot_changed = false;
}

static void usb_apple_magic_kbd_handle_control(USBDevice *dev, USBPacket *p,
                                              int request, int value,
                                              int index, int length,
                                              uint8_t *data)
{
    USBAppleMagicKbdState *s = USB_APPLE_MAGIC_KBD(dev);
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
        if ((value >> 8) == 0x22) {
            /*
             * GET_DESCRIPTOR(REPORT). Pick the right report descriptor
             * based on the interface index in wIndex. Interface 0 =
             * Apple vendor HID; Interface 1 = boot keyboard.
             */
            const uint8_t *rd;
            uint16_t rd_len;
            uint16_t copy;
            if (index == 1) {
                rd     = apple_magic_kbd_boot_hid_report_descriptor;
                rd_len = sizeof(apple_magic_kbd_boot_hid_report_descriptor);
            } else {
                rd     = apple_magic_kbd_hid_report_descriptor;
                rd_len = sizeof(apple_magic_kbd_hid_report_descriptor);
            }
            copy = length < rd_len ? length : rd_len;
            memcpy(data, rd, copy);
            p->actual_length = copy;
            return;
        }
        break;
    case HID_GET_IDLE:
        data[0] = 0;
        p->actual_length = 1;
        return;
    case HID_SET_IDLE:
        return;
    case HID_GET_PROTOCOL:
        data[0] = 1; /* report protocol */
        p->actual_length = 1;
        return;
    case HID_SET_PROTOCOL:
        return;
    case HID_GET_REPORT: {
        /*
         * GET_REPORT — feature/input poll over EP0. Behaviour depends
         * on the interface index in wIndex.
         *
         * Interface 0 (vendor): blanket-ACK with zero-filled payload of
         * the declared report size. Stalling these would send
         * AppleUSBTopCaseHIDDriver into a tight retry loop on
         * match-probe. Match the per-Report-ID sizes declared in the
         * vendor HID Report Descriptor:
         *   0xe0 → 4 bytes  (input only, but driver may probe Feature)
         *   0x9a → 1 byte
         *   0x90 → 2 bytes  (AC/charge bits + battery byte)
         * Default: zeros of the requested 'length' bytes.
         *
         * Interface 1 (boot keyboard): synthesize an input report from
         * current boot state if the host requests Report ID 0x01.
         */
        uint8_t report_id = value & 0xff;
        uint8_t report_type = (value >> 8) & 0xff;
        uint16_t reply_len = 0;

        if (index == 1) {
            uint8_t buf[AMK_BOOT_REPORT_LEN];
            if (report_type == 0x01 /* Input */ &&
                report_id == AMK_BOOT_REPORT_ID) {
                apple_magic_kbd_pack_boot_report(s, buf);
                reply_len = AMK_BOOT_REPORT_LEN;
                if (reply_len > length) {
                    reply_len = length;
                }
                memcpy(data, buf, reply_len);
                p->actual_length = reply_len;
                return;
            }
            /*
             * Unknown report type/ID on the boot iface — STALL.
             * macOS speculatively probes Feature reads for IDs not in the
             * descriptor (e.g. 0x02, 0x03); a real device responds with
             * a STALL there and macOS moves on. Returning zero-fill
             * causes the host's HID parser to treat the response as a
             * valid-but-malformed report and stall the IOHIDInterface
             * during ::start (the (a,4020001) busy timeout).
             */
            break;
        }

        /* Vendor iface: same logic — only declared report IDs answer. */
        switch (report_id) {
        case 0xe0: reply_len = 4; break;
        case 0x9a: reply_len = 1; break;
        case 0x90: reply_len = 2; break;
        default:
            /* Unknown vendor report ID — STALL (real device behaviour). */
            break;
        }
        if (reply_len == 0) {
            break;  /* falls through to STALL */
        }
        if (reply_len > length) {
            reply_len = length;
        }
        memset(data, 0, reply_len);
        p->actual_length = reply_len;
        return;
    }
    case HID_SET_REPORT:
        /*
         * Interface 0 (vendor): silently accept SET_REPORT writes.
         * The vendor multitouch-enable SET_REPORT (0x02, 0xF1, per
         * Linux's magicmouse_enable_multitouch) is acknowledged but
         * not yet acted on; the device stays on the boot face.
         *
         * Interface 1 (boot keyboard): ACK SET_REPORT (LED state).
         * We don't drive any host-visible LEDs yet but must not stall.
         *
         * MUST set actual_length: the USB layer reports back the
         * number of bytes accepted, which the host uses to confirm
         * the write succeeded. Without it the host reads "0 bytes
         * accepted" and retries. Trace 2026-05-08 showed macOS
         * sending the same LED SET_REPORT 5 times back-to-back —
         * exactly that retry pattern.
         */
        p->actual_length = length;
        return;
    }

    p->status = USB_RET_STALL;
}

static void usb_apple_magic_kbd_handle_data(USBDevice *dev, USBPacket *p)
{
    USBAppleMagicKbdState *s = USB_APPLE_MAGIC_KBD(dev);

    if (p->pid != USB_TOKEN_IN) {
        p->status = USB_RET_STALL;
        return;
    }

    switch (p->ep->nr) {
    case AMK_EP_VENDOR_IN:
        /*
         * Vendor IN endpoint carries the 1 Hz 0x90 heartbeat queued
         * from the heartbeat timer. NAK when nothing is pending so
         * the host keeps polling without erroring; the typing pipe
         * is on Interface 1's boot-keyboard endpoint.
         */
        p->status = USB_RET_NAK;
        return;
    case AMK_EP_BOOT_IN: {
        uint8_t buf[AMK_BOOT_REPORT_LEN];
        size_t copy;

        if (!s->boot_changed) {
            p->status = USB_RET_NAK;
            return;
        }
        s->boot_changed = false;
        apple_magic_kbd_pack_boot_report(s, buf);
        copy = p->iov.size < AMK_BOOT_REPORT_LEN
             ? p->iov.size : AMK_BOOT_REPORT_LEN;
        usb_packet_copy(p, buf, copy);
        return;
    }
    default:
        p->status = USB_RET_STALL;
        return;
    }
}

static void usb_apple_magic_kbd_unrealize(USBDevice *dev)
{
    USBAppleMagicKbdState *s = USB_APPLE_MAGIC_KBD(dev);

    if (s->input_handler) {
        qemu_input_handler_unregister(s->input_handler);
        s->input_handler = NULL;
    }
}

static void usb_apple_magic_kbd_class_initfn(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_apple_magic_kbd_realize;
    uc->product_desc   = "Magic Keyboard with Numeric Keypad";
    uc->usb_desc       = &desc_apple_magic_kbd;
    uc->handle_reset   = usb_apple_magic_kbd_handle_reset;
    uc->handle_control = usb_apple_magic_kbd_handle_control;
    uc->handle_data    = usb_apple_magic_kbd_handle_data;
    uc->unrealize      = usb_apple_magic_kbd_unrealize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc           = "Apple Magic Keyboard with Numeric Keypad "
                         "(USB-mode emulator, vendor HID protocol)";
    dc->vmsd           = &vmstate_apple_magic_kbd;
}

static const TypeInfo usb_apple_magic_kbd_info = {
    .name          = TYPE_USB_APPLE_MAGIC_KBD,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBAppleMagicKbdState),
    .class_init    = usb_apple_magic_kbd_class_initfn,
};

static void usb_hid_register_types(void)
{
    type_register_static(&usb_hid_type_info);
    type_register_static(&usb_tablet_info);
    usb_legacy_register("usb-tablet", "tablet", NULL);
    type_register_static(&usb_mouse_info);
    usb_legacy_register("usb-mouse", "mouse", NULL);
    type_register_static(&usb_keyboard_info);
    usb_legacy_register("usb-kbd", "keyboard", NULL);
    type_register_static(&usb_apple_magic_kbd_info);
}

type_init(usb_hid_register_types)
