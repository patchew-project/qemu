/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015, Linaro Limited
 */
#ifndef __LINUX_ARM_SMCCC_H
#define __LINUX_ARM_SMCCC_H

#include <linux/const.h>

/*
 * This file provides common defines for ARM SMC Calling Convention as
 * specified in
 * https://developer.arm.com/docs/den0028/latest
 *
 * This code is up-to-date with version DEN 0028 C
 */

#define ARM_SMCCC_STD_CALL	        _AC(0,U)
#define ARM_SMCCC_FAST_CALL	        _AC(1,U)
#define ARM_SMCCC_TYPE_SHIFT		31

#define ARM_SMCCC_SMC_32		0
#define ARM_SMCCC_SMC_64		1
#define ARM_SMCCC_CALL_CONV_SHIFT	30

#define ARM_SMCCC_OWNER_MASK		0x3F
#define ARM_SMCCC_OWNER_SHIFT		24

#define ARM_SMCCC_FUNC_MASK		0xFFFF

#define ARM_SMCCC_IS_FAST_CALL(smc_val)	\
	((smc_val) & (ARM_SMCCC_FAST_CALL << ARM_SMCCC_TYPE_SHIFT))
#define ARM_SMCCC_IS_64(smc_val) \
	((smc_val) & (ARM_SMCCC_SMC_64 << ARM_SMCCC_CALL_CONV_SHIFT))
#define ARM_SMCCC_FUNC_NUM(smc_val)	((smc_val) & ARM_SMCCC_FUNC_MASK)
#define ARM_SMCCC_OWNER_NUM(smc_val) \
	(((smc_val) >> ARM_SMCCC_OWNER_SHIFT) & ARM_SMCCC_OWNER_MASK)

#define ARM_SMCCC_CALL_VAL(type, calling_convention, owner, func_num) \
	(((type) << ARM_SMCCC_TYPE_SHIFT) | \
	((calling_convention) << ARM_SMCCC_CALL_CONV_SHIFT) | \
	(((owner) & ARM_SMCCC_OWNER_MASK) << ARM_SMCCC_OWNER_SHIFT) | \
	((func_num) & ARM_SMCCC_FUNC_MASK))

#define ARM_SMCCC_OWNER_ARCH		0
#define ARM_SMCCC_OWNER_CPU		1
#define ARM_SMCCC_OWNER_SIP		2
#define ARM_SMCCC_OWNER_OEM		3
#define ARM_SMCCC_OWNER_STANDARD	4
#define ARM_SMCCC_OWNER_STANDARD_HYP	5
#define ARM_SMCCC_OWNER_VENDOR_HYP	6
#define ARM_SMCCC_OWNER_TRUSTED_APP	48
#define ARM_SMCCC_OWNER_TRUSTED_APP_END	49
#define ARM_SMCCC_OWNER_TRUSTED_OS	50
#define ARM_SMCCC_OWNER_TRUSTED_OS_END	63

#define ARM_SMCCC_FUNC_QUERY_CALL_UID  0xff01

#define ARM_SMCCC_QUIRK_NONE		0
#define ARM_SMCCC_QUIRK_QCOM_A6		1 /* Save/restore register a6 */

#define ARM_SMCCC_VERSION_1_0		0x10000
#define ARM_SMCCC_VERSION_1_1		0x10001
#define ARM_SMCCC_VERSION_1_2		0x10002
#define ARM_SMCCC_VERSION_1_3		0x10003

#define ARM_SMCCC_1_3_SVE_HINT		0x10000

#define ARM_SMCCC_VERSION_FUNC_ID					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 0)

#define ARM_SMCCC_ARCH_FEATURES_FUNC_ID					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 1)

#define ARM_SMCCC_ARCH_SOC_ID						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 2)

#define ARM_SMCCC_ARCH_WORKAROUND_1					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 0x8000)

#define ARM_SMCCC_ARCH_WORKAROUND_2					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 0x7fff)

#define ARM_SMCCC_ARCH_WORKAROUND_3					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   0, 0x3fff)

#define ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID				\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_FUNC_QUERY_CALL_UID)

/* KVM UID value: 28b46fb6-2ec5-11e9-a9ca-4b564d003a74 */
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_0	0xb66fb428U
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_1	0xe911c52eU
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_2	0x564bcaa9U
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_3	0x743a004dU

/* KVM "vendor specific" services */
#define ARM_SMCCC_KVM_FUNC_FEATURES		0
#define ARM_SMCCC_KVM_FUNC_PTP			1
#define ARM_SMCCC_KVM_FUNC_FEATURES_2		127
#define ARM_SMCCC_KVM_NUM_FUNCS			128

#define ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID			\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_FEATURES)

#define SMCCC_ARCH_WORKAROUND_RET_UNAFFECTED	1

/*
 * ptp_kvm is a feature used for time sync between vm and host.
 * ptp_kvm module in guest kernel will get service from host using
 * this hypercall ID.
 */
#define ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID				\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_32,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_PTP)

/* ptp_kvm counter type ID */
#define KVM_PTP_VIRT_COUNTER			0
#define KVM_PTP_PHYS_COUNTER			1

/* Paravirtualised time calls (defined by ARM DEN0057A) */
#define ARM_SMCCC_HV_PV_TIME_FEATURES				\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_64,			\
			   ARM_SMCCC_OWNER_STANDARD_HYP,	\
			   0x20)

#define ARM_SMCCC_HV_PV_TIME_ST					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_64,			\
			   ARM_SMCCC_OWNER_STANDARD_HYP,	\
			   0x21)

/* TRNG entropy source calls (defined by ARM DEN0098) */
#define ARM_SMCCC_TRNG_VERSION					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_32,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x50)

#define ARM_SMCCC_TRNG_FEATURES					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_32,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x51)

#define ARM_SMCCC_TRNG_GET_UUID					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_32,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x52)

#define ARM_SMCCC_TRNG_RND32					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_32,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x53)

#define ARM_SMCCC_TRNG_RND64					\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,			\
			   ARM_SMCCC_SMC_64,			\
			   ARM_SMCCC_OWNER_STANDARD,		\
			   0x53)

/*
 * Return codes defined in ARM DEN 0070A
 * ARM DEN 0070A is now merged/consolidated into ARM DEN 0028 C
 */
#define SMCCC_RET_SUCCESS			0
#define SMCCC_RET_NOT_SUPPORTED			-1
#define SMCCC_RET_NOT_REQUIRED			-2
#define SMCCC_RET_INVALID_PARAMETER		-3

#ifndef __ASSEMBLY__

#include <linux/types.h>

enum arm_smccc_conduit {
	SMCCC_CONDUIT_NONE,
	SMCCC_CONDUIT_SMC,
	SMCCC_CONDUIT_HVC,
};

/**
 * struct arm_smccc_res - Result from SMC/HVC call
 * @a0-a3 result values from registers 0 to 3
 */
struct arm_smccc_res {
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
};

/**
 * struct arm_smccc_1_2_regs - Arguments for or Results from SMC/HVC call
 * @a0-a17 argument values from registers 0 to 17
 */
struct arm_smccc_1_2_regs {
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
	unsigned long a4;
	unsigned long a5;
	unsigned long a6;
	unsigned long a7;
	unsigned long a8;
	unsigned long a9;
	unsigned long a10;
	unsigned long a11;
	unsigned long a12;
	unsigned long a13;
	unsigned long a14;
	unsigned long a15;
	unsigned long a16;
	unsigned long a17;
};

#endif /*__ASSEMBLY__*/
#endif /*__LINUX_ARM_SMCCC_H*/
