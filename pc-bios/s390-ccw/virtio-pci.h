/*
 * Definitions for virtio-pci
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VIRTIO_PCI_H
#define VIRTIO_PCI_H

/* Common configuration */
#define VIRTIO_PCI_CAP_COMMON_CFG          1
/* Notifications */
#define VIRTIO_PCI_CAP_NOTIFY_CFG          2
/* ISR access */
#define VIRTIO_PCI_CAP_ISR_CFG             3
/* Device specific configuration */
#define VIRTIO_PCI_CAP_DEVICE_CFG          4
/* PCI configuration access */
#define VIRTIO_PCI_CAP_PCI_CFG             5
/* Additional shared memory capability */
#define VIRTIO_PCI_CAP_SHARED_MEMORY_CFG   8
/* PCI vendor data configuration */
#define VIRTIO_PCI_CAP_VENDOR_CFG          9

/* Offsets within capability header */
#define VIRTIO_PCI_CAP_VNDR        0
#define VIRTIO_PCI_CAP_NEXT        1
#define VIRTIO_PCI_CAP_LEN         2
#define VIRTIO_PCI_CAP_CFG_TYPE    3
#define VIRTIO_PCI_CAP_BAR         4
#define VIRTIO_PCI_CAP_OFFSET      8
#define VIRTIO_PCI_CAP_LENGTH      12

#define VIRTIO_PCI_NOTIFY_CAP_MULT 16 /* VIRTIO_PCI_CAP_NOTIFY_CFG only */

/* Common Area Offsets for virtio-pci queue */
#define VPCI_C_OFFSET_DFSELECT      0
#define VPCI_C_OFFSET_DF            4
#define VPCI_C_OFFSET_GFSELECT      8
#define VPCI_C_OFFSET_GF            12
#define VPCI_C_COMMON_NUMQ          18
#define VPCI_C_OFFSET_STATUS        20
#define VPCI_C_OFFSET_Q_SELECT      22
#define VPCI_C_OFFSET_Q_SIZE        24
#define VPCI_C_OFFSET_Q_ENABLE      28
#define VPCI_C_OFFSET_Q_NOFF        30
#define VPCI_C_OFFSET_Q_DESCLO      32
#define VPCI_C_OFFSET_Q_DESCHI      36
#define VPCI_C_OFFSET_Q_AVAILLO     40
#define VPCI_C_OFFSET_Q_AVAILHI     44
#define VPCI_C_OFFSET_Q_USEDLO      48
#define VPCI_C_OFFSET_Q_USEDHI      52

#define VPCI_S_RESET            0
#define VPCI_S_ACKNOWLEDGE      1
#define VPCI_S_DRIVER           2
#define VPCI_S_DRIVER_OK        4
#define VPCI_S_FEATURES_OK      8

#define VIRTIO_F_VERSION_1      (1 << (32 - 32)) /* Feature bit 32 */

#define VIRT_Q_SIZE 16

long virtio_pci_notify(uint32_t fhandle, int vq_id);
int virtio_pci_setup(VDev *vdev);
int virtio_pci_setup_device(void);
int virtio_pci_reset(VDev *vdev);
void virtio_pci_id2type(VDev *vdev, uint16_t device_id);

#endif
