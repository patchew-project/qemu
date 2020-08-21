/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Virtio-iommu definition v0.12
 *
 * Copyright (C) 2019 Arm Ltd.
 */
#ifndef _LINUX_VIRTIO_IOMMU_H
#define _LINUX_VIRTIO_IOMMU_H

#include "standard-headers/linux/types.h"

/* Feature bits */
#define VIRTIO_IOMMU_F_INPUT_RANGE		0
#define VIRTIO_IOMMU_F_DOMAIN_RANGE		1
#define VIRTIO_IOMMU_F_MAP_UNMAP		2
#define VIRTIO_IOMMU_F_BYPASS			3
#define VIRTIO_IOMMU_F_PROBE			4
#define VIRTIO_IOMMU_F_MMIO			5
#define VIRTIO_IOMMU_F_TOPOLOGY			6

struct virtio_iommu_range_64 {
	uint64_t					start;
	uint64_t					end;
};

struct virtio_iommu_range_32 {
	uint32_t					start;
	uint32_t					end;
};

struct virtio_iommu_topo_config {
	/* Number of topology description structures */
	uint16_t					count;
	/*
	 * Offset to the first topology description structure
	 * (virtio_iommu_topo_*) from the start of the virtio_iommu config
	 * space. Aligned on 8 bytes.
	 */
	uint16_t					offset;
};

struct virtio_iommu_config {
	/* Supported page sizes */
	uint64_t					page_size_mask;
	/* Supported IOVA range */
	struct virtio_iommu_range_64		input_range;
	/* Max domain ID size */
	struct virtio_iommu_range_32		domain_range;
	/* Probe buffer size */
	uint32_t					probe_size;
	struct virtio_iommu_topo_config		topo_config;
};

#define VIRTIO_IOMMU_TOPO_PCI_RANGE		0x1
#define VIRTIO_IOMMU_TOPO_MMIO			0x2

struct virtio_iommu_topo_pci_range {
	/* VIRTIO_IOMMU_TOPO_PCI_RANGE */
	uint8_t					type;
	uint8_t					reserved;
	/* Length of this structure */
	uint16_t					length;
	/* First endpoint ID in the range */
	uint32_t					endpoint_start;
	/* PCI domain number */
	uint16_t					segment;
	/* PCI Bus:Device.Function range */
	uint16_t					bdf_start;
	uint16_t					bdf_end;
	uint16_t					padding;
};

struct virtio_iommu_topo_mmio {
	/* VIRTIO_IOMMU_TOPO_MMIO */
	uint8_t					type;
	uint8_t					reserved;
	/* Length of this structure */
	uint16_t					length;
	/* Endpoint ID */
	uint32_t					endpoint;
	/* Address of the first MMIO region */
	uint64_t					address;
};

/* Request types */
#define VIRTIO_IOMMU_T_ATTACH			0x01
#define VIRTIO_IOMMU_T_DETACH			0x02
#define VIRTIO_IOMMU_T_MAP			0x03
#define VIRTIO_IOMMU_T_UNMAP			0x04
#define VIRTIO_IOMMU_T_PROBE			0x05

/* Status types */
#define VIRTIO_IOMMU_S_OK			0x00
#define VIRTIO_IOMMU_S_IOERR			0x01
#define VIRTIO_IOMMU_S_UNSUPP			0x02
#define VIRTIO_IOMMU_S_DEVERR			0x03
#define VIRTIO_IOMMU_S_INVAL			0x04
#define VIRTIO_IOMMU_S_RANGE			0x05
#define VIRTIO_IOMMU_S_NOENT			0x06
#define VIRTIO_IOMMU_S_FAULT			0x07
#define VIRTIO_IOMMU_S_NOMEM			0x08

struct virtio_iommu_req_head {
	uint8_t					type;
	uint8_t					reserved[3];
};

struct virtio_iommu_req_tail {
	uint8_t					status;
	uint8_t					reserved[3];
};

struct virtio_iommu_req_attach {
	struct virtio_iommu_req_head		head;
	uint32_t					domain;
	uint32_t					endpoint;
	uint8_t					reserved[8];
	struct virtio_iommu_req_tail		tail;
};

struct virtio_iommu_req_detach {
	struct virtio_iommu_req_head		head;
	uint32_t					domain;
	uint32_t					endpoint;
	uint8_t					reserved[8];
	struct virtio_iommu_req_tail		tail;
};

#define VIRTIO_IOMMU_MAP_F_READ			(1 << 0)
#define VIRTIO_IOMMU_MAP_F_WRITE		(1 << 1)
#define VIRTIO_IOMMU_MAP_F_MMIO			(1 << 2)

#define VIRTIO_IOMMU_MAP_F_MASK			(VIRTIO_IOMMU_MAP_F_READ |	\
						 VIRTIO_IOMMU_MAP_F_WRITE |	\
						 VIRTIO_IOMMU_MAP_F_MMIO)

struct virtio_iommu_req_map {
	struct virtio_iommu_req_head		head;
	uint32_t					domain;
	uint64_t					virt_start;
	uint64_t					virt_end;
	uint64_t					phys_start;
	uint32_t					flags;
	struct virtio_iommu_req_tail		tail;
};

struct virtio_iommu_req_unmap {
	struct virtio_iommu_req_head		head;
	uint32_t					domain;
	uint64_t					virt_start;
	uint64_t					virt_end;
	uint8_t					reserved[4];
	struct virtio_iommu_req_tail		tail;
};

#define VIRTIO_IOMMU_PROBE_T_NONE		0
#define VIRTIO_IOMMU_PROBE_T_RESV_MEM		1

#define VIRTIO_IOMMU_PROBE_T_MASK		0xfff

struct virtio_iommu_probe_property {
	uint16_t					type;
	uint16_t					length;
};

#define VIRTIO_IOMMU_RESV_MEM_T_RESERVED	0
#define VIRTIO_IOMMU_RESV_MEM_T_MSI		1

struct virtio_iommu_probe_resv_mem {
	struct virtio_iommu_probe_property	head;
	uint8_t					subtype;
	uint8_t					reserved[3];
	uint64_t					start;
	uint64_t					end;
};

struct virtio_iommu_req_probe {
	struct virtio_iommu_req_head		head;
	uint32_t					endpoint;
	uint8_t					reserved[64];

	uint8_t					properties[];

	/*
	 * Tail follows the variable-length properties array. No padding,
	 * property lengths are all aligned on 8 bytes.
	 */
};

/* Fault types */
#define VIRTIO_IOMMU_FAULT_R_UNKNOWN		0
#define VIRTIO_IOMMU_FAULT_R_DOMAIN		1
#define VIRTIO_IOMMU_FAULT_R_MAPPING		2

#define VIRTIO_IOMMU_FAULT_F_READ		(1 << 0)
#define VIRTIO_IOMMU_FAULT_F_WRITE		(1 << 1)
#define VIRTIO_IOMMU_FAULT_F_EXEC		(1 << 2)
#define VIRTIO_IOMMU_FAULT_F_ADDRESS		(1 << 8)

struct virtio_iommu_fault {
	uint8_t					reason;
	uint8_t					reserved[3];
	uint32_t					flags;
	uint32_t					endpoint;
	uint8_t					reserved2[4];
	uint64_t					address;
};

#endif
