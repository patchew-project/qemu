/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * IOMMU user API definitions
 */

#ifndef _IOMMU_H
#define _IOMMU_H

#include <linux/types.h>

#define IOMMU_FAULT_PERM_WRITE	(1 << 0) /* write */
#define IOMMU_FAULT_PERM_EXEC	(1 << 1) /* exec */
#define IOMMU_FAULT_PERM_PRIV	(1 << 2) /* privileged */

/*  Generic fault types, can be expanded IRQ remapping fault */
enum iommu_fault_type {
	IOMMU_FAULT_DMA_UNRECOV = 1,	/* unrecoverable fault */
	IOMMU_FAULT_PAGE_REQ,		/* page request fault */
};

enum iommu_fault_reason {
	IOMMU_FAULT_REASON_UNKNOWN = 0,

	/* Could not access the PASID table (fetch caused external abort) */
	IOMMU_FAULT_REASON_PASID_FETCH,

	/* pasid entry is invalid or has configuration errors */
	IOMMU_FAULT_REASON_BAD_PASID_ENTRY,

	/*
	 * PASID is out of range (e.g. exceeds the maximum PASID
	 * supported by the IOMMU) or disabled.
	 */
	IOMMU_FAULT_REASON_PASID_INVALID,

	/*
	 * An external abort occurred fetching (or updating) a translation
	 * table descriptor
	 */
	IOMMU_FAULT_REASON_WALK_EABT,

	/*
	 * Could not access the page table entry (Bad address),
	 * actual translation fault
	 */
	IOMMU_FAULT_REASON_PTE_FETCH,

	/* Protection flag check failed */
	IOMMU_FAULT_REASON_PERMISSION,

	/* access flag check failed */
	IOMMU_FAULT_REASON_ACCESS,

	/* Output address of a translation stage caused Address Size fault */
	IOMMU_FAULT_REASON_OOR_ADDRESS,
};

/**
 * Unrecoverable fault data
 * @reason: reason of the fault
 * @addr: offending page address
 * @fetch_addr: address that caused a fetch abort, if any
 * @pasid: contains process address space ID, used in shared virtual memory
 * @perm: Requested permission access using by the incoming transaction
 * (IOMMU_FAULT_PERM_* values)
 */
struct iommu_fault_unrecoverable {
	__u32	reason; /* enum iommu_fault_reason */
#define IOMMU_FAULT_UNRECOV_PASID_VALID		(1 << 0)
#define IOMMU_FAULT_UNRECOV_PERM_VALID		(1 << 1)
#define IOMMU_FAULT_UNRECOV_ADDR_VALID		(1 << 2)
#define IOMMU_FAULT_UNRECOV_FETCH_ADDR_VALID	(1 << 3)
	__u32	flags;
	__u32	pasid;
	__u32	perm;
	__u64	addr;
	__u64	fetch_addr;
};

/*
 * Page Request data (aka. recoverable fault data)
 * @flags : encodes whether the pasid is valid and whether this
 * is the last page in group
 * @pasid: pasid
 * @grpid: page request group index
 * @perm: requested page permissions (IOMMU_FAULT_PERM_* values)
 * @addr: page address
 */
struct iommu_fault_page_request {
#define IOMMU_FAULT_PAGE_REQUEST_PASID_VALID	(1 << 0)
#define IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE	(1 << 1)
#define IOMMU_FAULT_PAGE_REQUEST_PRIV_DATA	(1 << 2)
	__u32   flags;
	__u32	pasid;
	__u32	grpid;
	__u32	perm;
	__u64	addr;
	__u64	private_data[2];
};

/**
 * struct iommu_fault - Generic fault data
 *
 * @type contains fault type
 */

struct iommu_fault {
	__u32	type;   /* enum iommu_fault_type */
	__u32	reserved;
	union {
		struct iommu_fault_unrecoverable event;
		struct iommu_fault_page_request prm;
	};
};

/**
 * SMMUv3 Stream Table Entry stage 1 related information
 * The PASID table is referred to as the context descriptor (CD) table.
 *
 * @s1fmt: STE s1fmt (format of the CD table: single CD, linear table
   or 2-level table)
 * @s1dss: STE s1dss (specifies the behavior when pasid_bits != 0
   and no pasid is passed along with the incoming transaction)
 * Please refer to the smmu 3.x spec (ARM IHI 0070A) for full details
 */
struct iommu_pasid_smmuv3 {
#define PASID_TABLE_SMMUV3_CFG_VERSION_1 1
	__u32	version;
	__u8 s1fmt;
	__u8 s1dss;
	__u8 padding[2];
};

/**
 * PASID table data used to bind guest PASID table to the host IOMMU
 * Note PASID table corresponds to the Context Table on ARM SMMUv3.
 *
 * @version: API version to prepare for future extensions
 * @format: format of the PASID table
 * @base_ptr: guest physical address of the PASID table
 * @pasid_bits: number of PASID bits used in the PASID table
 * @config: indicates whether the guest translation stage must
 * be translated, bypassed or aborted.
 */
struct iommu_pasid_table_config {
#define PASID_TABLE_CFG_VERSION_1 1
	__u32	version;
#define IOMMU_PASID_FORMAT_SMMUV3	1
	__u32	format;
	__u64	base_ptr;
	__u8	pasid_bits;
#define IOMMU_PASID_CONFIG_TRANSLATE	1
#define IOMMU_PASID_CONFIG_BYPASS	2
#define IOMMU_PASID_CONFIG_ABORT	3
	__u8	config;
	__u8    padding[6];
	union {
		struct iommu_pasid_smmuv3 smmuv3;
	};
};

/* defines the granularity of the invalidation */
enum iommu_inv_granularity {
	IOMMU_INV_GRANU_DOMAIN,	/* domain-selective invalidation */
	IOMMU_INV_GRANU_PASID,	/* pasid-selective invalidation */
	IOMMU_INV_GRANU_ADDR,	/* page-selective invalidation */
};

/**
 * Address Selective Invalidation Structure
 *
 * @flags indicates the granularity of the address-selective invalidation
 * - if PASID bit is set, @pasid field is populated and the invalidation
 *   relates to cache entries tagged with this PASID and matching the
 *   address range.
 * - if ARCHID bit is set, @archid is populated and the invalidation relates
 *   to cache entries tagged with this architecture specific id and matching
 *   the address range.
 * - Both PASID and ARCHID can be set as they may tag different caches.
 * - if neither PASID or ARCHID is set, global addr invalidation applies
 * - LEAF flag indicates whether only the leaf PTE caching needs to be
 *   invalidated and other paging structure caches can be preserved.
 * @pasid: process address space id
 * @archid: architecture-specific id
 * @addr: first stage/level input address
 * @granule_size: page/block size of the mapping in bytes
 * @nb_granules: number of contiguous granules to be invalidated
 */
struct iommu_inv_addr_info {
#define IOMMU_INV_ADDR_FLAGS_PASID	(1 << 0)
#define IOMMU_INV_ADDR_FLAGS_ARCHID	(1 << 1)
#define IOMMU_INV_ADDR_FLAGS_LEAF	(1 << 2)
	__u32	flags;
	__u32	archid;
	__u64	pasid;
	__u64	addr;
	__u64	granule_size;
	__u64	nb_granules;
};

/**
 * First level/stage invalidation information
 * @cache: bitfield that allows to select which caches to invalidate
 * @granularity: defines the lowest granularity used for the invalidation:
 *     domain > pasid > addr
 *
 * Not all the combinations of cache/granularity make sense:
 *
 *         type |   DEV_IOTLB   |     IOTLB     |      PASID    |
 * granularity	|		|		|      cache	|
 * -------------+---------------+---------------+---------------+
 * DOMAIN	|	N/A	|       Y	|	Y	|
 * PASID	|	Y	|       Y	|	Y	|
 * ADDR		|       Y	|       Y	|	N/A	|
 *
 * Invalidations by %IOMMU_INV_GRANU_ADDR use field @addr_info.
 * Invalidations by %IOMMU_INV_GRANU_PASID use field @pasid.
 * Invalidations by %IOMMU_INV_GRANU_DOMAIN don't take any argument.
 *
 * If multiple cache types are invalidated simultaneously, they all
 * must support the used granularity.
 */
struct iommu_cache_invalidate_info {
#define IOMMU_CACHE_INVALIDATE_INFO_VERSION_1 1
	__u32	version;
/* IOMMU paging structure cache */
#define IOMMU_CACHE_INV_TYPE_IOTLB	(1 << 0) /* IOMMU IOTLB */
#define IOMMU_CACHE_INV_TYPE_DEV_IOTLB	(1 << 1) /* Device IOTLB */
#define IOMMU_CACHE_INV_TYPE_PASID	(1 << 2) /* PASID cache */
	__u8	cache;
	__u8	granularity;
	__u8	padding[2];
	union {
		__u64	pasid;
		struct iommu_inv_addr_info addr_info;
	};
};


#endif /* _IOMMU_H */
