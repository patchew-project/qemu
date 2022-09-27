/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CPU
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LS7A_H
#define HW_LS7A_H

#include "hw/pci/pci.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci-host/pam.h"
#include "qemu/units.h"
#include "qemu/range.h"
#include "qom/object.h"

/*
 * According to the kernel pch irq start from 64 offset
 * 0 ~ 16 irqs used for non-pci device while 16 ~ 64 irqs
 * used for pci device.
 */
#define PCH_PIC_IRQ_OFFSET       64
#define VIRT_DEVICE_IRQS         16
#define VIRT_PCI_IRQS            48
#define VIRT_UART_IRQ            (PCH_PIC_IRQ_OFFSET + 2)
#define VIRT_RTC_IRQ             (PCH_PIC_IRQ_OFFSET + 3)
#define VIRT_SCI_IRQ             (PCH_PIC_IRQ_OFFSET + 4)

#define VIRT_PLATFORM_BUS_NUM_IRQS      2
#define VIRT_PLATFORM_BUS_IRQ           69
#endif
