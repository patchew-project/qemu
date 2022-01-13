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
 *   USB Print Interface Class IPP Protocol Specification, revision 1.0
 */

#ifndef HW_USB_PRINTER_H
#define HW_USB_PRINTER_H

/* 4.2 Class-Specific Requests */
#define USBPRINTER_GET_DEVICE_ID   0
#define USBPRINTER_GET_PORT_STATUS 1
#define USBPRINTER_SOFT_RESET      2

typedef enum {
    USB_PRINTER_DEVICE_ID_DEFAULT,
    USB_PRINTER_DEVICE_ID_MAX
} USBPrinterDeviceIDType;

typedef const char *USBPrinterDeviceIDStrings[USB_PRINTER_DEVICE_ID_MAX];

/* 4.2.2 GET_PORT_STATUS (bRequest = 1) */
#define PAPER_EMPTY     (1 << 5)
#define PAPER_NOT_EMPTY (0 << 5)
#define SELECTED        (1 << 4)
#define NOT_SELECTED    (0 << 4)
#define NO_ERROR        (1 << 3)
#define ERROR           (0 << 3)

/*
 * 4.2.3 SOFT_RESET (bRequest = 2)
 *
 * "Note: Version 1.0 of the specification incorrectly stated that the
 *  bmReqestType for SOFT_RESET was 00100011B. Version 1.1 Host software
 *  implementers should be prepared for USB printers that expect this
 *  request code, and version 1.1 device implementers should be prepared
 *  for host software that issues this request code."
 */
#define ClassInterfaceOutRequestCompat1_0 \
        ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER) << 8)

/* 5.3 Interface Descriptors */
#define EP_NUMS_1                0x01
#define EP_NUMS_2                0x02
#define EP_NUM_BULK_OUT          0x01
#define EP_NUM_BULK_IN           0x02
#define SC_PRINTERS              0x01
#define PC_PROTOCOL_UNIDIR       0x01
#define PC_PROTOCOL_BIDIR        0x02
#define PC_PROTOCOL_BIDIR_1284_4 0x03
#define PC_PROTOCOL_IPP_USB      0x04
#define PC_VENDOR_SPECIFIC       0xff

/* 4.3 Device Info Descriptor: A Class Specific Descriptor */
#define DEV_INFO_DESC_CHECK_LEN(bLength) \
        QEMU_BUILD_BUG_ON((bLength) < 10)

#define DEV_INFO_DESC_CHECK_NUM_DESCS(bNumDescriptors) \
        QEMU_BUILD_BUG_ON((bNumDescriptors) < 1)

#define DEV_INFO_DESC_CHECK_OPT_CT(bCapabilitiesType) \
        QEMU_BUILD_BUG_ON((bCapabilitiesType) < 0x20 || \
                          (bCapabilitiesType) > 0xff)

#define IPP_USB_CT_BASIC    0x00

#define IPP_USB_CAP_BASIC_PRINT                 (1 << 0)
#define IPP_USB_CAP_BASIC_SCAN                  (1 << 1)
#define IPP_USB_CAP_BASIC_FAX                   (1 << 2)
#define IPP_USB_CAP_BASIC_OTHER                 (1 << 3)
#define IPP_USB_CAP_BASIC_ANY_HTTP_1_1_OVER_USB (1 << 4)

#define IPP_USB_CAP_BASIC_AUTH_NONE              (0x00 << 5)
#define IPP_USB_CAP_BASIC_AUTH_USERNAME_PASSWORD (0x01 << 5)
#define IPP_USB_CAP_BASIC_AUTH_RESERVED          (0x02 << 5)
#define IPP_USB_CAP_BASIC_AUTH_NEGOTIATE         (0x03 << 5)

/* TODO: IPP string table in IPP server implementation */

#endif /* HW_USB_PRINTER_H */
