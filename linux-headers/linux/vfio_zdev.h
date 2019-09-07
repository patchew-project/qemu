/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Region definition for ZPCI devices
 *
 * Copyright IBM Corp. 2019
 *
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 */

#ifndef _VFIO_ZDEV_H_
#define _VFIO_ZDEV_H_

#include <linux/types.h>

/**
 * struct vfio_region_zpci_info - ZPCI information.
 *
 */
struct vfio_region_zpci_info {
	__u64 dasm;
	__u64 start_dma;
	__u64 end_dma;
	__u64 msi_addr;
	__u64 flags;
	__u16 pchid;
	__u16 mui;
	__u16 noi;
	__u16 maxstbl;
	__u8 version;
	__u8 gid;
#define VFIO_PCI_ZDEV_FLAGS_REFRESH 1
	__u8 util_str[];
} __attribute__ ((__packed__));

#endif
