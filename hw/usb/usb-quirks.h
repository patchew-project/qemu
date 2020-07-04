/*
 * USB quirk handling
 *
 * Copyright (c) 2012 Red Hat, Inc.
 *
 * Red Hat Authors:
 * Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef HW_USB_QUIRKS_H
#define HW_USB_QUIRKS_H

/* In bulk endpoints are streaming data sources (iow behave like isoc eps) */
#define USB_QUIRK_BUFFER_BULK_IN        0x01
/* Bulk pkts in FTDI format, need special handling when combining packets */
#define USB_QUIRK_IS_FTDI               0x02

int usb_get_quirks(uint16_t vendor_id, uint16_t product_id,
                   uint8_t interface_class, uint8_t interface_subclass,
                   uint8_t interface_protocol);

#endif
