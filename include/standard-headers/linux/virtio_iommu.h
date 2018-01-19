/*
 * Virtio-iommu definition v0.5
 *
 * Copyright (C) 2017 ARM Ltd.
 *
 * This header is BSD licensed so anyone can use the definitions
 * to implement compatible drivers/servers:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of ARM Ltd. nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _LINUX_VIRTIO_IOMMU_H
#define _LINUX_VIRTIO_IOMMU_H

/* Feature bits */
#define VIRTIO_IOMMU_F_INPUT_RANGE		0
#define VIRTIO_IOMMU_F_DOMAIN_BITS		1
#define VIRTIO_IOMMU_F_MAP_UNMAP		2
#define VIRTIO_IOMMU_F_BYPASS			3
#define VIRTIO_IOMMU_F_PROBE			4

struct virtio_iommu_config {
	/* Supported page sizes */
	uint64_t					page_size_mask;
	/* Supported IOVA range */
	struct virtio_iommu_range {
		uint64_t				start;
		uint64_t				end;
	} input_range;
	/* Max domain ID size */
	uint8_t 					domain_bits;
	uint8_t					padding[3];
	/* Probe buffer size */
	uint32_t					probe_size;
} QEMU_PACKED;

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

struct virtio_iommu_req_head {
	uint8_t					type;
	uint8_t					reserved[3];
} QEMU_PACKED;

struct virtio_iommu_req_tail {
	uint8_t					status;
	uint8_t					reserved[3];
} QEMU_PACKED;

struct virtio_iommu_req_attach {
	struct virtio_iommu_req_head		head;

	uint32_t					domain;
	uint32_t					endpoint;
	uint32_t					reserved;

	struct virtio_iommu_req_tail		tail;
} QEMU_PACKED;

struct virtio_iommu_req_detach {
	struct virtio_iommu_req_head		head;

	uint32_t					endpoint;
	uint32_t					reserved;

	struct virtio_iommu_req_tail		tail;
} QEMU_PACKED;

#define VIRTIO_IOMMU_MAP_F_READ			(1 << 0)
#define VIRTIO_IOMMU_MAP_F_WRITE		(1 << 1)
#define VIRTIO_IOMMU_MAP_F_EXEC			(1 << 2)

#define VIRTIO_IOMMU_MAP_F_MASK			(VIRTIO_IOMMU_MAP_F_READ |	\
						 VIRTIO_IOMMU_MAP_F_WRITE |	\
						 VIRTIO_IOMMU_MAP_F_EXEC)

struct virtio_iommu_req_map {
	struct virtio_iommu_req_head		head;

	uint32_t					domain;
	uint64_t					virt_addr;
	uint64_t					phys_addr;
	uint64_t					size;
	uint32_t					flags;

	struct virtio_iommu_req_tail		tail;
} QEMU_PACKED;

QEMU_PACKED
struct virtio_iommu_req_unmap {
	struct virtio_iommu_req_head		head;

	uint32_t					domain;
	uint64_t					virt_addr;
	uint64_t					size;
	uint32_t					reserved;

	struct virtio_iommu_req_tail		tail;
} QEMU_PACKED;

#define VIRTIO_IOMMU_RESV_MEM_T_RESERVED	0
#define VIRTIO_IOMMU_RESV_MEM_T_MSI		1

struct virtio_iommu_probe_resv_mem {
	uint8_t					subtype;
	uint8_t					reserved[3];
	uint64_t					addr;
	uint64_t					size;
} QEMU_PACKED;

#define VIRTIO_IOMMU_PROBE_T_NONE		0
#define VIRTIO_IOMMU_PROBE_T_RESV_MEM		1

#define VIRTIO_IOMMU_PROBE_T_MASK		0xfff

struct virtio_iommu_probe_property {
	uint16_t					type;
	uint16_t					length;
	uint8_t					value[];
} QEMU_PACKED;

struct virtio_iommu_req_probe {
	struct virtio_iommu_req_head		head;
	uint32_t					endpoint;
	uint8_t					reserved[64];

	uint8_t					properties[];

	/* Tail follows the variable-length properties array (no padding) */
} QEMU_PACKED;

union virtio_iommu_req {
	struct virtio_iommu_req_head		head;

	struct virtio_iommu_req_attach		attach;
	struct virtio_iommu_req_detach		detach;
	struct virtio_iommu_req_map		map;
	struct virtio_iommu_req_unmap		unmap;
	struct virtio_iommu_req_probe		probe;
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
	uint8_t					padding[3];
	uint32_t					flags;
	uint32_t					endpoint;
	uint64_t					address;
} QEMU_PACKED;

#endif
