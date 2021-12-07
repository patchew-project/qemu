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
	__u16 mui;		/* Measurement Block Update Interval */
	__u16 noi;		/* Maximum number of MSIs */
	__u16 maxstbl;		/* Maximum Store Block Length */
	__u8 version;		/* Supported PCI Version */
	/* End of version 1 */
	__u8 dtsm;		/* Supported IOAT Designations */
	/* End of version 2 */
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
 * VFIO_DEVICE_FEATURE_ZPCI_INTERP
 *
 * This feature is used for enabling zPCI instruction interpretation for a
 * device.  No data is provided when setting this feature.  When getting
 * this feature, the following structure is provided which details whether
 * or not interpretation is active and provides the guest with host device
 * information necessary to enable interpretation.
 */
struct vfio_device_zpci_interp {
	__u64 flags;
#define VFIO_DEVICE_ZPCI_FLAG_INTERP 1
	__u32 fh;		/* Host device function handle */
};

/**
 * VFIO_DEVICE_FEATURE_ZPCI_AIF
 *
 * This feature is used for enabling forwarding of adapter interrupts directly
 * from firmware to the guest.  When setting this feature, the flags indicate
 * whether to enable/disable the feature and the structure defined below is
 * used to setup the forwarding structures.  When getting this feature, only
 * the flags are used to indicate the current state.
 */
struct vfio_device_zpci_aif {
	__u64 flags;
#define VFIO_DEVICE_ZPCI_FLAG_AIF_FLOAT 1
#define VFIO_DEVICE_ZPCI_FLAG_AIF_HOST 2
	__u64 ibv;		/* Address of guest interrupt bit vector */
	__u64 sb;		/* Address of guest summary bit */
	__u32 noi;		/* Number of interrupts */
	__u8 isc;		/* Guest interrupt subclass */
	__u8 sbo;		/* Offset of guest summary bit vector */
};

/**
 * VFIO_DEVICE_FEATURE_ZPCI_IOAT
 *
 * This feature is used for enabling guest I/O translation assistance for
 * passthrough zPCI devices using instruction interpretation.  When setting
 * this feature, the iota specifies a KVM guest I/O translation anchor.  When
 * getting this feature, the most recently set anchor (or 0) is returned in
 * iota.
 */
struct vfio_device_zpci_ioat {
	__u64 iota;
};

#endif
