/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef __UAPI_ASM_LOONGARCH_KVM_H
#define __UAPI_ASM_LOONGARCH_KVM_H

#include <linux/types.h>

/*
 * KVM Loongarch specific structures and definitions.
 */

#define __KVM_HAVE_READONLY_MEM

#define KVM_COALESCED_MMIO_PAGE_OFFSET 1

/*
 * for KVM_GET_REGS and KVM_SET_REGS
 */
struct kvm_regs {
	/* out (KVM_GET_REGS) / in (KVM_SET_REGS) */
	__u64 gpr[32];
	__u64 pc;
};

/*
 * for KVM_GET_FPU and KVM_SET_FPU
 */
struct kvm_fpu {
	__u32 fcsr;
	__u32 none;
	__u64 fcc;    /* 8x8 */
	struct kvm_fpureg {
		__u64 val64[4];
	} fpr[32];
};

/*
 * For LoongArch, we use KVM_SET_ONE_REG and KVM_GET_ONE_REG to access various
 * registers.  The id field is broken down as follows:
 *
 *  bits[63..52] - As per linux/kvm.h
 *  bits[51..32] - Must be zero.
 *  bits[31..16] - Register set.
 *
 * Register set = 0: GP registers from kvm_regs (see definitions below).
 *
 * Register set = 1: CSR registers.
 *
 * Register set = 2: KVM specific registers (see definitions below).
 *
 * Register set = 3: FPU / SIMD registers (see definitions below).
 *
 * Other sets registers may be added in the future.  Each set would
 * have its own identifier in bits[31..16].
 */

#define KVM_REG_LOONGARCH_GP		(KVM_REG_LOONGARCH | 0x00000ULL)
#define KVM_REG_LOONGARCH_CSR		(KVM_REG_LOONGARCH | 0x10000ULL)
#define KVM_REG_LOONGARCH_KVM		(KVM_REG_LOONGARCH | 0x20000ULL)
#define KVM_REG_LOONGARCH_FPU		(KVM_REG_LOONGARCH | 0x30000ULL)
#define KVM_REG_LOONGARCH_MASK		(KVM_REG_LOONGARCH | 0x30000ULL)
#define KVM_CSR_IDX_MASK		(0x10000 - 1)

/*
 * KVM_REG_LOONGARCH_KVM - KVM specific control registers.
 */

#define KVM_REG_LOONGARCH_COUNTER	(KVM_REG_LOONGARCH_KVM | KVM_REG_SIZE_U64 | 3)
#define KVM_REG_LOONGARCH_VCPU_RESET	(KVM_REG_LOONGARCH_KVM | KVM_REG_SIZE_U64 | 4)

struct kvm_debug_exit_arch {
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
};

/* definition of registers in kvm_run */
struct kvm_sync_regs {
};

/* dummy definition */
struct kvm_sregs {
};

struct kvm_loongarch_interrupt {
	/* in */
	__u32 cpu;
	__u32 irq;
};

#define KVM_NR_IRQCHIPS		1
#define KVM_IRQCHIP_NUM_PINS	64
#define KVM_MAX_CORES		256

#endif /* __UAPI_ASM_LOONGARCH_KVM_H */
