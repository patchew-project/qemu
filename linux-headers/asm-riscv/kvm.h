/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#ifndef __LINUX_KVM_RISCV_H
#define __LINUX_KVM_RISCV_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/bitsperlong.h>
#include <asm/ptrace.h>

#define __KVM_HAVE_READONLY_MEM

#define KVM_COALESCED_MMIO_PAGE_OFFSET 1

#define KVM_INTERRUPT_SET	-1U
#define KVM_INTERRUPT_UNSET	-2U

/* for KVM_GET_REGS and KVM_SET_REGS */
struct kvm_regs {
};

/* for KVM_GET_FPU and KVM_SET_FPU */
struct kvm_fpu {
};

/* KVM Debug exit structure */
struct kvm_debug_exit_arch {
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
};

/* definition of registers in kvm_run */
struct kvm_sync_regs {
};

/* for KVM_GET_SREGS and KVM_SET_SREGS */
struct kvm_sregs {
};

/* CONFIG registers for KVM_GET_ONE_REG and KVM_SET_ONE_REG */
struct kvm_riscv_config {
	unsigned long isa;
	unsigned long zicbom_block_size;
	unsigned long mvendorid;
	unsigned long marchid;
	unsigned long mimpid;
	unsigned long zicboz_block_size;
};

/* CORE registers for KVM_GET_ONE_REG and KVM_SET_ONE_REG */
struct kvm_riscv_core {
	struct user_regs_struct regs;
	unsigned long mode;
};

/* Possible privilege modes for kvm_riscv_core */
#define KVM_RISCV_MODE_S	1
#define KVM_RISCV_MODE_U	0

/* General CSR registers for KVM_GET_ONE_REG and KVM_SET_ONE_REG */
struct kvm_riscv_csr {
	unsigned long sstatus;
	unsigned long sie;
	unsigned long stvec;
	unsigned long sscratch;
	unsigned long sepc;
	unsigned long scause;
	unsigned long stval;
	unsigned long sip;
	unsigned long satp;
	unsigned long scounteren;
};

/* AIA CSR registers for KVM_GET_ONE_REG and KVM_SET_ONE_REG */
struct kvm_riscv_aia_csr {
	unsigned long siselect;
	unsigned long iprio1;
	unsigned long iprio2;
	unsigned long sieh;
	unsigned long siph;
	unsigned long iprio1h;
	unsigned long iprio2h;
};

/* TIMER registers for KVM_GET_ONE_REG and KVM_SET_ONE_REG */
struct kvm_riscv_timer {
	__u64 frequency;
	__u64 time;
	__u64 compare;
	__u64 state;
};

/*
 * ISA extension IDs specific to KVM. This is not the same as the host ISA
 * extension IDs as that is internal to the host and should not be exposed
 * to the guest. This should always be contiguous to keep the mapping simple
 * in KVM implementation.
 */
enum KVM_RISCV_ISA_EXT_ID {
	KVM_RISCV_ISA_EXT_A = 0,
	KVM_RISCV_ISA_EXT_C,
	KVM_RISCV_ISA_EXT_D,
	KVM_RISCV_ISA_EXT_F,
	KVM_RISCV_ISA_EXT_H,
	KVM_RISCV_ISA_EXT_I,
	KVM_RISCV_ISA_EXT_M,
	KVM_RISCV_ISA_EXT_SVPBMT,
	KVM_RISCV_ISA_EXT_SSTC,
	KVM_RISCV_ISA_EXT_SVINVAL,
	KVM_RISCV_ISA_EXT_ZIHINTPAUSE,
	KVM_RISCV_ISA_EXT_ZICBOM,
	KVM_RISCV_ISA_EXT_ZICBOZ,
	KVM_RISCV_ISA_EXT_ZBB,
	KVM_RISCV_ISA_EXT_SSAIA,
	KVM_RISCV_ISA_EXT_MAX,
};

/*
 * SBI extension IDs specific to KVM. This is not the same as the SBI
 * extension IDs defined by the RISC-V SBI specification.
 */
enum KVM_RISCV_SBI_EXT_ID {
	KVM_RISCV_SBI_EXT_V01 = 0,
	KVM_RISCV_SBI_EXT_TIME,
	KVM_RISCV_SBI_EXT_IPI,
	KVM_RISCV_SBI_EXT_RFENCE,
	KVM_RISCV_SBI_EXT_SRST,
	KVM_RISCV_SBI_EXT_HSM,
	KVM_RISCV_SBI_EXT_PMU,
	KVM_RISCV_SBI_EXT_EXPERIMENTAL,
	KVM_RISCV_SBI_EXT_VENDOR,
	KVM_RISCV_SBI_EXT_MAX,
};

/* Possible states for kvm_riscv_timer */
#define KVM_RISCV_TIMER_STATE_OFF	0
#define KVM_RISCV_TIMER_STATE_ON	1

#define KVM_REG_SIZE(id)		\
	(1U << (((id) & KVM_REG_SIZE_MASK) >> KVM_REG_SIZE_SHIFT))

/* If you need to interpret the index values, here is the key: */
#define KVM_REG_RISCV_TYPE_MASK		0x00000000FF000000
#define KVM_REG_RISCV_TYPE_SHIFT	24
#define KVM_REG_RISCV_SUBTYPE_MASK	0x0000000000FF0000
#define KVM_REG_RISCV_SUBTYPE_SHIFT	16

/* Config registers are mapped as type 1 */
#define KVM_REG_RISCV_CONFIG		(0x01 << KVM_REG_RISCV_TYPE_SHIFT)
#define KVM_REG_RISCV_CONFIG_REG(name)	\
	(offsetof(struct kvm_riscv_config, name) / sizeof(unsigned long))

/* Core registers are mapped as type 2 */
#define KVM_REG_RISCV_CORE		(0x02 << KVM_REG_RISCV_TYPE_SHIFT)
#define KVM_REG_RISCV_CORE_REG(name)	\
		(offsetof(struct kvm_riscv_core, name) / sizeof(unsigned long))

/* Control and status registers are mapped as type 3 */
#define KVM_REG_RISCV_CSR		(0x03 << KVM_REG_RISCV_TYPE_SHIFT)
#define KVM_REG_RISCV_CSR_GENERAL	(0x0 << KVM_REG_RISCV_SUBTYPE_SHIFT)
#define KVM_REG_RISCV_CSR_AIA		(0x1 << KVM_REG_RISCV_SUBTYPE_SHIFT)
#define KVM_REG_RISCV_CSR_REG(name)	\
		(offsetof(struct kvm_riscv_csr, name) / sizeof(unsigned long))
#define KVM_REG_RISCV_CSR_AIA_REG(name)	\
	(offsetof(struct kvm_riscv_aia_csr, name) / sizeof(unsigned long))

/* Timer registers are mapped as type 4 */
#define KVM_REG_RISCV_TIMER		(0x04 << KVM_REG_RISCV_TYPE_SHIFT)
#define KVM_REG_RISCV_TIMER_REG(name)	\
		(offsetof(struct kvm_riscv_timer, name) / sizeof(__u64))

/* F extension registers are mapped as type 5 */
#define KVM_REG_RISCV_FP_F		(0x05 << KVM_REG_RISCV_TYPE_SHIFT)
#define KVM_REG_RISCV_FP_F_REG(name)	\
		(offsetof(struct __riscv_f_ext_state, name) / sizeof(__u32))

/* D extension registers are mapped as type 6 */
#define KVM_REG_RISCV_FP_D		(0x06 << KVM_REG_RISCV_TYPE_SHIFT)
#define KVM_REG_RISCV_FP_D_REG(name)	\
		(offsetof(struct __riscv_d_ext_state, name) / sizeof(__u64))

/* ISA Extension registers are mapped as type 7 */
#define KVM_REG_RISCV_ISA_EXT		(0x07 << KVM_REG_RISCV_TYPE_SHIFT)

/* SBI extension registers are mapped as type 8 */
#define KVM_REG_RISCV_SBI_EXT		(0x08 << KVM_REG_RISCV_TYPE_SHIFT)
#define KVM_REG_RISCV_SBI_SINGLE	(0x0 << KVM_REG_RISCV_SUBTYPE_SHIFT)
#define KVM_REG_RISCV_SBI_MULTI_EN	(0x1 << KVM_REG_RISCV_SUBTYPE_SHIFT)
#define KVM_REG_RISCV_SBI_MULTI_DIS	(0x2 << KVM_REG_RISCV_SUBTYPE_SHIFT)
#define KVM_REG_RISCV_SBI_MULTI_REG(__ext_id)	\
		((__ext_id) / __BITS_PER_LONG)
#define KVM_REG_RISCV_SBI_MULTI_MASK(__ext_id)	\
		(1UL << ((__ext_id) % __BITS_PER_LONG))
#define KVM_REG_RISCV_SBI_MULTI_REG_LAST	\
		KVM_REG_RISCV_SBI_MULTI_REG(KVM_RISCV_SBI_EXT_MAX - 1)

#endif

#endif /* __LINUX_KVM_RISCV_H */
