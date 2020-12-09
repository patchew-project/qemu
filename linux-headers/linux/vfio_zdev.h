/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * VFIO Region definitions for ZPCI devices
 *
 * Copyright IBM Corp. 2020
 *
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *            Matthew Rosato <mjrosato@linux.ibm.com>
 */

#ifndef _VFIO_ZDEV_H_
#define _VFIO_ZDEV_H_

#include <linux/types.h>
#include <linux/vfio.h>

/**
 * VFIO_DEVICE_INFO_CAP_ZPCI_BASE - Base PCI Function information
 *
 * This capability provides a set of descriptive information about the
 * associated PCI function.
 */
struct vfio_device_info_cap_zpci_base {
	struct vfio_info_cap_header header;
	__u64 start_dma;	/* Start of available DMA addresses */
	__u64 end_dma;		/* End of available DMA addresses */
	__u16 pchid;		/* Physical Channel ID */
	__u16 vfn;		/* Virtual function number */
	__u16 fmb_length;	/* Measurement Block Length (in bytes) */
	__u8 pft;		/* PCI Function Type */
	__u8 gid;		/* PCI function group ID */
};

/**
 * VFIO_DEVICE_INFO_CAP_ZPCI_GROUP - Base PCI Function Group information
 *
 * This capability provides a set of descriptive information about the group of
 * PCI functions that the associated device belongs to.
 */
struct vfio_device_info_cap_zpci_group {
	struct vfio_info_cap_header header;
	__u64 dasm;		/* DMA Address space mask */
	__u64 msi_addr;		/* MSI address */
	__u64 flags;
#define VFIO_DEVICE_INFO_ZPCI_FLAG_REFRESH 1 /* Program-specified TLB refresh */
#define VFIO_DEVICE_INFO_ZPCI_FLAG_RELAXED 2 /* Relaxed Length and Alignment */
	__u16 mui;		/* Measurement Block Update Interval */
	__u16 noi;		/* Maximum number of MSIs */
	__u16 maxstbl;		/* Maximum Store Block Length */
	__u8 version;		/* Supported PCI Version */
};

/**
 * VFIO_DEVICE_INFO_CAP_ZPCI_UTIL - Utility String
 *
 * This capability provides the utility string for the associated device, which
 * is a device identifier string made up of EBCDID characters.  'size' specifies
 * the length of 'util_str'.
 */
struct vfio_device_info_cap_zpci_util {
	struct vfio_info_cap_header header;
	__u32 size;
	__u8 util_str[];
};

/**
 * VFIO_DEVICE_INFO_CAP_ZPCI_PFIP - PCI Function Path
 *
 * This capability provides the PCI function path string, which is an identifier
 * that describes the internal hardware path of the device. 'size' specifies
 * the length of 'pfip'.
 */
struct vfio_device_info_cap_zpci_pfip {
	struct vfio_info_cap_header header;
	__u32 size;
	__u8 pfip[];
};

/**
 * VFIO_REGION_SUBTYPE_IBM_ZPCI_IO - VFIO zPCI PCI Direct I/O Region
 *
 * This region is used to transfer I/O operations from the guest directly
 * to the host zPCI I/O layer.
 *
 * The _hdr area is user-readable and is used to provide setup information.
 * The _req area is user-writable and is used to provide the I/O operation.
 */
struct vfio_zpci_io_hdr {
	__u64 flags;
	__u16 max;		/* Max block operation size allowed */
	__u16 reserved;
	__u32 reserved2;
};

struct vfio_zpci_io_req {
	__u64 flags;
#define VFIO_ZPCI_IO_FLAG_READ (1 << 0) /* Read Operation Specified */
#define VFIO_ZPCI_IO_FLAG_BLOCKW (1 << 1) /* Block Write Operation Specified */
	__u64 gaddr;		/* Address of guest data */
	__u64 offset;		/* Offset into target PCI Address Space */
	__u32 reserved;
	__u16 len;		/* Length of guest operation */
	__u8 pcias;		/* Target PCI Address Space */
	__u8 reserved2;
};

struct vfio_region_zpci_io {
	struct vfio_zpci_io_hdr hdr;
	struct vfio_zpci_io_req req;
};
#endif
