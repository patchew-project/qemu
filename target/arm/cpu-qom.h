/*
 * QEMU ARM CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */
#ifndef QEMU_ARM_CPU_QOM_H
#define QEMU_ARM_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

struct arm_boot_info;

#define TYPE_ARM_CPU "arm-cpu"
#define TYPE_ARM_V7M_CPU "arm-v7m-cpu"
#define TYPE_AARCH64_CPU "aarch64-cpu"

OBJECT_DECLARE_CPU_TYPE(ARMCPU, ARMCPUClass, ARM_CPU)

#define TYPE_ARM_MAX_CPU "max-" TYPE_ARM_CPU

typedef struct ARMCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
    void (*class_init)(ARMCPUClass *acc);
} ARMCPUInfo;

void arm_cpu_register_parent(const ARMCPUInfo *info, const char *parent);

static inline void arm_cpu_register(const ARMCPUInfo *info)
{
    arm_cpu_register_parent(info, TYPE_ARM_CPU);
}

static inline void arm_v7m_cpu_register(const ARMCPUInfo *info)
{
    arm_cpu_register_parent(info, TYPE_ARM_V7M_CPU);
}

static inline void aarch64_cpu_register(const ARMCPUInfo *info)
{
    arm_cpu_register_parent(info, TYPE_AARCH64_CPU);
}

typedef struct ARMISARegisters {
    uint32_t id_isar0;
    uint32_t id_isar1;
    uint32_t id_isar2;
    uint32_t id_isar3;
    uint32_t id_isar4;
    uint32_t id_isar5;
    uint32_t id_isar6;
    uint32_t id_mmfr0;
    uint32_t id_mmfr1;
    uint32_t id_mmfr2;
    uint32_t id_mmfr3;
    uint32_t id_mmfr4;
    uint32_t id_mmfr5;
    uint32_t id_pfr0;
    uint32_t id_pfr1;
    uint32_t id_pfr2;
    uint32_t mvfr0;
    uint32_t mvfr1;
    uint32_t mvfr2;
    uint32_t id_dfr0;
    uint32_t id_dfr1;
    uint32_t dbgdidr;
    uint32_t dbgdevid;
    uint32_t dbgdevid1;
    uint64_t id_aa64isar0;
    uint64_t id_aa64isar1;
    uint64_t id_aa64pfr0;
    uint64_t id_aa64pfr1;
    uint64_t id_aa64mmfr0;
    uint64_t id_aa64mmfr1;
    uint64_t id_aa64mmfr2;
    uint64_t id_aa64dfr0;
    uint64_t id_aa64dfr1;
    uint64_t id_aa64zfr0;
    uint64_t id_aa64smfr0;
    uint64_t reset_pmcr_el0;
} ARMISARegisters;

/**
 * ARMCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * An ARM CPU model.
 */
struct ARMCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    const ARMCPUInfo *info;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;

    /* Coprocessor information */
    GHashTable *cp_regs;

    /* 'compatible' string for this CPU for Linux device trees */
    const char *dtb_compatible;

    /* Internal CPU feature flags.  */
    uint64_t features;

    /*
     * The instance init functions for implementation-specific subclasses
     * set these fields to specify the implementation-dependent values of
     * various constant registers and reset values of non-constant
     * registers.
     * Some of these might become QOM properties eventually.
     * Field names match the official register names as defined in the
     * ARMv7AR ARM Architecture Reference Manual. A reset_ prefix
     * is used for reset values of non-constant registers; no reset_
     * prefix means a constant register.
     * Some of these registers are split out into a substructure that
     * is shared with the translators to control the ISA.
     *
     * Note that if you add an ID register to the ARMISARegisters struct
     * you need to also update the 32-bit and 64-bit versions of the
     * kvm_arm_get_host_cpu_features() function to correctly populate the
     * field by reading the value from the KVM vCPU.
     */
    ARMISARegisters isar;

    uint64_t midr;
    uint64_t ctr;
    uint64_t pmceid0;
    uint64_t pmceid1;
    uint64_t id_aa64afr0;
    uint64_t id_aa64afr1;
    uint64_t clidr;
    /*
     * The elements of this array are the CCSIDR values for each cache,
     * in the order L1DCache, L1ICache, L2DCache, L2ICache, etc.
     */
    uint64_t ccsidr[16];

    uint32_t revidr;
    uint32_t id_afr0;
    uint32_t reset_fpsid;
    uint32_t reset_sctlr;
    uint32_t reset_auxcr;

    /* PMSAv7 MPU number of supported regions */
    uint32_t pmsav7_dregion;
    /* v8M SAU number of supported regions */
    uint32_t sau_sregion;

    /* DCZ blocksize, in log_2(words), ie low 4 bits of DCZID_EL0 */
    uint32_t dcz_blocksize;

    /* Configurable aspects of GIC cpu interface (which is part of the CPU) */
    int gic_num_lrs; /* number of list registers */
    int gic_vpribits; /* number of virtual priority bits */
    int gic_vprebits; /* number of virtual preemption bits */
    int gic_pribits; /* number of physical priority bits */

    /*
     * [QEMU_]KVM_ARM_TARGET_* constant for this CPU, or
     * QEMU_KVM_ARM_TARGET_NONE if the kernel doesn't support this CPU type.
     */
    uint32_t kvm_target;
};

static inline int arm_class_feature(ARMCPUClass *acc, int feature)
{
    return (acc->features & (1ULL << feature)) != 0;
}

static inline void set_class_feature(ARMCPUClass *acc, int feature)
{
    acc->features |= 1ULL << feature;
}

static inline void unset_class_feature(ARMCPUClass *acc, int feature)
{
    acc->features &= ~(1ULL << feature);
}

void register_cp_regs_for_features(ARMCPU *cpu);
void init_cpreg_list(ARMCPU *cpu);

/* Callback functions for the generic timer's timers. */
void arm_gt_ptimer_cb(void *opaque);
void arm_gt_vtimer_cb(void *opaque);
void arm_gt_htimer_cb(void *opaque);
void arm_gt_stimer_cb(void *opaque);
void arm_gt_hvtimer_cb(void *opaque);

#define ARM_AFF0_SHIFT 0
#define ARM_AFF0_MASK  (0xFFULL << ARM_AFF0_SHIFT)
#define ARM_AFF1_SHIFT 8
#define ARM_AFF1_MASK  (0xFFULL << ARM_AFF1_SHIFT)
#define ARM_AFF2_SHIFT 16
#define ARM_AFF2_MASK  (0xFFULL << ARM_AFF2_SHIFT)
#define ARM_AFF3_SHIFT 32
#define ARM_AFF3_MASK  (0xFFULL << ARM_AFF3_SHIFT)
#define ARM_DEFAULT_CPUS_PER_CLUSTER 8

#define ARM32_AFFINITY_MASK (ARM_AFF0_MASK|ARM_AFF1_MASK|ARM_AFF2_MASK)
#define ARM64_AFFINITY_MASK \
    (ARM_AFF0_MASK|ARM_AFF1_MASK|ARM_AFF2_MASK|ARM_AFF3_MASK)
#define ARM64_AFFINITY_INVALID (~ARM64_AFFINITY_MASK)

#endif
