/*
 * QEMU AArch64 CPU
 *
 * Copyright (c) 2013 Linaro Ltd
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu/module.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "qapi/visitor.h"

static inline void set_feature(CPUARMState *env, int feature)
{
    env->features |= 1ULL << feature;
}

static inline void unset_feature(CPUARMState *env, int feature)
{
    env->features &= ~(1ULL << feature);
}

#ifndef CONFIG_USER_ONLY
static uint64_t a57_a53_l2ctlr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);

    /* Number of cores is in [25:24]; otherwise we RAZ */
    return (cpu->core_count - 1) << 24;
}
#endif

static const ARMCPRegInfo cortex_a72_a57_a53_cp_reginfo[] = {
#ifndef CONFIG_USER_ONLY
    { .name = "L2CTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = a57_a53_l2ctlr_read,
      .writefn = arm_cp_write_ignore },
    { .name = "L2CTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = a57_a53_l2ctlr_read,
      .writefn = arm_cp_write_ignore },
#endif
    { .name = "L2ECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ECTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ACTLR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR",
      .cp = 15, .opc1 = 0, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUECTLR",
      .cp = 15, .opc1 = 1, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUMERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUMERRSR",
      .cp = 15, .opc1 = 2, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "L2MERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2MERRSR",
      .cp = 15, .opc1 = 3, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static void aarch64_a57_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a57";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_VFP4);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A57;
    cpu->midr = 0x411fd070;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    cpu->id_pfr0 = 0x00000131;
    cpu->id_pfr1 = 0x00011011;
    cpu->id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->id_mmfr0 = 0x10101105;
    cpu->id_mmfr1 = 0x40000000;
    cpu->id_mmfr2 = 0x01260000;
    cpu->id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_isar6 = 0;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001124;
    cpu->dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x70ffe07a; /* 2048KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);
}

static void aarch64_a53_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a53";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_VFP4);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A53;
    cpu->midr = 0x410fd034;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x84448004; /* L1Ip = VIPT */
    cpu->reset_sctlr = 0x00c50838;
    cpu->id_pfr0 = 0x00000131;
    cpu->id_pfr1 = 0x00011011;
    cpu->id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->id_mmfr0 = 0x10101105;
    cpu->id_mmfr1 = 0x40000000;
    cpu->id_mmfr2 = 0x01260000;
    cpu->id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_isar6 = 0;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001122; /* 40 bit physical addr */
    cpu->dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x700fe01a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32KB L1 icache */
    cpu->ccsidr[2] = 0x707fe07a; /* 1024KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);
}

static void aarch64_a72_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a72";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_VFP4);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x410fd083;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034080;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    cpu->id_pfr0 = 0x00000131;
    cpu->id_pfr1 = 0x00011011;
    cpu->id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->id_mmfr0 = 0x10201105;
    cpu->id_mmfr1 = 0x40000000;
    cpu->id_mmfr2 = 0x01260000;
    cpu->id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001124;
    cpu->dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x707fe07a; /* 1MB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);
}

/*
 * While we eventually use cpu->sve_vq_map as a typical bitmap, where each vq
 * has only two states (off/on), until we've finalized the map at realize time
 * we use an extra bit, at the vq - 1 + ARM_MAX_VQ bit number, to also allow
 * tracking of the uninitialized state. The arm_vq_state typedef and following
 * functions allow us to more easily work with the bitmap. Also, while the map
 * is still initializing, sve-max-vq has an additional three states, bringing
 * the number of its states to five, which are the following:
 *
 * sve-max-vq:
 *   0:    SVE is disabled. The default value for a vq in the map is 'OFF'.
 *  -1:    SVE is enabled, but neither sve-max-vq nor sve<vl-bits> properties
 *         have yet been specified by the user. The default value for a vq in
 *         the map is 'ON'.
 *  -2:    SVE is enabled and one or more sve<vl-bits> properties have been
 *         set to 'OFF' by the user, but no sve<vl-bits> properties have yet
 *         been set to 'ON'. The user is now blocked from setting sve-max-vq
 *         and the default value for a vq in the map is 'ON'.
 *  -3:    SVE is enabled and one or more sve<vl-bits> properties have been
 *         set to 'ON' by the user. The user is blocked from setting sve-max-vq
 *         and the default value for a vq in the map is 'OFF'. sve-max-vq never
 *         transitions back to -2, even if later inputs disable the vector
 *         lengths that initially transitioned sve-max-vq to this state. This
 *         avoids the default values from flip-flopping.
 *  [1-ARM_MAX_VQ]: SVE is enabled and the user has specified a valid
 *                  sve-max-vq. The sve-max-vq specified vq and all smaller
 *                  vq's will be initially enabled. All larger vq's will have
 *                  a default of 'OFF'.
 */
#define ARM_SVE_INIT          -1
#define ARM_VQ_DEFAULT_ON     -2
#define ARM_VQ_DEFAULT_OFF    -3

#define arm_sve_have_max_vq(cpu) ((int32_t)(cpu)->sve_max_vq > 0)

typedef enum arm_vq_state {
    ARM_VQ_OFF,
    ARM_VQ_ON,
    ARM_VQ_UNINITIALIZED,
} arm_vq_state;

static arm_vq_state arm_cpu_vq_map_get(ARMCPU *cpu, int vq)
{
    assert(vq <= ARM_MAX_VQ);

    return test_bit(vq - 1, cpu->sve_vq_map) |
           test_bit(vq - 1 + ARM_MAX_VQ, cpu->sve_vq_map) << 1;
}

static void arm_cpu_vq_map_set(ARMCPU *cpu, int vq, arm_vq_state state)
{
    assert(state == ARM_VQ_OFF || state == ARM_VQ_ON);
    assert(vq <= ARM_MAX_VQ);

    clear_bit(vq - 1 + ARM_MAX_VQ, cpu->sve_vq_map);

    if (state == ARM_VQ_ON) {
        set_bit(vq - 1, cpu->sve_vq_map);
    } else {
        clear_bit(vq - 1, cpu->sve_vq_map);
    }
}

static void arm_cpu_vq_map_init(ARMCPU *cpu)
{
    bitmap_zero(cpu->sve_vq_map, ARM_MAX_VQ * 2);
    bitmap_set(cpu->sve_vq_map, ARM_MAX_VQ, ARM_MAX_VQ);
}

static bool arm_cpu_vq_map_is_finalized(ARMCPU *cpu)
{
    DECLARE_BITMAP(map, ARM_MAX_VQ * 2);

    bitmap_zero(map, ARM_MAX_VQ * 2);
    bitmap_set(map, ARM_MAX_VQ, ARM_MAX_VQ);
    bitmap_and(map, map, cpu->sve_vq_map, ARM_MAX_VQ * 2);

    return bitmap_empty(map, ARM_MAX_VQ * 2);
}

static void arm_cpu_vq_map_finalize(ARMCPU *cpu)
{
    Error *err = NULL;
    char name[8];
    uint32_t vq;
    bool value;

    /*
     * We use the property get accessor because it knows what default
     * values to return for uninitialized vector lengths.
     */
    for (vq = 1; vq <= ARM_MAX_VQ; ++vq) {
        sprintf(name, "sve%d", vq * 128);
        value = object_property_get_bool(OBJECT(cpu), name, &err);
        assert(!err);
        if (value) {
            arm_cpu_vq_map_set(cpu, vq, ARM_VQ_ON);
        } else {
            arm_cpu_vq_map_set(cpu, vq, ARM_VQ_OFF);
        }
    }

    assert(arm_cpu_vq_map_is_finalized(cpu));
}

void arm_cpu_sve_finalize(ARMCPU *cpu, Error **errp)
{
    Error *err = NULL;

    if (!cpu->sve_max_vq) {
        bitmap_zero(cpu->sve_vq_map, ARM_MAX_VQ * 2);
        return;
    }

    /* sve-max-vq and sve<vl-bits> properties not yet implemented for KVM */
    if (kvm_enabled()) {
        return;
    }

    if (cpu->sve_max_vq == ARM_SVE_INIT) {
        object_property_set_uint(OBJECT(cpu), ARM_MAX_VQ, "sve-max-vq", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        assert(cpu->sve_max_vq == ARM_MAX_VQ);
        arm_cpu_vq_map_finalize(cpu);
    } else {
        arm_cpu_vq_map_finalize(cpu);
        if (!arm_sve_have_max_vq(cpu)) {
            cpu->sve_max_vq = arm_cpu_vq_map_next_smaller(cpu, ARM_MAX_VQ + 1);
        }
    }

    assert(cpu->sve_max_vq == arm_cpu_vq_map_next_smaller(cpu, ARM_MAX_VQ + 1));
}

uint32_t arm_cpu_vq_map_next_smaller(ARMCPU *cpu, uint32_t vq)
{
    uint32_t bitnum;

    assert(vq <= ARM_MAX_VQ + 1);
    assert(arm_cpu_vq_map_is_finalized(cpu));

    bitnum = find_last_bit(cpu->sve_vq_map, vq - 1);
    return bitnum == vq - 1 ? 0 : bitnum + 1;
}

static void cpu_max_get_sve_max_vq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    visit_type_uint32(v, name, &cpu->sve_max_vq, errp);
}

static void cpu_max_set_sve_max_vq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    Error *err = NULL;
    uint32_t value;

    visit_type_uint32(v, name, &value, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (!cpu->sve_max_vq) {
        error_setg(errp, "cannot set sve-max-vq");
        error_append_hint(errp, "SVE has been disabled with sve=off\n");
        return;
    }

    /*
     * It gets complicated trying to support both sve-max-vq and
     * sve<vl-bits> properties together, so we mostly don't. We
     * do allow both if sve-max-vq is specified first and only once
     * though.
     */
    if (cpu->sve_max_vq != ARM_SVE_INIT) {
        error_setg(errp, "sve<vl-bits> in use or sve-max-vq already "
                   "specified");
        error_append_hint(errp, "sve-max-vq must come before all "
                          "sve<vl-bits> properties and it must only "
                          "be specified once.\n");
        return;
    }

    cpu->sve_max_vq = value;

    if (cpu->sve_max_vq == 0 || cpu->sve_max_vq > ARM_MAX_VQ) {
        error_setg(errp, "unsupported SVE vector length");
        error_append_hint(errp, "Valid sve-max-vq in range [1-%d]\n",
                          ARM_MAX_VQ);
    } else {
        uint32_t vq;

        for (vq = 1; vq <= cpu->sve_max_vq; ++vq) {
            char name[8];
            sprintf(name, "sve%d", vq * 128);
            object_property_set_bool(obj, true, name, &err);
            if (err) {
                error_propagate(errp, err);
                return;
            }
        }
    }
}

static void cpu_arm_get_sve_vq(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    int vq = atoi(&name[3]) / 128;
    arm_vq_state vq_state;
    bool value;

    vq_state = arm_cpu_vq_map_get(cpu, vq);

    if (!cpu->sve_max_vq) {
        /* All vector lengths are disabled when SVE is off. */
        value = false;
    } else if (vq_state == ARM_VQ_ON) {
        value = true;
    } else if (vq_state == ARM_VQ_OFF) {
        value = false;
    } else {
        /*
         * vq is uninitialized. We pick a default here based on the
         * the state of sve-max-vq and other sve<vl-bits> properties.
         */
        if (arm_sve_have_max_vq(cpu)) {
            /*
             * If we have sve-max-vq, then all remaining uninitialized
             * vq's are 'OFF'.
             */
            value = false;
        } else {
            switch (cpu->sve_max_vq) {
            case ARM_SVE_INIT:
            case ARM_VQ_DEFAULT_ON:
                value = true;
                break;
            case ARM_VQ_DEFAULT_OFF:
                value = false;
                break;
            }
        }
    }

    visit_type_bool(v, name, &value, errp);
}

static void cpu_arm_set_sve_vq(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    int vq = atoi(&name[3]) / 128;
    arm_vq_state vq_state;
    Error *err = NULL;
    uint32_t max_vq = 0;
    bool value;

    visit_type_bool(v, name, &value, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (value && !cpu->sve_max_vq) {
        error_setg(errp, "cannot enable %s", name);
        error_append_hint(errp, "SVE has been disabled with sve=off\n");
        return;
    } else if (!cpu->sve_max_vq) {
        /*
         * We don't complain about disabling vector lengths when SVE
         * is off, but we don't do anything either.
         */
        return;
    }

    if (arm_sve_have_max_vq(cpu)) {
        max_vq = cpu->sve_max_vq;
    } else {
        if (value) {
            cpu->sve_max_vq = ARM_VQ_DEFAULT_OFF;
        } else if (cpu->sve_max_vq != ARM_VQ_DEFAULT_OFF) {
            cpu->sve_max_vq = ARM_VQ_DEFAULT_ON;
        }
    }

    /*
     * We need to know the maximum vector length, which may just currently
     * be the maximum length, in order to validate the enabling/disabling
     * of this vector length. We use the property get accessor in order to
     * get the appropriate default value for any uninitialized lengths.
     */
    if (!max_vq) {
        char tmp[8];
        bool s;

        for (max_vq = ARM_MAX_VQ; max_vq >= 1; --max_vq) {
            sprintf(tmp, "sve%d", max_vq * 128);
            s = object_property_get_bool(OBJECT(cpu), tmp, &err);
            assert(!err);
            if (s) {
                break;
            }
        }
    }

    if (arm_sve_have_max_vq(cpu) && value && vq > cpu->sve_max_vq) {
        error_setg(errp, "cannot enable %s", name);
        error_append_hint(errp, "vq=%d (%d bits) is larger than the "
                          "maximum vector length, sve-max-vq=%d "
                          "(%d bits)\n", vq, vq * 128,
                          cpu->sve_max_vq, cpu->sve_max_vq * 128);
    } else if (arm_sve_have_max_vq(cpu) && !value && vq == cpu->sve_max_vq) {
        error_setg(errp, "cannot disable %s", name);
        error_append_hint(errp, "The maximum vector length must be "
                          "enabled, sve-max-vq=%d (%d bits)\n",
                          cpu->sve_max_vq, cpu->sve_max_vq * 128);
    } else if (arm_sve_have_max_vq(cpu) && !value && vq < cpu->sve_max_vq &&
               is_power_of_2(vq)) {
        error_setg(errp, "cannot disable %s", name);
        error_append_hint(errp, "vq=%d (%d bits) is required as it is a "
                          "power-of-2 length smaller than the maximum, "
                          "sve-max-vq=%d (%d bits)\n", vq, vq * 128,
                          cpu->sve_max_vq, cpu->sve_max_vq * 128);
    } else if (!value && vq < max_vq && is_power_of_2(vq)) {
        error_setg(errp, "cannot disable %s", name);
        error_append_hint(errp, "Vector length %d-bits is required as it "
                          "is a power-of-2 length smaller than another "
                          "enabled vector length. Disable all larger vector "
                          "lengths first.\n", vq * 128);
    } else {
        if (value) {
            bool fail = false;
            uint32_t s;

            /*
             * Enabling a vector length automatically enables all
             * uninitialized power-of-2 lengths smaller than it, as
             * per the architecture.
             */
            for (s = 1; s < vq; ++s) {
                if (is_power_of_2(s)) {
                    vq_state = arm_cpu_vq_map_get(cpu, s);
                    if (vq_state == ARM_VQ_UNINITIALIZED) {
                        arm_cpu_vq_map_set(cpu, s, ARM_VQ_ON);
                    } else if (vq_state == ARM_VQ_OFF) {
                        fail = true;
                        break;
                    }
                }
            }

            if (fail) {
                error_setg(errp, "cannot enable %s", name);
                error_append_hint(errp, "Vector length %d-bits is disabled "
                                  "and is a power-of-2 length smaller than "
                                  "%s. All power-of-2 vector lengths smaller "
                                  "than the maximum length are required.\n",
                                  s * 128, name);
            } else {
                arm_cpu_vq_map_set(cpu, vq, ARM_VQ_ON);
            }
        } else {
            arm_cpu_vq_map_set(cpu, vq, ARM_VQ_OFF);
        }
    }
}

static void cpu_arm_get_sve(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    bool value = !!cpu->sve_max_vq;

    if (kvm_enabled() && !kvm_arm_sve_supported(CPU(cpu))) {
        value = false;
    }

    visit_type_bool(v, name, &value, errp);
}

static void cpu_arm_set_sve(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    Error *err = NULL;
    bool value;

    visit_type_bool(v, name, &value, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (value) {
        if (kvm_enabled() && !kvm_arm_sve_supported(CPU(cpu))) {
            error_setg(errp, "'sve' feature not supported by KVM on this host");
            return;
        }

        /*
         * We handle the -cpu <cpu>,sve=off,sve=on case by reinitializing,
         * but otherwise we don't do anything as an sve=on could come after
         * a sve-max-vq or sve<vl-bits> setting.
         */
        if (!cpu->sve_max_vq) {
            cpu->sve_max_vq = ARM_SVE_INIT;
            arm_cpu_vq_map_init(cpu);
        }
    } else {
        cpu->sve_max_vq = 0;
    }
}

/* -cpu max: if KVM is enabled, like -cpu host (best possible with this host);
 * otherwise, a CPU with as many features enabled as our emulation supports.
 * The version of '-cpu max' for qemu-system-arm is defined in cpu.c;
 * this only needs to handle 64 bits.
 */
static void aarch64_max_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t vq;

    if (kvm_enabled()) {
        kvm_arm_set_cpu_features_from_host(cpu);
        /*
         * KVM doesn't yet support the sve-max-vq property, but
         * setting cpu->sve_max_vq is also used to turn SVE on.
         */
        cpu->sve_max_vq = ARM_SVE_INIT;
    } else {
        uint64_t t;
        uint32_t u;
        aarch64_a57_initfn(obj);

        t = cpu->isar.id_aa64isar0;
        t = FIELD_DP64(t, ID_AA64ISAR0, AES, 2); /* AES + PMULL */
        t = FIELD_DP64(t, ID_AA64ISAR0, SHA1, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SHA2, 2); /* SHA512 */
        t = FIELD_DP64(t, ID_AA64ISAR0, CRC32, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, ATOMIC, 2);
        t = FIELD_DP64(t, ID_AA64ISAR0, RDM, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SHA3, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SM3, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SM4, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, DP, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, FHM, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, TS, 2); /* v8.5-CondM */
        t = FIELD_DP64(t, ID_AA64ISAR0, RNDR, 1);
        cpu->isar.id_aa64isar0 = t;

        t = cpu->isar.id_aa64isar1;
        t = FIELD_DP64(t, ID_AA64ISAR1, JSCVT, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, FCMA, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, APA, 1); /* PAuth, architected only */
        t = FIELD_DP64(t, ID_AA64ISAR1, API, 0);
        t = FIELD_DP64(t, ID_AA64ISAR1, GPA, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, GPI, 0);
        t = FIELD_DP64(t, ID_AA64ISAR1, SB, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, SPECRES, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, FRINTTS, 1);
        cpu->isar.id_aa64isar1 = t;

        t = cpu->isar.id_aa64pfr0;
        t = FIELD_DP64(t, ID_AA64PFR0, SVE, 1);
        t = FIELD_DP64(t, ID_AA64PFR0, FP, 1);
        t = FIELD_DP64(t, ID_AA64PFR0, ADVSIMD, 1);
        cpu->isar.id_aa64pfr0 = t;

        t = cpu->isar.id_aa64pfr1;
        t = FIELD_DP64(t, ID_AA64PFR1, BT, 1);
        cpu->isar.id_aa64pfr1 = t;

        t = cpu->isar.id_aa64mmfr1;
        t = FIELD_DP64(t, ID_AA64MMFR1, HPDS, 1); /* HPD */
        t = FIELD_DP64(t, ID_AA64MMFR1, LO, 1);
        cpu->isar.id_aa64mmfr1 = t;

        /* Replicate the same data to the 32-bit id registers.  */
        u = cpu->isar.id_isar5;
        u = FIELD_DP32(u, ID_ISAR5, AES, 2); /* AES + PMULL */
        u = FIELD_DP32(u, ID_ISAR5, SHA1, 1);
        u = FIELD_DP32(u, ID_ISAR5, SHA2, 1);
        u = FIELD_DP32(u, ID_ISAR5, CRC32, 1);
        u = FIELD_DP32(u, ID_ISAR5, RDM, 1);
        u = FIELD_DP32(u, ID_ISAR5, VCMA, 1);
        cpu->isar.id_isar5 = u;

        u = cpu->isar.id_isar6;
        u = FIELD_DP32(u, ID_ISAR6, JSCVT, 1);
        u = FIELD_DP32(u, ID_ISAR6, DP, 1);
        u = FIELD_DP32(u, ID_ISAR6, FHM, 1);
        u = FIELD_DP32(u, ID_ISAR6, SB, 1);
        u = FIELD_DP32(u, ID_ISAR6, SPECRES, 1);
        cpu->isar.id_isar6 = u;

        /*
         * FIXME: We do not yet support ARMv8.2-fp16 for AArch32 yet,
         * so do not set MVFR1.FPHP.  Strictly speaking this is not legal,
         * but it is also not legal to enable SVE without support for FP16,
         * and enabling SVE in system mode is more useful in the short term.
         */

#ifdef CONFIG_USER_ONLY
        /* For usermode -cpu max we can use a larger and more efficient DCZ
         * blocksize since we don't have to follow what the hardware does.
         */
        cpu->ctr = 0x80038003; /* 32 byte I and D cacheline size, VIPT icache */
        cpu->dcz_blocksize = 7; /*  512 bytes */
#endif

        /*
         * sve_max_vq is initially unspecified, but must be initialized to a
         * non-zero value (ARM_SVE_INIT) to indicate that this cpu type has
         * SVE. It will be finalized in arm_cpu_realizefn().
         */
        cpu->sve_max_vq = ARM_SVE_INIT;
        object_property_add(obj, "sve-max-vq", "uint32", cpu_max_get_sve_max_vq,
                            cpu_max_set_sve_max_vq, NULL, NULL, &error_fatal);

        /*
         * sve_vq_map uses a special state while setting properties, so
         * we initialize it here with its init function and finalize it
         * in arm_cpu_realizefn().
         */
        arm_cpu_vq_map_init(cpu);
        for (vq = 1; vq <= ARM_MAX_VQ; ++vq) {
            char name[8];
            sprintf(name, "sve%d", vq * 128);
            object_property_add(obj, name, "bool", cpu_arm_get_sve_vq,
                                cpu_arm_set_sve_vq, NULL, NULL, &error_fatal);
        }
    }

    object_property_add(obj, "sve", "bool", cpu_arm_get_sve,
                        cpu_arm_set_sve, NULL, NULL, &error_fatal);
}

struct ARMCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
    void (*class_init)(ObjectClass *oc, void *data);
};

static const ARMCPUInfo aarch64_cpus[] = {
    { .name = "cortex-a57",         .initfn = aarch64_a57_initfn },
    { .name = "cortex-a53",         .initfn = aarch64_a53_initfn },
    { .name = "cortex-a72",         .initfn = aarch64_a72_initfn },
    { .name = "max",                .initfn = aarch64_max_initfn },
    { .name = NULL }
};

static bool aarch64_cpu_get_aarch64(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    return arm_feature(&cpu->env, ARM_FEATURE_AARCH64);
}

static void aarch64_cpu_set_aarch64(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    /* At this time, this property is only allowed if KVM is enabled.  This
     * restriction allows us to avoid fixing up functionality that assumes a
     * uniform execution state like do_interrupt.
     */
    if (value == false) {
        if (!kvm_enabled() || !kvm_arm_aarch32_supported(CPU(cpu))) {
            error_setg(errp, "'aarch64' feature cannot be disabled "
                             "unless KVM is enabled and 32-bit EL1 "
                             "is supported");
            return;
        }
        unset_feature(&cpu->env, ARM_FEATURE_AARCH64);
    } else {
        set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    }
}

static void aarch64_cpu_initfn(Object *obj)
{
    object_property_add_bool(obj, "aarch64", aarch64_cpu_get_aarch64,
                             aarch64_cpu_set_aarch64, NULL);
    object_property_set_description(obj, "aarch64",
                                    "Set on/off to enable/disable aarch64 "
                                    "execution state ",
                                    NULL);
}

static void aarch64_cpu_finalizefn(Object *obj)
{
}

static gchar *aarch64_gdb_arch_name(CPUState *cs)
{
    return g_strdup("aarch64");
}

static void aarch64_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);

    cc->cpu_exec_interrupt = arm_cpu_exec_interrupt;
    cc->gdb_read_register = aarch64_cpu_gdb_read_register;
    cc->gdb_write_register = aarch64_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 34;
    cc->gdb_core_xml_file = "aarch64-core.xml";
    cc->gdb_arch_name = aarch64_gdb_arch_name;
}

static void aarch64_cpu_instance_init(Object *obj)
{
    ARMCPUClass *acc = ARM_CPU_GET_CLASS(obj);

    acc->info->initfn(obj);
    arm_cpu_post_init(obj);
}

static void cpu_register_class_init(ObjectClass *oc, void *data)
{
    ARMCPUClass *acc = ARM_CPU_CLASS(oc);

    acc->info = data;
}

static void aarch64_cpu_register(const ARMCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_AARCH64_CPU,
        .instance_size = sizeof(ARMCPU),
        .instance_init = aarch64_cpu_instance_init,
        .class_size = sizeof(ARMCPUClass),
        .class_init = info->class_init ?: cpu_register_class_init,
        .class_data = (void *)info,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_ARM_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo aarch64_cpu_type_info = {
    .name = TYPE_AARCH64_CPU,
    .parent = TYPE_ARM_CPU,
    .instance_size = sizeof(ARMCPU),
    .instance_init = aarch64_cpu_initfn,
    .instance_finalize = aarch64_cpu_finalizefn,
    .abstract = true,
    .class_size = sizeof(AArch64CPUClass),
    .class_init = aarch64_cpu_class_init,
};

static void aarch64_cpu_register_types(void)
{
    const ARMCPUInfo *info = aarch64_cpus;

    type_register_static(&aarch64_cpu_type_info);

    while (info->name) {
        aarch64_cpu_register(info);
        info++;
    }
}

type_init(aarch64_cpu_register_types)
