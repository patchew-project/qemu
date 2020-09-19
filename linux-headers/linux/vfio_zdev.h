/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Region definition for ZPCI devices
 *
 * Copyright IBM Corp. 2020
 *
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *            Matthew Rosato <mjrosato@linux.ibm.com>
 */

#ifndef _VFIO_ZDEV_H_
#define _VFIO_ZDEV_H_

#include <linux/types.h>

/**
 * struct vfio_region_zpci_info - ZPCI information
 *
 * This region provides zPCI specific hardware feature information for a
 * device.
 *
 * The ZPCI information structure is presented as a chain of CLP features
 * defined below. argsz provides the size of the entire region, and offset
 * provides the location of the first CLP feature in the chain.
 *
 */
struct vfio_region_zpci_info {
	__u32 argsz;		/* Size of entire payload */
	__u32 offset;		/* Location of first entry */
} __attribute__((packed));

/**
 * struct vfio_region_zpci_info_hdr - ZPCI header information
 *
 * This structure is included at the top of each CLP feature to define what
 * type of CLP feature is presented / the structure version. The next value
 * defines the offset of the next CLP feature, and is an offset from the very
 * beginning of the region (vfio_region_zpci_info).
 *
 * Each CLP feature must have it's own unique 'id'.
 */
struct vfio_region_zpci_info_hdr {
	__u16 id;		/* Identifies the CLP type */
	__u16	version;	/* version of the CLP data */
	__u32 next;		/* Offset of next entry */
} __attribute__((packed));

/**
 * struct vfio_region_zpci_info_qpci - Initial Query PCI information
 *
 * This region provides an initial set of data from the Query PCI Function
 * CLP.
 */
#define VFIO_REGION_ZPCI_INFO_QPCI	1

struct vfio_region_zpci_info_qpci {
	struct vfio_region_zpci_info_hdr hdr;
	__u64 start_dma;	/* Start of available DMA addresses */
	__u64 end_dma;		/* End of available DMA addresses */
	__u16 pchid;		/* Physical Channel ID */
	__u16 vfn;		/* Virtual function number */
	__u16 fmb_length;	/* Measurement Block Length (in bytes) */
	__u8 pft;		/* PCI Function Type */
	__u8 gid;		/* PCI function group ID */
} __attribute__((packed));


/**
 * struct vfio_region_zpci_info_qpcifg - Initial Query PCI Function Group info
 *
 * This region provides an initial set of data from the Query PCI Function
 * Group CLP.
 */
#define VFIO_REGION_ZPCI_INFO_QPCIFG	2

struct vfio_region_zpci_info_qpcifg {
	struct vfio_region_zpci_info_hdr hdr;
	__u64 dasm;		/* DMA Address space mask */
	__u64 msi_addr;		/* MSI address */
	__u64 flags;
#define VFIO_PCI_ZDEV_FLAGS_REFRESH 1 /* Use program-specified TLB refresh */
	__u16 mui;		/* Measurement Block Update Interval */
	__u16 noi;		/* Maximum number of MSIs */
	__u16 maxstbl;		/* Maximum Store Block Length */
	__u8 version;		/* Supported PCI Version */
} __attribute__((packed));

/**
 * struct vfio_region_zpci_info_util - Utility String
 *
 * This region provides the utility string for the associated device, which is
 * a device identifier string.
 */
#define VFIO_REGION_ZPCI_INFO_UTIL	3

struct vfio_region_zpci_info_util {
	struct vfio_region_zpci_info_hdr hdr;
	__u32 size;
	__u8 util_str[];
} __attribute__((packed));

/**
 * struct vfio_region_zpci_info_pfip - PCI Function Path
 *
 * This region provides the PCI function path string, which is an identifier
 * that describes the internal hardware path of the device.
 */
#define VFIO_REGION_ZPCI_INFO_PFIP	4

struct vfio_region_zpci_info_pfip {
struct vfio_region_zpci_info_hdr hdr;
	__u32 size;
	__u8 pfip[];
} __attribute__((packed));

#endif
