/*
 * QEMU USB HCD types
 *
 * Copyright (c) 2020  Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_USB_HCD_TYPES_H
#define HW_USB_HCD_TYPES_H

/* OHCI */
#define TYPE_SYSBUS_OHCI            "sysbus-ohci"
#define TYPE_PCI_OHCI               "pci-ohci"

/* EHCI */
#define TYPE_SYS_BUS_EHCI           "sysbus-ehci-usb"
#define TYPE_PCI_EHCI               "pci-ehci-usb"
#define TYPE_PLATFORM_EHCI          "platform-ehci-usb"
#define TYPE_EXYNOS4210_EHCI        "exynos4210-ehci-usb"
#define TYPE_AW_H3_EHCI             "aw-h3-ehci-usb"
#define TYPE_TEGRA2_EHCI            "tegra2-ehci-usb"
#define TYPE_PPC4xx_EHCI            "ppc4xx-ehci-usb"
#define TYPE_FUSBH200_EHCI          "fusbh200-ehci-usb"
#define TYPE_CHIPIDEA               "usb-chipidea"

/* UHCI */
#define TYPE_PIIX3_USB_UHCI         "piix3-usb-uhci"
#define TYPE_PIIX4_USB_UHCI         "piix4-usb-uhci"
#define TYPE_VT82C686B_USB_UHCI     "vt82c686b-usb-uhci"
#define TYPE_ICH9_USB_UHCI(n)       "ich9-usb-uhci" #n

#endif
