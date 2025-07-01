/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace interfaces for /dev/mshv* devices and derived fds
 * Includes:
 * - VMM APIs for parent (nested/baremetal root) partition APIs
 * - VMM APIs for VTL0 APIs
 * - Debug and performance metrics APIs
 *
 * This file is divided into sections containing data structures and IOCTLs for
 * a particular set of related devices or derived file descriptors.
 *
 * The IOCTL definitions are at the end of each section. They are grouped by
 * device/fd, so that new IOCTLs can easily be added with a monotonically
 * increasing number.
 */

#ifndef HW_HYPERV_LINUX_MSHV_H
#define HW_HYPERV_LINUX_MSHV_H

#include <linux/types.h>
#include <stdint.h>

#define MSHV_IOCTL	0xB8

typedef enum hv_register_name {
	/* Pending Interruption Register */
	HV_REGISTER_PENDING_INTERRUPTION = 0x00010002,

	/* X64 User-Mode Registers */
	HV_X64_REGISTER_RAX		= 0x00020000,
	HV_X64_REGISTER_RCX		= 0x00020001,
	HV_X64_REGISTER_RDX		= 0x00020002,
	HV_X64_REGISTER_RBX		= 0x00020003,
	HV_X64_REGISTER_RSP		= 0x00020004,
	HV_X64_REGISTER_RBP		= 0x00020005,
	HV_X64_REGISTER_RSI		= 0x00020006,
	HV_X64_REGISTER_RDI		= 0x00020007,
	HV_X64_REGISTER_R8		= 0x00020008,
	HV_X64_REGISTER_R9		= 0x00020009,
	HV_X64_REGISTER_R10		= 0x0002000A,
	HV_X64_REGISTER_R11		= 0x0002000B,
	HV_X64_REGISTER_R12		= 0x0002000C,
	HV_X64_REGISTER_R13		= 0x0002000D,
	HV_X64_REGISTER_R14		= 0x0002000E,
	HV_X64_REGISTER_R15		= 0x0002000F,
	HV_X64_REGISTER_RIP		= 0x00020010,
	HV_X64_REGISTER_RFLAGS	= 0x00020011,

	/* X64 Floating Point and Vector Registers */
	HV_X64_REGISTER_XMM0				= 0x00030000,
	HV_X64_REGISTER_XMM1				= 0x00030001,
	HV_X64_REGISTER_XMM2				= 0x00030002,
	HV_X64_REGISTER_XMM3				= 0x00030003,
	HV_X64_REGISTER_XMM4				= 0x00030004,
	HV_X64_REGISTER_XMM5				= 0x00030005,
	HV_X64_REGISTER_XMM6				= 0x00030006,
	HV_X64_REGISTER_XMM7				= 0x00030007,
	HV_X64_REGISTER_XMM8				= 0x00030008,
	HV_X64_REGISTER_XMM9				= 0x00030009,
	HV_X64_REGISTER_XMM10				= 0x0003000A,
	HV_X64_REGISTER_XMM11				= 0x0003000B,
	HV_X64_REGISTER_XMM12				= 0x0003000C,
	HV_X64_REGISTER_XMM13				= 0x0003000D,
	HV_X64_REGISTER_XMM14				= 0x0003000E,
	HV_X64_REGISTER_XMM15				= 0x0003000F,
	HV_X64_REGISTER_FP_MMX0				= 0x00030010,
	HV_X64_REGISTER_FP_MMX1				= 0x00030011,
	HV_X64_REGISTER_FP_MMX2				= 0x00030012,
	HV_X64_REGISTER_FP_MMX3				= 0x00030013,
	HV_X64_REGISTER_FP_MMX4				= 0x00030014,
	HV_X64_REGISTER_FP_MMX5				= 0x00030015,
	HV_X64_REGISTER_FP_MMX6				= 0x00030016,
	HV_X64_REGISTER_FP_MMX7				= 0x00030017,
	HV_X64_REGISTER_FP_CONTROL_STATUS	= 0x00030018,
	HV_X64_REGISTER_XMM_CONTROL_STATUS	= 0x00030019,

	/* X64 Control Registers */
	HV_X64_REGISTER_CR0		= 0x00040000,
	HV_X64_REGISTER_CR2		= 0x00040001,
	HV_X64_REGISTER_CR3		= 0x00040002,
	HV_X64_REGISTER_CR4		= 0x00040003,
	HV_X64_REGISTER_CR8		= 0x00040004,
	HV_X64_REGISTER_XFEM	= 0x00040005,

	/* X64 Segment Registers */
	HV_X64_REGISTER_ES		= 0x00060000,
	HV_X64_REGISTER_CS		= 0x00060001,
	HV_X64_REGISTER_SS		= 0x00060002,
	HV_X64_REGISTER_DS		= 0x00060003,
	HV_X64_REGISTER_FS		= 0x00060004,
	HV_X64_REGISTER_GS		= 0x00060005,
	HV_X64_REGISTER_LDTR	= 0x00060006,
	HV_X64_REGISTER_TR		= 0x00060007,

	/* X64 Table Registers */
	HV_X64_REGISTER_IDTR	= 0x00070000,
	HV_X64_REGISTER_GDTR	= 0x00070001,

	/* X64 Virtualized MSRs */
	HV_X64_REGISTER_TSC				= 0x00080000,
	HV_X64_REGISTER_EFER			= 0x00080001,
	HV_X64_REGISTER_KERNEL_GS_BASE	= 0x00080002,
	HV_X64_REGISTER_APIC_BASE		= 0x00080003,
	HV_X64_REGISTER_PAT				= 0x00080004,
	HV_X64_REGISTER_SYSENTER_CS		= 0x00080005,
	HV_X64_REGISTER_SYSENTER_EIP	= 0x00080006,
	HV_X64_REGISTER_SYSENTER_ESP	= 0x00080007,
	HV_X64_REGISTER_STAR			= 0x00080008,
	HV_X64_REGISTER_LSTAR			= 0x00080009,
	HV_X64_REGISTER_CSTAR			= 0x0008000A,
	HV_X64_REGISTER_SFMASK			= 0x0008000B,
	HV_X64_REGISTER_INITIAL_APIC_ID	= 0x0008000C,

	/* X64 Cache control MSRs */
	HV_X64_REGISTER_MSR_MTRR_CAP			= 0x0008000D,
	HV_X64_REGISTER_MSR_MTRR_DEF_TYPE		= 0x0008000E,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE0		= 0x00080010,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE1		= 0x00080011,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE2		= 0x00080012,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE3		= 0x00080013,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE4		= 0x00080014,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE5		= 0x00080015,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE6		= 0x00080016,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE7		= 0x00080017,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE8		= 0x00080018,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASE9		= 0x00080019,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEA		= 0x0008001A,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEB		= 0x0008001B,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEC		= 0x0008001C,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASED		= 0x0008001D,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEE		= 0x0008001E,
	HV_X64_REGISTER_MSR_MTRR_PHYS_BASEF		= 0x0008001F,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK0		= 0x00080040,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK1		= 0x00080041,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK2		= 0x00080042,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK3		= 0x00080043,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK4		= 0x00080044,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK5		= 0x00080045,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK6		= 0x00080046,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK7		= 0x00080047,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK8		= 0x00080048,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASK9		= 0x00080049,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKA		= 0x0008004A,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKB		= 0x0008004B,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKC		= 0x0008004C,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKD		= 0x0008004D,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKE		= 0x0008004E,
	HV_X64_REGISTER_MSR_MTRR_PHYS_MASKF		= 0x0008004F,
	HV_X64_REGISTER_MSR_MTRR_FIX64K00000	= 0x00080070,
	HV_X64_REGISTER_MSR_MTRR_FIX16K80000	= 0x00080071,
	HV_X64_REGISTER_MSR_MTRR_FIX16KA0000	= 0x00080072,
	HV_X64_REGISTER_MSR_MTRR_FIX4KC0000		= 0x00080073,
	HV_X64_REGISTER_MSR_MTRR_FIX4KC8000		= 0x00080074,
	HV_X64_REGISTER_MSR_MTRR_FIX4KD0000		= 0x00080075,
	HV_X64_REGISTER_MSR_MTRR_FIX4KD8000		= 0x00080076,
	HV_X64_REGISTER_MSR_MTRR_FIX4KE0000		= 0x00080077,
	HV_X64_REGISTER_MSR_MTRR_FIX4KE8000		= 0x00080078,
	HV_X64_REGISTER_MSR_MTRR_FIX4KF0000		= 0x00080079,
	HV_X64_REGISTER_MSR_MTRR_FIX4KF8000		= 0x0008007A,

	HV_X64_REGISTER_TSC_AUX		= 0x0008007B,
	HV_X64_REGISTER_BNDCFGS		= 0x0008007C,
	HV_X64_REGISTER_DEBUG_CTL	= 0x0008007D,

	/* Available */

	HV_X64_REGISTER_SPEC_CTRL		= 0x00080084,
	HV_X64_REGISTER_TSC_ADJUST	    = 0x00080096,

	/* Other MSRs */
	HV_X64_REGISTER_MSR_IA32_MISC_ENABLE = 0x000800A0,

	/* Misc */
	HV_REGISTER_GUEST_OS_ID			= 0x00090002,
	HV_REGISTER_REFERENCE_TSC		= 0x00090017,

	/* Hypervisor-defined Registers (Synic) */
	HV_REGISTER_SINT0		= 0x000A0000,
	HV_REGISTER_SINT1		= 0x000A0001,
	HV_REGISTER_SINT2		= 0x000A0002,
	HV_REGISTER_SINT3		= 0x000A0003,
	HV_REGISTER_SINT4		= 0x000A0004,
	HV_REGISTER_SINT5		= 0x000A0005,
	HV_REGISTER_SINT6		= 0x000A0006,
	HV_REGISTER_SINT7		= 0x000A0007,
	HV_REGISTER_SINT8		= 0x000A0008,
	HV_REGISTER_SINT9		= 0x000A0009,
	HV_REGISTER_SINT10		= 0x000A000A,
	HV_REGISTER_SINT11		= 0x000A000B,
	HV_REGISTER_SINT12		= 0x000A000C,
	HV_REGISTER_SINT13		= 0x000A000D,
	HV_REGISTER_SINT14		= 0x000A000E,
	HV_REGISTER_SINT15		= 0x000A000F,
	HV_REGISTER_SCONTROL	= 0x000A0010,
	HV_REGISTER_SVERSION	= 0x000A0011,
	HV_REGISTER_SIEFP		= 0x000A0012,
	HV_REGISTER_SIMP		= 0x000A0013,
	HV_REGISTER_EOM			= 0x000A0014,
	HV_REGISTER_SIRBP		= 0x000A0015,
} hv_register_name;

enum hv_intercept_type {
	HV_INTERCEPT_TYPE_X64_IO_PORT		= 0X00000000,
	HV_INTERCEPT_TYPE_X64_MSR			= 0X00000001,
	HV_INTERCEPT_TYPE_X64_CPUID			= 0X00000002,
	HV_INTERCEPT_TYPE_EXCEPTION			= 0X00000003,

	/* Used to be HV_INTERCEPT_TYPE_REGISTER */
	HV_INTERCEPT_TYPE_RESERVED0			= 0X00000004,
	HV_INTERCEPT_TYPE_MMIO				= 0X00000005,
	HV_INTERCEPT_TYPE_X64_GLOBAL_CPUID	= 0X00000006,
	HV_INTERCEPT_TYPE_X64_APIC_SMI		= 0X00000007,
	HV_INTERCEPT_TYPE_HYPERCALL			= 0X00000008,

	HV_INTERCEPT_TYPE_X64_APIC_INIT_SIPI		= 0X00000009,
	HV_INTERCEPT_MC_UPDATE_PATCH_LEVEL_MSR_READ	= 0X0000000A,

	HV_INTERCEPT_TYPE_X64_APIC_WRITE		= 0X0000000B,
	HV_INTERCEPT_TYPE_X64_MSR_INDEX			= 0X0000000C,
	HV_INTERCEPT_TYPE_MAX,
	HV_INTERCEPT_TYPE_INVALID			    = 0XFFFFFFFF,
};

struct hv_u128 {
	__u64 low_part;
	__u64 high_part;
};

union hv_x64_xmm_control_status_register {
	struct hv_u128 as_uint128;
	struct {
		union {
			/* long mode */
			__u64 last_fp_rdp;
			/* 32 bit mode */
			struct {
				__u32 last_fp_dp;
				__u16 last_fp_ds;
				__u16 padding;
			};
		};
		__u32 xmm_status_control;
		__u32 xmm_status_control_mask;
	};
};

union hv_x64_fp_register {
	struct hv_u128 as_uint128;
	struct {
		__u64 mantissa;
		__u64 biased_exponent : 15;
		__u64 sign : 1;
		__u64 reserved : 48;
	};
};

union hv_x64_pending_exception_event {
	__u64 as_uint64[2];
	struct {
		__u32 event_pending : 1;
		__u32 event_type : 3;
		__u32 reserved0 : 4;
		__u32 deliver_error_code : 1;
		__u32 reserved1 : 7;
		__u32 vector : 16;
		__u32 error_code;
		__u64 exception_parameter;
	};
};

union hv_x64_pending_virtualization_fault_event {
	__u64 as_uint64[2];
	struct {
		__u32 event_pending : 1;
		__u32 event_type : 3;
		__u32 reserved0 : 4;
		__u32 reserved1 : 8;
		__u32 parameter0 : 16;
		__u32 code;
		__u64 parameter1;
	};
};

union hv_x64_pending_interruption_register {
	__u64 as_uint64;
	struct {
		__u32 interruption_pending : 1;
		__u32 interruption_type : 3;
		__u32 deliver_error_code : 1;
		__u32 instruction_length : 4;
		__u32 nested_event : 1;
		__u32 reserved : 6;
		__u32 interruption_vector : 16;
		__u32 error_code;
	};
};

union hv_x64_register_sev_control {
	__u64 as_uint64;
	struct {
		__u64 enable_encrypted_state : 1;
		__u64 reserved_z : 11;
		__u64 vmsa_gpa_page_number : 52;
	};
};

union hv_x64_msr_npiep_config_contents {
	__u64 as_uint64;
	struct {
		/*
		 * These bits enable instruction execution prevention for
		 * specific instructions.
		 */
		__u64 prevents_gdt : 1;
		__u64 prevents_idt : 1;
		__u64 prevents_ldt : 1;
		__u64 prevents_tr : 1;

		/* The reserved bits must always be 0. */
		__u64 reserved : 60;
	};
};

typedef struct hv_x64_segment_register {
	__u64 base;
	__u32 limit;
	__u16 selector;
	union {
		struct {
			__u16 segment_type : 4;
			__u16 non_system_segment : 1;
			__u16 descriptor_privilege_level : 2;
			__u16 present : 1;
			__u16 reserved : 4;
			__u16 available : 1;
			__u16 _long : 1;
			__u16 _default : 1;
			__u16 granularity : 1;
		};
		__u16 attributes;
	};
} hv_x64_segment_register;

typedef struct hv_x64_table_register {
	__u16 pad[3];
	__u16 limit;
	__u64 base;
} hv_x64_table_register;

union hv_x64_fp_control_status_register {
	struct hv_u128 as_uint128;
	struct {
		__u16 fp_control;
		__u16 fp_status;
		__u8 fp_tag;
		__u8 reserved;
		__u16 last_fp_op;
		union {
			/* long mode */
			__u64 last_fp_rip;
			/* 32 bit mode */
			struct {
				__u32 last_fp_eip;
				__u16 last_fp_cs;
				__u16 padding;
			};
		};
	};
};

/* General Hypervisor Register Content Definitions */

union hv_explicit_suspend_register {
	__u64 as_uint64;
	struct {
		__u64 suspended : 1;
		__u64 reserved : 63;
	};
};

union hv_internal_activity_register {
	__u64 as_uint64;

	struct {
		__u64 startup_suspend : 1;
		__u64 halt_suspend : 1;
		__u64 idle_suspend : 1;
		__u64 rsvd_z : 61;
	};
};

union hv_x64_interrupt_state_register {
	__u64 as_uint64;
	struct {
		__u64 interrupt_shadow : 1;
		__u64 nmi_masked : 1;
		__u64 reserved : 62;
	};
};

union hv_intercept_suspend_register {
	__u64 as_uint64;
	struct {
		__u64 suspended : 1;
		__u64 reserved : 63;
	};
};

union hv_register_value {
	struct hv_u128 reg128;
	__u64 reg64;
	__u32 reg32;
	__u16 reg16;
	__u8 reg8;
	union hv_x64_fp_register fp;
	union hv_x64_fp_control_status_register fp_control_status;
	union hv_x64_xmm_control_status_register xmm_control_status;
	struct hv_x64_segment_register segment;
	struct hv_x64_table_register table;
	union hv_explicit_suspend_register explicit_suspend;
	union hv_intercept_suspend_register intercept_suspend;
	union hv_internal_activity_register internal_activity;
	union hv_x64_interrupt_state_register interrupt_state;
	union hv_x64_pending_interruption_register pending_interruption;
	union hv_x64_msr_npiep_config_contents npiep_config;
	union hv_x64_pending_exception_event pending_exception_event;
	union hv_x64_pending_virtualization_fault_event
		pending_virtualization_fault_event;
	union hv_x64_register_sev_control sev_control;
};

typedef struct hv_register_assoc {
	__u32 name;			/* enum hv_register_name */
	__u32 reserved1;
	__u64 reserved2;
	union hv_register_value value;
} hv_register_assoc;

#define MSHV_VP_MAX_REGISTERS	128

struct mshv_vp_registers {
	int count; /* at most MSHV_VP_MAX_REGISTERS */
	struct hv_register_assoc *regs;
};

/**
 * struct mshv_user_mem_region - arguments for MSHV_SET_GUEST_MEMORY
 * @size: Size of the memory region (bytes). Must be aligned to PAGE_SIZE
 * @guest_pfn: Base guest page number to map
 * @userspace_addr: Base address of userspace memory. Must be aligned to
 *                  PAGE_SIZE
 * @flags: Bitmask of 1 << MSHV_SET_MEM_BIT_*. If (1 << MSHV_SET_MEM_BIT_UNMAP)
 *         is set, ignore other bits.
 * @rsvd: MBZ
 *
 * Map or unmap a region of userspace memory to Guest Physical Addresses (GPA).
 * Mappings can't overlap in GPA space or userspace.
 * To unmap, these fields must match an existing mapping.
 */
typedef struct mshv_user_mem_region {
	__u64 size;
	__u64 guest_pfn;
	__u64 userspace_addr;
	__u8 flags;
	__u8 rsvd[7];
} mshv_user_mem_region;

enum {
	MSHV_SET_MEM_BIT_WRITABLE,
	MSHV_SET_MEM_BIT_EXECUTABLE,
	MSHV_SET_MEM_BIT_UNMAP,
	MSHV_SET_MEM_BIT_COUNT
};
#define MSHV_SET_MEM_FLAGS_MASK ((1 << MSHV_SET_MEM_BIT_COUNT) - 1)

enum {
	MSHV_PT_BIT_LAPIC,
	MSHV_PT_BIT_X2APIC,
	MSHV_PT_BIT_GPA_SUPER_PAGES,
	MSHV_PT_BIT_COUNT,
};
#define MSHV_PT_FLAGS_MASK ((1 << MSHV_PT_BIT_COUNT) - 1)

enum {
	MSHV_PT_ISOLATION_NONE,
	MSHV_PT_ISOLATION_SNP,
	MSHV_PT_ISOLATION_COUNT,
};

enum {
	MSHV_IOEVENTFD_BIT_DATAMATCH,
	MSHV_IOEVENTFD_BIT_PIO,
	MSHV_IOEVENTFD_BIT_DEASSIGN,
	MSHV_IOEVENTFD_BIT_COUNT,
};
#define MSHV_IOEVENTFD_FLAGS_MASK	((1 << MSHV_IOEVENTFD_BIT_COUNT) - 1)

union hv_interrupt_control {
	__u64 as_uint64;
	struct {
		__u32 interrupt_type; /* enum hv_interrupt type */
		__u32 level_triggered : 1;
		__u32 logical_dest_mode : 1;
		__u32 rsvd : 30;
	};
};

struct hv_input_assert_virtual_interrupt {
	__u64 partition_id;
	union hv_interrupt_control control;
	__u64 dest_addr; /* cpu's apic id */
	__u32 vector;
	__u8 target_vtl;
	__u8 rsvd_z0;
	__u16 rsvd_z1;
};

struct hv_register_x64_cpuid_result_parameters {
	struct {
		__u32 eax;
		__u32 ecx;
		__u8 subleaf_specific;
		__u8 always_override;
		__u16 padding;
	} input;
	struct {
		__u32 eax;
		__u32 eax_mask;
		__u32 ebx;
		__u32 ebx_mask;
		__u32 ecx;
		__u32 ecx_mask;
		__u32 edx;
		__u32 edx_mask;
	} result;
};

struct hv_register_x64_msr_result_parameters {
	__u32 msr_index;
	__u32 access_type;
	__u32 action; /* enum hv_unimplemented_msr_action */
};

union hv_register_intercept_result_parameters {
	struct hv_register_x64_cpuid_result_parameters cpuid;
	struct hv_register_x64_msr_result_parameters msr;
};

struct mshv_register_intercept_result {
	__u32 intercept_type; /* enum hv_intercept_type */
	union hv_register_intercept_result_parameters parameters;
};

typedef struct mshv_user_ioeventfd {
	__u64 datamatch;
	__u64 addr;	   /* legal pio/mmio address */
	__u32 len;	   /* 1, 2, 4, or 8 bytes    */
	__s32 fd;
	__u32 flags;
	__u8  rsvd[4];
} mshv_user_ioeventfd;

typedef struct mshv_user_irq_entry {
	__u32 gsi;
	__u32 address_lo;
	__u32 address_hi;
	__u32 data;
} mshv_user_irq_entry;

struct mshv_user_irq_table {
	__u32 nr;
	__u32 rsvd; /* MBZ */
	struct mshv_user_irq_entry entries[0];
};

enum {
	MSHV_IRQFD_BIT_DEASSIGN,
	MSHV_IRQFD_BIT_RESAMPLE,
	MSHV_IRQFD_BIT_COUNT,
};
#define MSHV_IRQFD_FLAGS_MASK	((1 << MSHV_IRQFD_BIT_COUNT) - 1)

struct mshv_user_irqfd {
	__s32 fd;
	__s32 resamplefd;
	__u32 gsi;
	__u32 flags;
};

/**
 * struct mshv_create_partition - arguments for MSHV_CREATE_PARTITION
 * @pt_flags: Bitmask of 1 << MSHV_PT_BIT_*
 * @pt_isolation: MSHV_PT_ISOLATION_*
 *
 * Returns a file descriptor to act as a handle to a guest partition.
 * At this point the partition is not yet initialized in the hypervisor.
 * Some operations must be done with the partition in this state, e.g. setting
 * so-called "early" partition properties. The partition can then be
 * initialized with MSHV_INITIALIZE_PARTITION.
 */
struct mshv_create_partition {
	__u64 pt_flags;
	__u64 pt_isolation;
};

struct mshv_create_vp {
	__u32 vp_index;
};

enum hv_translate_gva_result_code {
	HV_TRANSLATE_GVA_SUCCESS					= 0,

	/* Translation failures. */
	HV_TRANSLATE_GVA_PAGE_NOT_PRESENT			= 1,
	HV_TRANSLATE_GVA_PRIVILEGE_VIOLATION		= 2,
	HV_TRANSLATE_GVA_INVALIDE_PAGE_TABLE_FLAGS	= 3,

	/* GPA access failures. */
	HV_TRANSLATE_GVA_GPA_UNMAPPED				= 4,
	HV_TRANSLATE_GVA_GPA_NO_READ_ACCESS			= 5,
	HV_TRANSLATE_GVA_GPA_NO_WRITE_ACCESS		= 6,
	HV_TRANSLATE_GVA_GPA_ILLEGAL_OVERLAY_ACCESS	= 7,

	/*
	 * Intercept for memory access by either
	 *  - a higher VTL
	 *  - a nested hypervisor (due to a violation of the nested page table)
	 */
	HV_TRANSLATE_GVA_INTERCEPT					= 8,

	HV_TRANSLATE_GVA_GPA_UNACCEPTED				= 9,
};

union hv_translate_gva_result {
	__u64 as_uint64;
	struct {
		__u32 result_code; /* enum hv_translate_hva_result_code */
		__u32 cache_type : 8;
		__u32 overlay_page : 1;
		__u32 reserved : 23;
	};
};

typedef struct mshv_translate_gva {
	__u64 gva;
	__u64 flags;
	union hv_translate_gva_result *result;
	__u64 *gpa;
} mshv_translate_gva;

/* /dev/mshv */
#define MSHV_CREATE_PARTITION	_IOW(MSHV_IOCTL, 0x00, struct mshv_create_partition)
#define MSHV_CREATE_VP			_IOW(MSHV_IOCTL, 0x01, struct mshv_create_vp)

/* Partition fds created with MSHV_CREATE_PARTITION */
#define MSHV_INITIALIZE_PARTITION	_IO(MSHV_IOCTL, 0x00)
#define MSHV_SET_GUEST_MEMORY		_IOW(MSHV_IOCTL, 0x02, struct mshv_user_mem_region)
#define MSHV_IRQFD					_IOW(MSHV_IOCTL, 0x03, struct mshv_user_irqfd)
#define MSHV_IOEVENTFD			    _IOW(MSHV_IOCTL, 0x04, struct mshv_user_ioeventfd)
#define MSHV_SET_MSI_ROUTING		_IOW(MSHV_IOCTL, 0x05, struct mshv_user_irq_table)

/* TODO: replace with ROOT_HVCALL */
#define MSHV_GET_VP_REGISTERS		_IOWR(MSHV_IOCTL, 0xF0, struct mshv_vp_registers)
#define MSHV_SET_VP_REGISTERS		_IOW(MSHV_IOCTL, 0xF1, struct mshv_vp_registers)
#define MSHV_TRANSLATE_GVA			_IOWR(MSHV_IOCTL, 0xF2, struct mshv_translate_gva)

#define MSHV_VP_REGISTER_INTERCEPT_RESULT _IOW(MSHV_IOCTL, 0xF3, struct mshv_register_intercept_result)

/*
 ********************************
 * VP APIs for child partitions *
 ********************************
 */

enum {
	MSHV_VP_STATE_LAPIC = 0,
	MSHV_VP_STATE_XSAVE, /* XSAVE data in compacted form */
	MSHV_VP_STATE_SIMP,
	MSHV_VP_STATE_SIEFP,
	MSHV_VP_STATE_SYNTHETIC_TIMERS,
	MSHV_VP_STATE_COUNT,
};

typedef struct mshv_get_set_vp_state {
	__u8 type;	/* MSHV_VP_STATE_* */
	__u8 rsvd[3];	/* MBZ */
	__u32 buf_sz;	/* in - 4k page-aligned size of buffer.
			 * out - actual size of data.
			 * On EINVAL, check this to see if buffer was too small
			 */
	__u64 buf_ptr;	/* 4k page-aligned data buffer. */
} mshv_get_set_vp_state;

struct hv_local_interrupt_controller_state {
	/* HV_X64_INTERRUPT_CONTROLLER_STATE */
	__u32 apic_id;
	__u32 apic_version;
	__u32 apic_ldr;
	__u32 apic_dfr;
	__u32 apic_spurious;
	__u32 apic_isr[8];
	__u32 apic_tmr[8];
	__u32 apic_irr[8];
	__u32 apic_esr;
	__u32 apic_icr_high;
	__u32 apic_icr_low;
	__u32 apic_lvt_timer;
	__u32 apic_lvt_thermal;
	__u32 apic_lvt_perfmon;
	__u32 apic_lvt_lint0;
	__u32 apic_lvt_lint1;
	__u32 apic_lvt_error;
	__u32 apic_lvt_cmci;
	__u32 apic_error_status;
	__u32 apic_initial_count;
	__u32 apic_counter_value;
	__u32 apic_divide_configuration;
	__u32 apic_remote_read;
};

#define MSHV_RUN_VP_BUF_SZ 256

struct mshv_run_vp {
	__u8 msg_buf[MSHV_RUN_VP_BUF_SZ];
};

#define MSHV_RUN_VP			    _IOR(MSHV_IOCTL, 0x00, struct mshv_run_vp)
#define MSHV_GET_VP_STATE		_IOWR(MSHV_IOCTL, 0x01, struct mshv_get_set_vp_state)
#define MSHV_SET_VP_STATE		_IOWR(MSHV_IOCTL, 0x02, struct mshv_get_set_vp_state)

/**
 * struct mshv_root_hvcall - arguments for MSHV_ROOT_HVCALL
 * @code: Hypercall code (HVCALL_*)
 * @reps: in: Rep count ('repcount')
 *	  out: Reps completed ('repcomp'). MBZ unless rep hvcall
 * @in_sz: Size of input incl rep data. <= HV_HYP_PAGE_SIZE
 * @out_sz: Size of output buffer. <= HV_HYP_PAGE_SIZE. MBZ if out_ptr is 0
 * @status: in: MBZ
 *	    out: HV_STATUS_* from hypercall
 * @rsvd: MBZ
 * @in_ptr: Input data buffer (struct hv_input_*). If used with partition or
 *	    vp fd, partition id field is added by kernel.
 * @out_ptr: Output data buffer (optional)
 */
struct mshv_root_hvcall {
	__u16 code;
	__u16 reps;
	__u16 in_sz;
	__u16 out_sz;
	__u16 status;
	__u8 rsvd[6];
	__u64 in_ptr;
	__u64 out_ptr;
};

/* Generic hypercall */
#define MSHV_ROOT_HVCALL		_IOWR(MSHV_IOCTL, 0x07, struct mshv_root_hvcall)

/* From hvgdk_mini.h */

#define HV_X64_MSR_GUEST_OS_ID		0x40000000
#define HV_X64_MSR_SINT0			0x40000090
#define HV_X64_MSR_SINT1			0x40000091
#define HV_X64_MSR_SINT2			0x40000092
#define HV_X64_MSR_SINT3			0x40000093
#define HV_X64_MSR_SINT4			0x40000094
#define HV_X64_MSR_SINT5			0x40000095
#define HV_X64_MSR_SINT6			0x40000096
#define HV_X64_MSR_SINT7			0x40000097
#define HV_X64_MSR_SINT8			0x40000098
#define HV_X64_MSR_SINT9			0x40000099
#define HV_X64_MSR_SINT10			0x4000009A
#define HV_X64_MSR_SINT11			0x4000009B
#define HV_X64_MSR_SINT12			0x4000009C
#define HV_X64_MSR_SINT13			0x4000009D
#define HV_X64_MSR_SINT14			0x4000009E
#define HV_X64_MSR_SINT15			0x4000009F
#define HV_X64_MSR_SCONTROL			0x40000080
#define HV_X64_MSR_SIEFP			0x40000082
#define HV_X64_MSR_SIMP				0x40000083
#define HV_X64_MSR_REFERENCE_TSC	0x40000021
#define HV_X64_MSR_EOM				0x40000084

/* Define port identifier type. */
union hv_port_id {
	__u32 as__u32;
	struct {
		__u32 id : 24;
		__u32 reserved : 8;
	};
};

#define HV_MESSAGE_SIZE			        (256)
#define HV_MESSAGE_PAYLOAD_BYTE_COUNT	(240)
#define HV_MESSAGE_PAYLOAD_QWORD_COUNT	(30)

/* Define hypervisor message types. */
enum hv_message_type {
	HVMSG_NONE							= 0x00000000,

	/* Memory access messages. */
	HVMSG_UNMAPPED_GPA					= 0x80000000,
	HVMSG_GPA_INTERCEPT					= 0x80000001,
	HVMSG_UNACCEPTED_GPA				= 0x80000003,
	HVMSG_GPA_ATTRIBUTE_INTERCEPT		= 0x80000004,

	/* Timer notification messages. */
	HVMSG_TIMER_EXPIRED					= 0x80000010,

	/* Error messages. */
	HVMSG_INVALID_VP_REGISTER_VALUE		= 0x80000020,
	HVMSG_UNRECOVERABLE_EXCEPTION		= 0x80000021,
	HVMSG_UNSUPPORTED_FEATURE			= 0x80000022,

	/*
	 * Opaque intercept message. The original intercept message is only
	 * accessible from the mapped intercept message page.
	 */
	HVMSG_OPAQUE_INTERCEPT				= 0x8000003F,

	/* Trace buffer complete messages. */
	HVMSG_EVENTLOG_BUFFERCOMPLETE		= 0x80000040,

	/* Hypercall intercept */
	HVMSG_HYPERCALL_INTERCEPT			= 0x80000050,

	/* SynIC intercepts */
	HVMSG_SYNIC_EVENT_INTERCEPT			= 0x80000060,
	HVMSG_SYNIC_SINT_INTERCEPT			= 0x80000061,
	HVMSG_SYNIC_SINT_DELIVERABLE		= 0x80000062,

	/* Async call completion intercept */
	HVMSG_ASYNC_CALL_COMPLETION			= 0x80000070,

	/* Root scheduler messages */
	HVMSG_SCHEDULER_VP_SIGNAL_BITSE		= 0x80000100,
	HVMSG_SCHEDULER_VP_SIGNAL_PAIR		= 0x80000101,

	/* Platform-specific processor intercept messages. */
	HVMSG_X64_IO_PORT_INTERCEPT			= 0x80010000,
	HVMSG_X64_MSR_INTERCEPT				= 0x80010001,
	HVMSG_X64_CPUID_INTERCEPT			= 0x80010002,
	HVMSG_X64_EXCEPTION_INTERCEPT		= 0x80010003,
	HVMSG_X64_APIC_EOI					= 0x80010004,
	HVMSG_X64_LEGACY_FP_ERROR			= 0x80010005,
	HVMSG_X64_IOMMU_PRQ					= 0x80010006,
	HVMSG_X64_HALT						= 0x80010007,
	HVMSG_X64_INTERRUPTION_DELIVERABLE	= 0x80010008,
	HVMSG_X64_SIPI_INTERCEPT			= 0x80010009,
	HVMSG_X64_SEV_VMGEXIT_INTERCEPT		= 0x80010013,
};

union hv_x64_vp_execution_state {
	__u16 as_uint16;
	struct {
		__u16 cpl:2;
		__u16 cr0_pe:1;
		__u16 cr0_am:1;
		__u16 efer_lma:1;
		__u16 debug_active:1;
		__u16 interruption_pending:1;
		__u16 vtl:4;
		__u16 enclave_mode:1;
		__u16 interrupt_shadow:1;
		__u16 virtualization_fault_active:1;
		__u16 reserved:2;
	};
};

/* From openvmm::hvdef */
enum hv_x64_intercept_access_type {
	HV_X64_INTERCEPT_ACCESS_TYPE_READ = 0,
	HV_X64_INTERCEPT_ACCESS_TYPE_WRITE = 1,
	HV_X64_INTERCEPT_ACCESS_TYPE_EXECUTE = 2,
};

struct hv_x64_intercept_message_header {
	__u32 vp_index;
	__u8 instruction_length:4;
	__u8 cr8:4; /* Only set for exo partitions */
	__u8 intercept_access_type;
	union hv_x64_vp_execution_state execution_state;
	struct hv_x64_segment_register cs_segment;
	__u64 rip;
	__u64 rflags;
};

union hv_x64_io_port_access_info {
	__u8 as_uint8;
	struct {
		__u8 access_size:3;
		__u8 string_op:1;
		__u8 rep_prefix:1;
		__u8 reserved:3;
	};
};

typedef struct hv_x64_io_port_intercept_message {
	struct hv_x64_intercept_message_header header;
	__u16 port_number;
	union hv_x64_io_port_access_info access_info;
	__u8 instruction_byte_count;
	__u32 reserved;
	__u64 rax;
	__u8 instruction_bytes[16];
	struct hv_x64_segment_register ds_segment;
	struct hv_x64_segment_register es_segment;
	__u64 rcx;
	__u64 rsi;
	__u64 rdi;
} hv_x64_io_port_intercept_message;

union hv_x64_memory_access_info {
	__u8 as_uint8;
	struct {
		__u8 gva_valid:1;
		__u8 gva_gpa_valid:1;
		__u8 hypercall_output_pending:1;
		__u8 tlb_locked_no_overlay:1;
		__u8 reserved:4;
	};
};

struct hv_x64_memory_intercept_message {
	struct hv_x64_intercept_message_header header;
	__u32 cache_type; /* enum hv_cache_type */
	__u8 instruction_byte_count;
	union hv_x64_memory_access_info memory_access_info;
	__u8 tpr_priority;
	__u8 reserved1;
	__u64 guest_virtual_address;
	__u64 guest_physical_address;
	__u8 instruction_bytes[16];
};

union hv_message_flags {
	__u8 asu8;
	struct {
		__u8 msg_pending : 1;
		__u8 reserved : 7;
	};
};

struct hv_message_header {
	__u32 message_type;
	__u8 payload_size;
	union hv_message_flags message_flags;
	__u8 reserved[2];
	union {
		__u64 sender;
		union hv_port_id port;
	};
};

struct hv_message {
	struct hv_message_header header;
	union {
		__u64 payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
	} u;
};

/* From  github.com/rust-vmm/mshv-bindings/src/x86_64/regs.rs */

struct hv_cpuid_entry {
	uint32_t function;
	uint32_t index;
	uint32_t flags;
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t padding[3];
};

struct hv_cpuid {
	uint32_t nent;
	uint32_t padding;
	struct hv_cpuid_entry entries[0];
};

#define IA32_MSR_TSC 			0x00000010
#define IA32_MSR_EFER 			0xC0000080
#define IA32_MSR_KERNEL_GS_BASE 0xC0000102
#define IA32_MSR_APIC_BASE 		0x0000001B
#define IA32_MSR_PAT 			0x0277
#define IA32_MSR_SYSENTER_CS 	0x00000174
#define IA32_MSR_SYSENTER_ESP 	0x00000175
#define IA32_MSR_SYSENTER_EIP 	0x00000176
#define IA32_MSR_STAR 			0xC0000081
#define IA32_MSR_LSTAR 			0xC0000082
#define IA32_MSR_CSTAR 			0xC0000083
#define IA32_MSR_SFMASK 		0xC0000084

#define IA32_MSR_MTRR_CAP 		0x00FE
#define IA32_MSR_MTRR_DEF_TYPE 	0x02FF
#define IA32_MSR_MTRR_PHYSBASE0 0x0200
#define IA32_MSR_MTRR_PHYSMASK0 0x0201
#define IA32_MSR_MTRR_PHYSBASE1 0x0202
#define IA32_MSR_MTRR_PHYSMASK1 0x0203
#define IA32_MSR_MTRR_PHYSBASE2 0x0204
#define IA32_MSR_MTRR_PHYSMASK2 0x0205
#define IA32_MSR_MTRR_PHYSBASE3 0x0206
#define IA32_MSR_MTRR_PHYSMASK3 0x0207
#define IA32_MSR_MTRR_PHYSBASE4 0x0208
#define IA32_MSR_MTRR_PHYSMASK4 0x0209
#define IA32_MSR_MTRR_PHYSBASE5 0x020A
#define IA32_MSR_MTRR_PHYSMASK5 0x020B
#define IA32_MSR_MTRR_PHYSBASE6 0x020C
#define IA32_MSR_MTRR_PHYSMASK6 0x020D
#define IA32_MSR_MTRR_PHYSBASE7 0x020E
#define IA32_MSR_MTRR_PHYSMASK7 0x020F

#define IA32_MSR_MTRR_FIX64K_00000 0x0250
#define IA32_MSR_MTRR_FIX16K_80000 0x0258
#define IA32_MSR_MTRR_FIX16K_A0000 0x0259
#define IA32_MSR_MTRR_FIX4K_C0000 0x0268
#define IA32_MSR_MTRR_FIX4K_C8000 0x0269
#define IA32_MSR_MTRR_FIX4K_D0000 0x026A
#define IA32_MSR_MTRR_FIX4K_D8000 0x026B
#define IA32_MSR_MTRR_FIX4K_E0000 0x026C
#define IA32_MSR_MTRR_FIX4K_E8000 0x026D
#define IA32_MSR_MTRR_FIX4K_F0000 0x026E
#define IA32_MSR_MTRR_FIX4K_F8000 0x026F

#define IA32_MSR_TSC_AUX 		  0xC0000103
#define IA32_MSR_BNDCFGS 		  0x00000d90
#define IA32_MSR_DEBUG_CTL 		  0x1D9
#define IA32_MSR_SPEC_CTRL        0x00000048
#define IA32_MSR_TSC_ADJUST 	  0x0000003b

#define IA32_MSR_MISC_ENABLE 0x000001a0


#define HV_TRANSLATE_GVA_VALIDATE_READ	     (0x0001)
#define HV_TRANSLATE_GVA_VALIDATE_WRITE      (0x0002)
#define HV_TRANSLATE_GVA_VALIDATE_EXECUTE    (0x0004)

#endif
