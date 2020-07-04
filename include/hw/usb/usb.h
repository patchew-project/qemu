/*
 * QEMU USB API
 *
 * Copyright (c) 2005 Fabrice Bellard
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
#ifndef QEMU_HW_USB_H
#define QEMU_HW_USB_H

typedef struct USBDevice USBDevice;

#define TYPE_USB_DEVICE "usb-device"
#define USB_DEVICE(obj) \
     OBJECT_CHECK(USBDevice, (obj), TYPE_USB_DEVICE)

typedef struct USBBus USBBus;

#define TYPE_USB_BUS "usb-bus"
#define USB_BUS(obj) OBJECT_CHECK(USBBus, (obj), TYPE_USB_BUS)

USBBus *usb_bus_find(int busnr);
USBDevice *usb_new(const char *name);
bool usb_realize_and_unref(USBDevice *dev, USBBus *bus, Error **errp);
USBDevice *usb_create_simple(USBBus *bus, const char *name);
USBDevice *usbdevice_create(const char *cmdline);

/**
 * usb_get_port_path:
 * @dev: the USB device
 *
 * The returned data must be released with g_free()
 * when no longer required.
 *
 * Returns: a dynamically allocated pathname.
 */
char *usb_get_port_path(USBDevice *dev);

void hmp_info_usbhost(Monitor *mon, const QDict *qdict);
bool usb_host_dev_is_scsi_storage(USBDevice *usbdev);

#endif
