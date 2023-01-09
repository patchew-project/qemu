/*
 * ARM generic helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "cpu.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "qemu/crc32c.h"
#include "qemu/qemu-print.h"
#include "exec/exec-all.h"
#include <zlib.h> /* For crc32 */
#include "hw/irq.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/kvm.h"
#include "sysemu/tcg.h"
#include "qapi/qapi-commands-machine-target.h"
#include "qapi/error.h"
#include "qemu/guest-random.h"
#ifdef CONFIG_TCG
#include "semihosting/common-semi.h"
#endif
#include "cpregs.h"


static void switch_mode(CPUARMState *env, int mode);

/*
 * Return the effective value of HCR_EL2, at the given security state.
 * Bits that are not included here:
 * RW       (read from SCR_EL3.RW as needed)
 */
uint64_t arm_hcr_el2_eff_secstate(CPUARMState *env, bool secure)
{
    uint64_t ret = env->cp15.hcr_el2;

    if (!arm_is_el2_enabled_secstate(env, secure)) {
        /*
         * "This register has no effect if EL2 is not enabled in the
         * current Security state".  This is ARMv8.4-SecEL2 speak for
         * !(SCR_EL3.NS==1 || SCR_EL3.EEL2==1).
         *
         * Prior to that, the language was "In an implementation that
         * includes EL3, when the value of SCR_EL3.NS is 0 the PE behaves
         * as if this field is 0 for all purposes other than a direct
         * read or write access of HCR_EL2".  With lots of enumeration
         * on a per-field basis.  In current QEMU, this is condition
         * is arm_is_secure_below_el3.
         *
         * Since the v8.4 language applies to the entire register, and
         * appears to be backward compatible, use that.
         */
        return 0;
    }

    /*
     * For a cpu that supports both aarch64 and aarch32, we can set bits
     * in HCR_EL2 (e.g. via EL3) that are RES0 when we enter EL2 as aa32.
     * Ignore all of the bits in HCR+HCR2 that are not valid for aarch32.
     */
    if (!arm_el_is_aa64(env, 2)) {
        uint64_t aa32_valid;

        /*
         * These bits are up-to-date as of ARMv8.6.
         * For HCR, it's easiest to list just the 2 bits that are invalid.
         * For HCR2, list those that are valid.
         */
        aa32_valid = MAKE_64BIT_MASK(0, 32) & ~(HCR_RW | HCR_TDZ);
        aa32_valid |= (HCR_CD | HCR_ID | HCR_TERR | HCR_TEA | HCR_MIOCNCE |
                       HCR_TID4 | HCR_TICAB | HCR_TOCU | HCR_TTLBIS);
        ret &= aa32_valid;
    }

    if (ret & HCR_TGE) {
        /* These bits are up-to-date as of ARMv8.6.  */
        if (ret & HCR_E2H) {
            ret &= ~(HCR_VM | HCR_FMO | HCR_IMO | HCR_AMO |
                     HCR_BSU_MASK | HCR_DC | HCR_TWI | HCR_TWE |
                     HCR_TID0 | HCR_TID2 | HCR_TPCP | HCR_TPU |
                     HCR_TDZ | HCR_CD | HCR_ID | HCR_MIOCNCE |
                     HCR_TID4 | HCR_TICAB | HCR_TOCU | HCR_ENSCXT |
                     HCR_TTLBIS | HCR_TTLBOS | HCR_TID5);
        } else {
            ret |= HCR_FMO | HCR_IMO | HCR_AMO;
        }
        ret &= ~(HCR_SWIO | HCR_PTW | HCR_VF | HCR_VI | HCR_VSE |
                 HCR_FB | HCR_TID1 | HCR_TID3 | HCR_TSC | HCR_TACR |
                 HCR_TSW | HCR_TTLB | HCR_TVM | HCR_HCD | HCR_TRVM |
                 HCR_TLOR);
    }

    return ret;
}

uint64_t arm_hcr_el2_eff(CPUARMState *env)
{
    return arm_hcr_el2_eff_secstate(env, arm_is_secure_below_el3(env));
}

/*
 * Corresponds to ARM pseudocode function ELIsInHost().
 */
bool el_is_in_host(CPUARMState *env, int el)
{
    uint64_t mask;

    /*
     * Since we only care about E2H and TGE, we can skip arm_hcr_el2_eff().
     * Perform the simplest bit tests first, and validate EL2 afterward.
     */
    if (el & 1) {
        return false; /* EL1 or EL3 */
    }

    /*
     * Note that hcr_write() checks isar_feature_aa64_vh(),
     * aka HaveVirtHostExt(), in allowing HCR_E2H to be set.
     */
    mask = el ? HCR_E2H : HCR_E2H | HCR_TGE;
    if ((env->cp15.hcr_el2 & mask) != mask) {
        return false;
    }

    /* TGE and/or E2H set: double check those bits are currently legal. */
    return arm_is_el2_enabled(env) && arm_el_is_aa64(env, 2);
}

/*
 * Return the exception level to which exceptions should be taken
 * via SVEAccessTrap.  This excludes the check for whether the exception
 * should be routed through AArch64.AdvSIMDFPAccessTrap.  That can easily
 * be found by testing 0 < fp_exception_el < sve_exception_el.
 *
 * C.f. the ARM pseudocode function CheckSVEEnabled.  Note that the
 * pseudocode does *not* separate out the FP trap checks, but has them
 * all in one function.
 */
int sve_exception_el(CPUARMState *env, int el)
{
#ifndef CONFIG_USER_ONLY
    if (el <= 1 && !el_is_in_host(env, el)) {
        switch (FIELD_EX64(env->cp15.cpacr_el1, CPACR_EL1, ZEN)) {
        case 1:
            if (el != 0) {
                break;
            }
            /* fall through */
        case 0:
        case 2:
            return 1;
        }
    }

    if (el <= 2 && arm_is_el2_enabled(env)) {
        /* CPTR_EL2 changes format with HCR_EL2.E2H (regardless of TGE). */
        if (env->cp15.hcr_el2 & HCR_E2H) {
            switch (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, ZEN)) {
            case 1:
                if (el != 0 || !(env->cp15.hcr_el2 & HCR_TGE)) {
                    break;
                }
                /* fall through */
            case 0:
            case 2:
                return 2;
            }
        } else {
            if (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, TZ)) {
                return 2;
            }
        }
    }

    /* CPTR_EL3.  Since EZ is negative we must check for EL3.  */
    if (arm_feature(env, ARM_FEATURE_EL3)
        && !FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, EZ)) {
        return 3;
    }
#endif
    return 0;
}

/*
 * Return the exception level to which exceptions should be taken for SME.
 * C.f. the ARM pseudocode function CheckSMEAccess.
 */
int sme_exception_el(CPUARMState *env, int el)
{
#ifndef CONFIG_USER_ONLY
    if (el <= 1 && !el_is_in_host(env, el)) {
        switch (FIELD_EX64(env->cp15.cpacr_el1, CPACR_EL1, SMEN)) {
        case 1:
            if (el != 0) {
                break;
            }
            /* fall through */
        case 0:
        case 2:
            return 1;
        }
    }

    if (el <= 2 && arm_is_el2_enabled(env)) {
        /* CPTR_EL2 changes format with HCR_EL2.E2H (regardless of TGE). */
        if (env->cp15.hcr_el2 & HCR_E2H) {
            switch (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, SMEN)) {
            case 1:
                if (el != 0 || !(env->cp15.hcr_el2 & HCR_TGE)) {
                    break;
                }
                /* fall through */
            case 0:
            case 2:
                return 2;
            }
        } else {
            if (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, TSM)) {
                return 2;
            }
        }
    }

    /* CPTR_EL3.  Since ESM is negative we must check for EL3.  */
    if (arm_feature(env, ARM_FEATURE_EL3)
        && !FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, ESM)) {
        return 3;
    }
#endif
    return 0;
}

/* This corresponds to the ARM pseudocode function IsFullA64Enabled(). */
static bool sme_fa64(CPUARMState *env, int el)
{
    if (!cpu_isar_feature(aa64_sme_fa64, env_archcpu(env))) {
        return false;
    }

    if (el <= 1 && !el_is_in_host(env, el)) {
        if (!FIELD_EX64(env->vfp.smcr_el[1], SMCR, FA64)) {
            return false;
        }
    }
    if (el <= 2 && arm_is_el2_enabled(env)) {
        if (!FIELD_EX64(env->vfp.smcr_el[2], SMCR, FA64)) {
            return false;
        }
    }
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        if (!FIELD_EX64(env->vfp.smcr_el[3], SMCR, FA64)) {
            return false;
        }
    }

    return true;
}

/*
 * Given that SVE is enabled, return the vector length for EL.
 */
uint32_t sve_vqm1_for_el_sm(CPUARMState *env, int el, bool sm)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t *cr = env->vfp.zcr_el;
    uint32_t map = cpu->sve_vq.map;
    uint32_t len = ARM_MAX_VQ - 1;

    if (sm) {
        cr = env->vfp.smcr_el;
        map = cpu->sme_vq.map;
    }

    if (el <= 1 && !el_is_in_host(env, el)) {
        len = MIN(len, 0xf & (uint32_t)cr[1]);
    }
    if (el <= 2 && arm_feature(env, ARM_FEATURE_EL2)) {
        len = MIN(len, 0xf & (uint32_t)cr[2]);
    }
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        len = MIN(len, 0xf & (uint32_t)cr[3]);
    }

    map &= MAKE_64BIT_MASK(0, len + 1);
    if (map != 0) {
        return 31 - clz32(map);
    }

    /* Bit 0 is always set for Normal SVE -- not so for Streaming SVE. */
    assert(sm);
    return ctz32(cpu->sme_vq.map);
}

uint32_t sve_vqm1_for_el(CPUARMState *env, int el)
{
    return sve_vqm1_for_el_sm(env, el, FIELD_EX64(env->svcr, SVCR, SM));
}

/* Sort alphabetically by type name, except for "any". */
static gint arm_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    if (strcmp(name_a, "any-" TYPE_ARM_CPU) == 0) {
        return 1;
    } else if (strcmp(name_b, "any-" TYPE_ARM_CPU) == 0) {
        return -1;
    } else {
        return strcmp(name_a, name_b);
    }
}

static void arm_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUClass *cc = CPU_CLASS(oc);
    const char *typename;
    char *name;

    typename = object_class_get_name(oc);
    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_ARM_CPU));
    if (cc->deprecation_note) {
        qemu_printf("  %s (deprecated)\n", name);
    } else {
        qemu_printf("  %s\n", name);
    }
    g_free(name);
}

void arm_cpu_list(void)
{
    GSList *list;

    list = object_class_get_list(TYPE_ARM_CPU, false);
    list = g_slist_sort(list, arm_cpu_list_compare);
    qemu_printf("Available CPUs:\n");
    g_slist_foreach(list, arm_cpu_list_entry, NULL);
    g_slist_free(list);
}

static void arm_cpu_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info;
    const char *typename;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = g_strndup(typename,
                           strlen(typename) - strlen("-" TYPE_ARM_CPU));
    info->q_typename = g_strdup(typename);

    QAPI_LIST_PREPEND(*cpu_list, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;

    list = object_class_get_list(TYPE_ARM_CPU, false);
    g_slist_foreach(list, arm_cpu_add_definition, &cpu_list);
    g_slist_free(list);

    return cpu_list;
}

static int bad_mode_switch(CPUARMState *env, int mode, CPSRWriteType write_type)
{
    /*
     * Return true if it is not valid for us to switch to
     * this CPU mode (ie all the UNPREDICTABLE cases in
     * the ARM ARM CPSRWriteByInstr pseudocode).
     */

    /* Changes to or from Hyp via MSR and CPS are illegal. */
    if (write_type == CPSRWriteByInstr &&
        ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_HYP ||
         mode == ARM_CPU_MODE_HYP)) {
        return 1;
    }

    switch (mode) {
    case ARM_CPU_MODE_USR:
        return 0;
    case ARM_CPU_MODE_SYS:
    case ARM_CPU_MODE_SVC:
    case ARM_CPU_MODE_ABT:
    case ARM_CPU_MODE_UND:
    case ARM_CPU_MODE_IRQ:
    case ARM_CPU_MODE_FIQ:
        /*
         * Note that we don't implement the IMPDEF NSACR.RFR which in v7
         * allows FIQ mode to be Secure-only. (In v8 this doesn't exist.)
         */
        /*
         * If HCR.TGE is set then changes from Monitor to NS PL1 via MSR
         * and CPS are treated as illegal mode changes.
         */
        if (write_type == CPSRWriteByInstr &&
            (env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_MON &&
            (arm_hcr_el2_eff(env) & HCR_TGE)) {
            return 1;
        }
        return 0;
    case ARM_CPU_MODE_HYP:
        return !arm_is_el2_enabled(env) || arm_current_el(env) < 2;
    case ARM_CPU_MODE_MON:
        return arm_current_el(env) < 3;
    default:
        return 1;
    }
}

uint32_t cpsr_read(CPUARMState *env)
{
    int ZF;
    ZF = (env->ZF == 0);
    return env->uncached_cpsr | (env->NF & 0x80000000) | (ZF << 30) |
        (env->CF << 29) | ((env->VF & 0x80000000) >> 3) | (env->QF << 27)
        | (env->thumb << 5) | ((env->condexec_bits & 3) << 25)
        | ((env->condexec_bits & 0xfc) << 8)
        | (env->GE << 16) | (env->daif & CPSR_AIF);
}

void cpsr_write(CPUARMState *env, uint32_t val, uint32_t mask,
                CPSRWriteType write_type)
{
    uint32_t changed_daif;
    bool rebuild_hflags = (write_type != CPSRWriteRaw) &&
        (mask & (CPSR_M | CPSR_E | CPSR_IL));

    if (mask & CPSR_NZCV) {
        env->ZF = (~val) & CPSR_Z;
        env->NF = val;
        env->CF = (val >> 29) & 1;
        env->VF = (val << 3) & 0x80000000;
    }
    if (mask & CPSR_Q) {
        env->QF = ((val & CPSR_Q) != 0);
    }
    if (mask & CPSR_T) {
        env->thumb = ((val & CPSR_T) != 0);
    }
    if (mask & CPSR_IT_0_1) {
        env->condexec_bits &= ~3;
        env->condexec_bits |= (val >> 25) & 3;
    }
    if (mask & CPSR_IT_2_7) {
        env->condexec_bits &= 3;
        env->condexec_bits |= (val >> 8) & 0xfc;
    }
    if (mask & CPSR_GE) {
        env->GE = (val >> 16) & 0xf;
    }

    /*
     * In a V7 implementation that includes the security extensions but does
     * not include Virtualization Extensions the SCR.FW and SCR.AW bits control
     * whether non-secure software is allowed to change the CPSR_F and CPSR_A
     * bits respectively.
     *
     * In a V8 implementation, it is permitted for privileged software to
     * change the CPSR A/F bits regardless of the SCR.AW/FW bits.
     */
    if (write_type != CPSRWriteRaw && !arm_feature(env, ARM_FEATURE_V8) &&
        arm_feature(env, ARM_FEATURE_EL3) &&
        !arm_feature(env, ARM_FEATURE_EL2) &&
        !arm_is_secure(env)) {

        changed_daif = (env->daif ^ val) & mask;

        if (changed_daif & CPSR_A) {
            /*
             * Check to see if we are allowed to change the masking of async
             * abort exceptions from a non-secure state.
             */
            if (!(env->cp15.scr_el3 & SCR_AW)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Ignoring attempt to switch CPSR_A flag from "
                              "non-secure world with SCR.AW bit clear\n");
                mask &= ~CPSR_A;
            }
        }

        if (changed_daif & CPSR_F) {
            /*
             * Check to see if we are allowed to change the masking of FIQ
             * exceptions from a non-secure state.
             */
            if (!(env->cp15.scr_el3 & SCR_FW)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Ignoring attempt to switch CPSR_F flag from "
                              "non-secure world with SCR.FW bit clear\n");
                mask &= ~CPSR_F;
            }

            /*
             * Check whether non-maskable FIQ (NMFI) support is enabled.
             * If this bit is set software is not allowed to mask
             * FIQs, but is allowed to set CPSR_F to 0.
             */
            if ((A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_NMFI) &&
                (val & CPSR_F)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Ignoring attempt to enable CPSR_F flag "
                              "(non-maskable FIQ [NMFI] support enabled)\n");
                mask &= ~CPSR_F;
            }
        }
    }

    env->daif &= ~(CPSR_AIF & mask);
    env->daif |= val & CPSR_AIF & mask;

    if (write_type != CPSRWriteRaw &&
        ((env->uncached_cpsr ^ val) & mask & CPSR_M)) {
        if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_USR) {
            /*
             * Note that we can only get here in USR mode if this is a
             * gdb stub write; for this case we follow the architectural
             * behaviour for guest writes in USR mode of ignoring an attempt
             * to switch mode. (Those are caught by translate.c for writes
             * triggered by guest instructions.)
             */
            mask &= ~CPSR_M;
        } else if (bad_mode_switch(env, val & CPSR_M, write_type)) {
            /*
             * Attempt to switch to an invalid mode: this is UNPREDICTABLE in
             * v7, and has defined behaviour in v8:
             *  + leave CPSR.M untouched
             *  + allow changes to the other CPSR fields
             *  + set PSTATE.IL
             * For user changes via the GDB stub, we don't set PSTATE.IL,
             * as this would be unnecessarily harsh for a user error.
             */
            mask &= ~CPSR_M;
            if (write_type != CPSRWriteByGDBStub &&
                arm_feature(env, ARM_FEATURE_V8)) {
                mask |= CPSR_IL;
                val |= CPSR_IL;
            }
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Illegal AArch32 mode switch attempt from %s to %s\n",
                          aarch32_mode_name(env->uncached_cpsr),
                          aarch32_mode_name(val));
        } else {
            qemu_log_mask(CPU_LOG_INT, "%s %s to %s PC 0x%" PRIx32 "\n",
                          write_type == CPSRWriteExceptionReturn ?
                          "Exception return from AArch32" :
                          "AArch32 mode switch from",
                          aarch32_mode_name(env->uncached_cpsr),
                          aarch32_mode_name(val), env->regs[15]);
            switch_mode(env, val & CPSR_M);
        }
    }
    mask &= ~CACHED_CPSR_BITS;
    env->uncached_cpsr = (env->uncached_cpsr & ~mask) | (val & mask);
    if (rebuild_hflags) {
        arm_rebuild_hflags(env);
    }
}

/* Sign/zero extend */
uint32_t HELPER(sxtb16)(uint32_t x)
{
    uint32_t res;
    res = (uint16_t)(int8_t)x;
    res |= (uint32_t)(int8_t)(x >> 16) << 16;
    return res;
}

static void handle_possible_div0_trap(CPUARMState *env, uintptr_t ra)
{
    /*
     * Take a division-by-zero exception if necessary; otherwise return
     * to get the usual non-trapping division behaviour (result of 0)
     */
    if (arm_feature(env, ARM_FEATURE_M)
        && (env->v7m.ccr[env->v7m.secure] & R_V7M_CCR_DIV_0_TRP_MASK)) {
        raise_exception_ra(env, EXCP_DIVBYZERO, 0, 1, ra);
    }
}

uint32_t HELPER(uxtb16)(uint32_t x)
{
    uint32_t res;
    res = (uint16_t)(uint8_t)x;
    res |= (uint32_t)(uint8_t)(x >> 16) << 16;
    return res;
}

int32_t HELPER(sdiv)(CPUARMState *env, int32_t num, int32_t den)
{
    if (den == 0) {
        handle_possible_div0_trap(env, GETPC());
        return 0;
    }
    if (num == INT_MIN && den == -1) {
        return INT_MIN;
    }
    return num / den;
}

uint32_t HELPER(udiv)(CPUARMState *env, uint32_t num, uint32_t den)
{
    if (den == 0) {
        handle_possible_div0_trap(env, GETPC());
        return 0;
    }
    return num / den;
}

uint32_t HELPER(rbit)(uint32_t x)
{
    return revbit32(x);
}

#ifdef CONFIG_USER_ONLY

static void switch_mode(CPUARMState *env, int mode)
{
    ARMCPU *cpu = env_archcpu(env);

    if (mode != ARM_CPU_MODE_USR) {
        cpu_abort(CPU(cpu), "Tried to switch out of user mode\n");
    }
}

uint32_t arm_phys_excp_target_el(CPUState *cs, uint32_t excp_idx,
                                 uint32_t cur_el, bool secure)
{
    return 1;
}

void aarch64_sync_64_to_32(CPUARMState *env)
{
    g_assert_not_reached();
}

#else

static void switch_mode(CPUARMState *env, int mode)
{
    int old_mode;
    int i;

    old_mode = env->uncached_cpsr & CPSR_M;
    if (mode == old_mode) {
        return;
    }

    if (old_mode == ARM_CPU_MODE_FIQ) {
        memcpy(env->fiq_regs, env->regs + 8, 5 * sizeof(uint32_t));
        memcpy(env->regs + 8, env->usr_regs, 5 * sizeof(uint32_t));
    } else if (mode == ARM_CPU_MODE_FIQ) {
        memcpy(env->usr_regs, env->regs + 8, 5 * sizeof(uint32_t));
        memcpy(env->regs + 8, env->fiq_regs, 5 * sizeof(uint32_t));
    }

    i = bank_number(old_mode);
    env->banked_r13[i] = env->regs[13];
    env->banked_spsr[i] = env->spsr;

    i = bank_number(mode);
    env->regs[13] = env->banked_r13[i];
    env->spsr = env->banked_spsr[i];

    env->banked_r14[r14_bank_number(old_mode)] = env->regs[14];
    env->regs[14] = env->banked_r14[r14_bank_number(mode)];
}

/*
 * Physical Interrupt Target EL Lookup Table
 *
 * [ From ARM ARM section G1.13.4 (Table G1-15) ]
 *
 * The below multi-dimensional table is used for looking up the target
 * exception level given numerous condition criteria.  Specifically, the
 * target EL is based on SCR and HCR routing controls as well as the
 * currently executing EL and secure state.
 *
 *    Dimensions:
 *    target_el_table[2][2][2][2][2][4]
 *                    |  |  |  |  |  +--- Current EL
 *                    |  |  |  |  +------ Non-secure(0)/Secure(1)
 *                    |  |  |  +--------- HCR mask override
 *                    |  |  +------------ SCR exec state control
 *                    |  +--------------- SCR mask override
 *                    +------------------ 32-bit(0)/64-bit(1) EL3
 *
 *    The table values are as such:
 *    0-3 = EL0-EL3
 *     -1 = Cannot occur
 *
 * The ARM ARM target EL table includes entries indicating that an "exception
 * is not taken".  The two cases where this is applicable are:
 *    1) An exception is taken from EL3 but the SCR does not have the exception
 *    routed to EL3.
 *    2) An exception is taken from EL2 but the HCR does not have the exception
 *    routed to EL2.
 * In these two cases, the below table contain a target of EL1.  This value is
 * returned as it is expected that the consumer of the table data will check
 * for "target EL >= current EL" to ensure the exception is not taken.
 *
 *            SCR     HCR
 *         64  EA     AMO                 From
 *        BIT IRQ     IMO      Non-secure         Secure
 *        EL3 FIQ  RW FMO   EL0 EL1 EL2 EL3   EL0 EL1 EL2 EL3
 */
static const int8_t target_el_table[2][2][2][2][2][4] = {
    {{{{/* 0   0   0   0 */{ 1,  1,  2, -1 },{ 3, -1, -1,  3 },},
       {/* 0   0   0   1 */{ 2,  2,  2, -1 },{ 3, -1, -1,  3 },},},
      {{/* 0   0   1   0 */{ 1,  1,  2, -1 },{ 3, -1, -1,  3 },},
       {/* 0   0   1   1 */{ 2,  2,  2, -1 },{ 3, -1, -1,  3 },},},},
     {{{/* 0   1   0   0 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},
       {/* 0   1   0   1 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},},
      {{/* 0   1   1   0 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},
       {/* 0   1   1   1 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},},},},
    {{{{/* 1   0   0   0 */{ 1,  1,  2, -1 },{ 1,  1, -1,  1 },},
       {/* 1   0   0   1 */{ 2,  2,  2, -1 },{ 2,  2, -1,  1 },},},
      {{/* 1   0   1   0 */{ 1,  1,  1, -1 },{ 1,  1,  1,  1 },},
       {/* 1   0   1   1 */{ 2,  2,  2, -1 },{ 2,  2,  2,  1 },},},},
     {{{/* 1   1   0   0 */{ 3,  3,  3, -1 },{ 3,  3, -1,  3 },},
       {/* 1   1   0   1 */{ 3,  3,  3, -1 },{ 3,  3, -1,  3 },},},
      {{/* 1   1   1   0 */{ 3,  3,  3, -1 },{ 3,  3,  3,  3 },},
       {/* 1   1   1   1 */{ 3,  3,  3, -1 },{ 3,  3,  3,  3 },},},},},
};

/*
 * Determine the target EL for physical exceptions
 */
uint32_t arm_phys_excp_target_el(CPUState *cs, uint32_t excp_idx,
                                 uint32_t cur_el, bool secure)
{
    CPUARMState *env = cs->env_ptr;
    bool rw;
    bool scr;
    bool hcr;
    int target_el;
    /* Is the highest EL AArch64? */
    bool is64 = arm_feature(env, ARM_FEATURE_AARCH64);
    uint64_t hcr_el2;

    if (arm_feature(env, ARM_FEATURE_EL3)) {
        rw = ((env->cp15.scr_el3 & SCR_RW) == SCR_RW);
    } else {
        /*
         * Either EL2 is the highest EL (and so the EL2 register width
         * is given by is64); or there is no EL2 or EL3, in which case
         * the value of 'rw' does not affect the table lookup anyway.
         */
        rw = is64;
    }

    hcr_el2 = arm_hcr_el2_eff(env);
    switch (excp_idx) {
    case EXCP_IRQ:
        scr = ((env->cp15.scr_el3 & SCR_IRQ) == SCR_IRQ);
        hcr = hcr_el2 & HCR_IMO;
        break;
    case EXCP_FIQ:
        scr = ((env->cp15.scr_el3 & SCR_FIQ) == SCR_FIQ);
        hcr = hcr_el2 & HCR_FMO;
        break;
    default:
        scr = ((env->cp15.scr_el3 & SCR_EA) == SCR_EA);
        hcr = hcr_el2 & HCR_AMO;
        break;
    };

    /*
     * For these purposes, TGE and AMO/IMO/FMO both force the
     * interrupt to EL2.  Fold TGE into the bit extracted above.
     */
    hcr |= (hcr_el2 & HCR_TGE) != 0;

    /* Perform a table-lookup for the target EL given the current state */
    target_el = target_el_table[is64][scr][rw][hcr][secure][cur_el];

    assert(target_el > 0);

    return target_el;
}

void arm_log_exception(CPUState *cs)
{
    int idx = cs->exception_index;

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        const char *exc = NULL;
        static const char * const excnames[] = {
            [EXCP_UDEF] = "Undefined Instruction",
            [EXCP_SWI] = "SVC",
            [EXCP_PREFETCH_ABORT] = "Prefetch Abort",
            [EXCP_DATA_ABORT] = "Data Abort",
            [EXCP_IRQ] = "IRQ",
            [EXCP_FIQ] = "FIQ",
            [EXCP_BKPT] = "Breakpoint",
            [EXCP_EXCEPTION_EXIT] = "QEMU v7M exception exit",
            [EXCP_KERNEL_TRAP] = "QEMU intercept of kernel commpage",
            [EXCP_HVC] = "Hypervisor Call",
            [EXCP_HYP_TRAP] = "Hypervisor Trap",
            [EXCP_SMC] = "Secure Monitor Call",
            [EXCP_VIRQ] = "Virtual IRQ",
            [EXCP_VFIQ] = "Virtual FIQ",
            [EXCP_SEMIHOST] = "Semihosting call",
            [EXCP_NOCP] = "v7M NOCP UsageFault",
            [EXCP_INVSTATE] = "v7M INVSTATE UsageFault",
            [EXCP_STKOF] = "v8M STKOF UsageFault",
            [EXCP_LAZYFP] = "v7M exception during lazy FP stacking",
            [EXCP_LSERR] = "v8M LSERR UsageFault",
            [EXCP_UNALIGNED] = "v7M UNALIGNED UsageFault",
            [EXCP_DIVBYZERO] = "v7M DIVBYZERO UsageFault",
            [EXCP_VSERR] = "Virtual SERR",
        };

        if (idx >= 0 && idx < ARRAY_SIZE(excnames)) {
            exc = excnames[idx];
        }
        if (!exc) {
            exc = "unknown";
        }
        qemu_log_mask(CPU_LOG_INT, "Taking exception %d [%s] on CPU %d\n",
                      idx, exc, cs->cpu_index);
    }
}

/*
 * Function used to synchronize QEMU's AArch64 register set with AArch32
 * register set.  This is necessary when switching between AArch32 and AArch64
 * execution state.
 */
void aarch64_sync_32_to_64(CPUARMState *env)
{
    int i;
    uint32_t mode = env->uncached_cpsr & CPSR_M;

    /* We can blanket copy R[0:7] to X[0:7] */
    for (i = 0; i < 8; i++) {
        env->xregs[i] = env->regs[i];
    }

    /*
     * Unless we are in FIQ mode, x8-x12 come from the user registers r8-r12.
     * Otherwise, they come from the banked user regs.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 8; i < 13; i++) {
            env->xregs[i] = env->usr_regs[i - 8];
        }
    } else {
        for (i = 8; i < 13; i++) {
            env->xregs[i] = env->regs[i];
        }
    }

    /*
     * Registers x13-x23 are the various mode SP and FP registers. Registers
     * r13 and r14 are only copied if we are in that mode, otherwise we copy
     * from the mode banked register.
     */
    if (mode == ARM_CPU_MODE_USR || mode == ARM_CPU_MODE_SYS) {
        env->xregs[13] = env->regs[13];
        env->xregs[14] = env->regs[14];
    } else {
        env->xregs[13] = env->banked_r13[bank_number(ARM_CPU_MODE_USR)];
        /* HYP is an exception in that it is copied from r14 */
        if (mode == ARM_CPU_MODE_HYP) {
            env->xregs[14] = env->regs[14];
        } else {
            env->xregs[14] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_USR)];
        }
    }

    if (mode == ARM_CPU_MODE_HYP) {
        env->xregs[15] = env->regs[13];
    } else {
        env->xregs[15] = env->banked_r13[bank_number(ARM_CPU_MODE_HYP)];
    }

    if (mode == ARM_CPU_MODE_IRQ) {
        env->xregs[16] = env->regs[14];
        env->xregs[17] = env->regs[13];
    } else {
        env->xregs[16] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_IRQ)];
        env->xregs[17] = env->banked_r13[bank_number(ARM_CPU_MODE_IRQ)];
    }

    if (mode == ARM_CPU_MODE_SVC) {
        env->xregs[18] = env->regs[14];
        env->xregs[19] = env->regs[13];
    } else {
        env->xregs[18] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_SVC)];
        env->xregs[19] = env->banked_r13[bank_number(ARM_CPU_MODE_SVC)];
    }

    if (mode == ARM_CPU_MODE_ABT) {
        env->xregs[20] = env->regs[14];
        env->xregs[21] = env->regs[13];
    } else {
        env->xregs[20] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_ABT)];
        env->xregs[21] = env->banked_r13[bank_number(ARM_CPU_MODE_ABT)];
    }

    if (mode == ARM_CPU_MODE_UND) {
        env->xregs[22] = env->regs[14];
        env->xregs[23] = env->regs[13];
    } else {
        env->xregs[22] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_UND)];
        env->xregs[23] = env->banked_r13[bank_number(ARM_CPU_MODE_UND)];
    }

    /*
     * Registers x24-x30 are mapped to r8-r14 in FIQ mode.  If we are in FIQ
     * mode, then we can copy from r8-r14.  Otherwise, we copy from the
     * FIQ bank for r8-r14.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 24; i < 31; i++) {
            env->xregs[i] = env->regs[i - 16];   /* X[24:30] <- R[8:14] */
        }
    } else {
        for (i = 24; i < 29; i++) {
            env->xregs[i] = env->fiq_regs[i - 24];
        }
        env->xregs[29] = env->banked_r13[bank_number(ARM_CPU_MODE_FIQ)];
        env->xregs[30] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_FIQ)];
    }

    env->pc = env->regs[15];
}

/*
 * Function used to synchronize QEMU's AArch32 register set with AArch64
 * register set.  This is necessary when switching between AArch32 and AArch64
 * execution state.
 */
void aarch64_sync_64_to_32(CPUARMState *env)
{
    int i;
    uint32_t mode = env->uncached_cpsr & CPSR_M;

    /* We can blanket copy X[0:7] to R[0:7] */
    for (i = 0; i < 8; i++) {
        env->regs[i] = env->xregs[i];
    }

    /*
     * Unless we are in FIQ mode, r8-r12 come from the user registers x8-x12.
     * Otherwise, we copy x8-x12 into the banked user regs.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 8; i < 13; i++) {
            env->usr_regs[i - 8] = env->xregs[i];
        }
    } else {
        for (i = 8; i < 13; i++) {
            env->regs[i] = env->xregs[i];
        }
    }

    /*
     * Registers r13 & r14 depend on the current mode.
     * If we are in a given mode, we copy the corresponding x registers to r13
     * and r14.  Otherwise, we copy the x register to the banked r13 and r14
     * for the mode.
     */
    if (mode == ARM_CPU_MODE_USR || mode == ARM_CPU_MODE_SYS) {
        env->regs[13] = env->xregs[13];
        env->regs[14] = env->xregs[14];
    } else {
        env->banked_r13[bank_number(ARM_CPU_MODE_USR)] = env->xregs[13];

        /*
         * HYP is an exception in that it does not have its own banked r14 but
         * shares the USR r14
         */
        if (mode == ARM_CPU_MODE_HYP) {
            env->regs[14] = env->xregs[14];
        } else {
            env->banked_r14[r14_bank_number(ARM_CPU_MODE_USR)] = env->xregs[14];
        }
    }

    if (mode == ARM_CPU_MODE_HYP) {
        env->regs[13] = env->xregs[15];
    } else {
        env->banked_r13[bank_number(ARM_CPU_MODE_HYP)] = env->xregs[15];
    }

    if (mode == ARM_CPU_MODE_IRQ) {
        env->regs[14] = env->xregs[16];
        env->regs[13] = env->xregs[17];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_IRQ)] = env->xregs[16];
        env->banked_r13[bank_number(ARM_CPU_MODE_IRQ)] = env->xregs[17];
    }

    if (mode == ARM_CPU_MODE_SVC) {
        env->regs[14] = env->xregs[18];
        env->regs[13] = env->xregs[19];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_SVC)] = env->xregs[18];
        env->banked_r13[bank_number(ARM_CPU_MODE_SVC)] = env->xregs[19];
    }

    if (mode == ARM_CPU_MODE_ABT) {
        env->regs[14] = env->xregs[20];
        env->regs[13] = env->xregs[21];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_ABT)] = env->xregs[20];
        env->banked_r13[bank_number(ARM_CPU_MODE_ABT)] = env->xregs[21];
    }

    if (mode == ARM_CPU_MODE_UND) {
        env->regs[14] = env->xregs[22];
        env->regs[13] = env->xregs[23];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_UND)] = env->xregs[22];
        env->banked_r13[bank_number(ARM_CPU_MODE_UND)] = env->xregs[23];
    }

    /*
     * Registers x24-x30 are mapped to r8-r14 in FIQ mode.  If we are in FIQ
     * mode, then we can copy to r8-r14.  Otherwise, we copy to the
     * FIQ bank for r8-r14.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 24; i < 31; i++) {
            env->regs[i - 16] = env->xregs[i];   /* X[24:30] -> R[8:14] */
        }
    } else {
        for (i = 24; i < 29; i++) {
            env->fiq_regs[i - 24] = env->xregs[i];
        }
        env->banked_r13[bank_number(ARM_CPU_MODE_FIQ)] = env->xregs[29];
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_FIQ)] = env->xregs[30];
    }

    env->regs[15] = env->pc;
}

static void take_aarch32_exception(CPUARMState *env, int new_mode,
                                   uint32_t mask, uint32_t offset,
                                   uint32_t newpc)
{
    int new_el;

    /* Change the CPU state so as to actually take the exception. */
    switch_mode(env, new_mode);

    /*
     * For exceptions taken to AArch32 we must clear the SS bit in both
     * PSTATE and in the old-state value we save to SPSR_<mode>, so zero it now.
     */
    env->pstate &= ~PSTATE_SS;
    env->spsr = cpsr_read(env);
    /* Clear IT bits.  */
    env->condexec_bits = 0;
    /* Switch to the new mode, and to the correct instruction set.  */
    env->uncached_cpsr = (env->uncached_cpsr & ~CPSR_M) | new_mode;

    /* This must be after mode switching. */
    new_el = arm_current_el(env);

    /* Set new mode endianness */
    env->uncached_cpsr &= ~CPSR_E;
    if (env->cp15.sctlr_el[new_el] & SCTLR_EE) {
        env->uncached_cpsr |= CPSR_E;
    }
    /* J and IL must always be cleared for exception entry */
    env->uncached_cpsr &= ~(CPSR_IL | CPSR_J);
    env->daif |= mask;

    if (cpu_isar_feature(aa32_ssbs, env_archcpu(env))) {
        if (env->cp15.sctlr_el[new_el] & SCTLR_DSSBS_32) {
            env->uncached_cpsr |= CPSR_SSBS;
        } else {
            env->uncached_cpsr &= ~CPSR_SSBS;
        }
    }

    if (new_mode == ARM_CPU_MODE_HYP) {
        env->thumb = (env->cp15.sctlr_el[2] & SCTLR_TE) != 0;
        env->elr_el[2] = env->regs[15];
    } else {
        /* CPSR.PAN is normally preserved preserved unless...  */
        if (cpu_isar_feature(aa32_pan, env_archcpu(env))) {
            switch (new_el) {
            case 3:
                if (!arm_is_secure_below_el3(env)) {
                    /* ... the target is EL3, from non-secure state.  */
                    env->uncached_cpsr &= ~CPSR_PAN;
                    break;
                }
                /* ... the target is EL3, from secure state ... */
                /* fall through */
            case 1:
                /* ... the target is EL1 and SCTLR.SPAN is 0.  */
                if (!(env->cp15.sctlr_el[new_el] & SCTLR_SPAN)) {
                    env->uncached_cpsr |= CPSR_PAN;
                }
                break;
            }
        }
        /*
         * this is a lie, as there was no c1_sys on V4T/V5, but who cares
         * and we should just guard the thumb mode on V4
         */
        if (arm_feature(env, ARM_FEATURE_V4T)) {
            env->thumb =
                (A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_TE) != 0;
        }
        env->regs[14] = env->regs[15] + offset;
    }
    env->regs[15] = newpc;
    arm_rebuild_hflags(env);
}

static void arm_cpu_do_interrupt_aarch32_hyp(CPUState *cs)
{
    /*
     * Handle exception entry to Hyp mode; this is sufficiently
     * different to entry to other AArch32 modes that we handle it
     * separately here.
     *
     * The vector table entry used is always the 0x14 Hyp mode entry point,
     * unless this is an UNDEF/SVC/HVC/abort taken from Hyp to Hyp.
     * The offset applied to the preferred return address is always zero
     * (see DDI0487C.a section G1.12.3).
     * PSTATE A/I/F masks are set based only on the SCR.EA/IRQ/FIQ values.
     */
    uint32_t addr, mask;
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (cs->exception_index) {
    case EXCP_UDEF:
        addr = 0x04;
        break;
    case EXCP_SWI:
        addr = 0x08;
        break;
    case EXCP_BKPT:
        /* Fall through to prefetch abort.  */
    case EXCP_PREFETCH_ABORT:
        env->cp15.ifar_s = env->exception.vaddress;
        qemu_log_mask(CPU_LOG_INT, "...with HIFAR 0x%x\n",
                      (uint32_t)env->exception.vaddress);
        addr = 0x0c;
        break;
    case EXCP_DATA_ABORT:
        env->cp15.dfar_s = env->exception.vaddress;
        qemu_log_mask(CPU_LOG_INT, "...with HDFAR 0x%x\n",
                      (uint32_t)env->exception.vaddress);
        addr = 0x10;
        break;
    case EXCP_IRQ:
        addr = 0x18;
        break;
    case EXCP_FIQ:
        addr = 0x1c;
        break;
    case EXCP_HVC:
        addr = 0x08;
        break;
    case EXCP_HYP_TRAP:
        addr = 0x14;
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
    }

    if (cs->exception_index != EXCP_IRQ && cs->exception_index != EXCP_FIQ) {
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            /*
             * QEMU syndrome values are v8-style. v7 has the IL bit
             * UNK/SBZP for "field not valid" cases, where v8 uses RES1.
             * If this is a v7 CPU, squash the IL bit in those cases.
             */
            if (cs->exception_index == EXCP_PREFETCH_ABORT ||
                (cs->exception_index == EXCP_DATA_ABORT &&
                 !(env->exception.syndrome & ARM_EL_ISV)) ||
                syn_get_ec(env->exception.syndrome) == EC_UNCATEGORIZED) {
                env->exception.syndrome &= ~ARM_EL_IL;
            }
        }
        env->cp15.esr_el[2] = env->exception.syndrome;
    }

    if (arm_current_el(env) != 2 && addr < 0x14) {
        addr = 0x14;
    }

    mask = 0;
    if (!(env->cp15.scr_el3 & SCR_EA)) {
        mask |= CPSR_A;
    }
    if (!(env->cp15.scr_el3 & SCR_IRQ)) {
        mask |= CPSR_I;
    }
    if (!(env->cp15.scr_el3 & SCR_FIQ)) {
        mask |= CPSR_F;
    }

    addr += env->cp15.hvbar;

    take_aarch32_exception(env, ARM_CPU_MODE_HYP, mask, 0, addr);
}

static void arm_cpu_do_interrupt_aarch32(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t addr;
    uint32_t mask;
    int new_mode;
    uint32_t offset;
    uint32_t moe;

    /* If this is a debug exception we must update the DBGDSCR.MOE bits */
    switch (syn_get_ec(env->exception.syndrome)) {
    case EC_BREAKPOINT:
    case EC_BREAKPOINT_SAME_EL:
        moe = 1;
        break;
    case EC_WATCHPOINT:
    case EC_WATCHPOINT_SAME_EL:
        moe = 10;
        break;
    case EC_AA32_BKPT:
        moe = 3;
        break;
    case EC_VECTORCATCH:
        moe = 5;
        break;
    default:
        moe = 0;
        break;
    }

    if (moe) {
        env->cp15.mdscr_el1 = deposit64(env->cp15.mdscr_el1, 2, 4, moe);
    }

    if (env->exception.target_el == 2) {
        arm_cpu_do_interrupt_aarch32_hyp(cs);
        return;
    }

    switch (cs->exception_index) {
    case EXCP_UDEF:
        new_mode = ARM_CPU_MODE_UND;
        addr = 0x04;
        mask = CPSR_I;
        if (env->thumb) {
            offset = 2;
        } else {
            offset = 4;
        }
        break;
    case EXCP_SWI:
        new_mode = ARM_CPU_MODE_SVC;
        addr = 0x08;
        mask = CPSR_I;
        /* The PC already points to the next instruction.  */
        offset = 0;
        break;
    case EXCP_BKPT:
        /* Fall through to prefetch abort.  */
    case EXCP_PREFETCH_ABORT:
        A32_BANKED_CURRENT_REG_SET(env, ifsr, env->exception.fsr);
        A32_BANKED_CURRENT_REG_SET(env, ifar, env->exception.vaddress);
        qemu_log_mask(CPU_LOG_INT, "...with IFSR 0x%x IFAR 0x%x\n",
                      env->exception.fsr, (uint32_t)env->exception.vaddress);
        new_mode = ARM_CPU_MODE_ABT;
        addr = 0x0c;
        mask = CPSR_A | CPSR_I;
        offset = 4;
        break;
    case EXCP_DATA_ABORT:
        A32_BANKED_CURRENT_REG_SET(env, dfsr, env->exception.fsr);
        A32_BANKED_CURRENT_REG_SET(env, dfar, env->exception.vaddress);
        qemu_log_mask(CPU_LOG_INT, "...with DFSR 0x%x DFAR 0x%x\n",
                      env->exception.fsr,
                      (uint32_t)env->exception.vaddress);
        new_mode = ARM_CPU_MODE_ABT;
        addr = 0x10;
        mask = CPSR_A | CPSR_I;
        offset = 8;
        break;
    case EXCP_IRQ:
        new_mode = ARM_CPU_MODE_IRQ;
        addr = 0x18;
        /* Disable IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I;
        offset = 4;
        if (env->cp15.scr_el3 & SCR_IRQ) {
            /* IRQ routed to monitor mode */
            new_mode = ARM_CPU_MODE_MON;
            mask |= CPSR_F;
        }
        break;
    case EXCP_FIQ:
        new_mode = ARM_CPU_MODE_FIQ;
        addr = 0x1c;
        /* Disable FIQ, IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I | CPSR_F;
        if (env->cp15.scr_el3 & SCR_FIQ) {
            /* FIQ routed to monitor mode */
            new_mode = ARM_CPU_MODE_MON;
        }
        offset = 4;
        break;
    case EXCP_VIRQ:
        new_mode = ARM_CPU_MODE_IRQ;
        addr = 0x18;
        /* Disable IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I;
        offset = 4;
        break;
    case EXCP_VFIQ:
        new_mode = ARM_CPU_MODE_FIQ;
        addr = 0x1c;
        /* Disable FIQ, IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I | CPSR_F;
        offset = 4;
        break;
    case EXCP_VSERR:
        {
            /*
             * Note that this is reported as a data abort, but the DFAR
             * has an UNKNOWN value.  Construct the SError syndrome from
             * AET and ExT fields.
             */
            ARMMMUFaultInfo fi = { .type = ARMFault_AsyncExternal, };

            if (extended_addresses_enabled(env)) {
                env->exception.fsr = arm_fi_to_lfsc(&fi);
            } else {
                env->exception.fsr = arm_fi_to_sfsc(&fi);
            }
            env->exception.fsr |= env->cp15.vsesr_el2 & 0xd000;
            A32_BANKED_CURRENT_REG_SET(env, dfsr, env->exception.fsr);
            qemu_log_mask(CPU_LOG_INT, "...with IFSR 0x%x\n",
                          env->exception.fsr);

            new_mode = ARM_CPU_MODE_ABT;
            addr = 0x10;
            mask = CPSR_A | CPSR_I;
            offset = 8;
        }
        break;
    case EXCP_SMC:
        new_mode = ARM_CPU_MODE_MON;
        addr = 0x08;
        mask = CPSR_A | CPSR_I | CPSR_F;
        offset = 0;
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
        return; /* Never happens.  Keep compiler happy.  */
    }

    if (new_mode == ARM_CPU_MODE_MON) {
        addr += env->cp15.mvbar;
    } else if (A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_V) {
        /* High vectors. When enabled, base address cannot be remapped. */
        addr += 0xffff0000;
    } else {
        /*
         * ARM v7 architectures provide a vector base address register to remap
         * the interrupt vector table.
         * This register is only followed in non-monitor mode, and is banked.
         * Note: only bits 31:5 are valid.
         */
        addr += A32_BANKED_CURRENT_REG_GET(env, vbar);
    }

    if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_MON) {
        env->cp15.scr_el3 &= ~SCR_NS;
    }

    take_aarch32_exception(env, new_mode, mask, offset, addr);
}

static int aarch64_regnum(CPUARMState *env, int aarch32_reg)
{
    /*
     * Return the register number of the AArch64 view of the AArch32
     * register @aarch32_reg. The CPUARMState CPSR is assumed to still
     * be that of the AArch32 mode the exception came from.
     */
    int mode = env->uncached_cpsr & CPSR_M;

    switch (aarch32_reg) {
    case 0 ... 7:
        return aarch32_reg;
    case 8 ... 12:
        return mode == ARM_CPU_MODE_FIQ ? aarch32_reg + 16 : aarch32_reg;
    case 13:
        switch (mode) {
        case ARM_CPU_MODE_USR:
        case ARM_CPU_MODE_SYS:
            return 13;
        case ARM_CPU_MODE_HYP:
            return 15;
        case ARM_CPU_MODE_IRQ:
            return 17;
        case ARM_CPU_MODE_SVC:
            return 19;
        case ARM_CPU_MODE_ABT:
            return 21;
        case ARM_CPU_MODE_UND:
            return 23;
        case ARM_CPU_MODE_FIQ:
            return 29;
        default:
            g_assert_not_reached();
        }
    case 14:
        switch (mode) {
        case ARM_CPU_MODE_USR:
        case ARM_CPU_MODE_SYS:
        case ARM_CPU_MODE_HYP:
            return 14;
        case ARM_CPU_MODE_IRQ:
            return 16;
        case ARM_CPU_MODE_SVC:
            return 18;
        case ARM_CPU_MODE_ABT:
            return 20;
        case ARM_CPU_MODE_UND:
            return 22;
        case ARM_CPU_MODE_FIQ:
            return 30;
        default:
            g_assert_not_reached();
        }
    case 15:
        return 31;
    default:
        g_assert_not_reached();
    }
}

static uint32_t cpsr_read_for_spsr_elx(CPUARMState *env)
{
    uint32_t ret = cpsr_read(env);

    /* Move DIT to the correct location for SPSR_ELx */
    if (ret & CPSR_DIT) {
        ret &= ~CPSR_DIT;
        ret |= PSTATE_DIT;
    }
    /* Merge PSTATE.SS into SPSR_ELx */
    ret |= env->pstate & PSTATE_SS;

    return ret;
}

static bool syndrome_is_sync_extabt(uint32_t syndrome)
{
    /* Return true if this syndrome value is a synchronous external abort */
    switch (syn_get_ec(syndrome)) {
    case EC_INSNABORT:
    case EC_INSNABORT_SAME_EL:
    case EC_DATAABORT:
    case EC_DATAABORT_SAME_EL:
        /* Look at fault status code for all the synchronous ext abort cases */
        switch (syndrome & 0x3f) {
        case 0x10:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}

/* Handle exception entry to a target EL which is using AArch64 */
static void arm_cpu_do_interrupt_aarch64(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    unsigned int new_el = env->exception.target_el;
    target_ulong addr = env->cp15.vbar_el[new_el];
    unsigned int new_mode = aarch64_pstate_mode(new_el, true);
    unsigned int old_mode;
    unsigned int cur_el = arm_current_el(env);
    int rt;

    if (tcg_enabled()) {
        /*
         * Note that new_el can never be 0.  If cur_el is 0, then
         * el0_a64 is is_a64(), else el0_a64 is ignored.
         */
        aarch64_sve_change_el(env, cur_el, new_el, is_a64(env));
    }

    if (cur_el < new_el) {
        /*
         * Entry vector offset depends on whether the implemented EL
         * immediately lower than the target level is using AArch32 or AArch64
         */
        bool is_aa64;
        uint64_t hcr;

        switch (new_el) {
        case 3:
            is_aa64 = (env->cp15.scr_el3 & SCR_RW) != 0;
            break;
        case 2:
            hcr = arm_hcr_el2_eff(env);
            if ((hcr & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
                is_aa64 = (hcr & HCR_RW) != 0;
                break;
            }
            /* fall through */
        case 1:
            is_aa64 = is_a64(env);
            break;
        default:
            g_assert_not_reached();
        }

        if (is_aa64) {
            addr += 0x400;
        } else {
            addr += 0x600;
        }
    } else if (pstate_read(env) & PSTATE_SP) {
        addr += 0x200;
    }

    switch (cs->exception_index) {
    case EXCP_PREFETCH_ABORT:
    case EXCP_DATA_ABORT:
        /*
         * FEAT_DoubleFault allows synchronous external aborts taken to EL3
         * to be taken to the SError vector entrypoint.
         */
        if (new_el == 3 && (env->cp15.scr_el3 & SCR_EASE) &&
            syndrome_is_sync_extabt(env->exception.syndrome)) {
            addr += 0x180;
        }
        env->cp15.far_el[new_el] = env->exception.vaddress;
        qemu_log_mask(CPU_LOG_INT, "...with FAR 0x%" PRIx64 "\n",
                      env->cp15.far_el[new_el]);
        /* fall through */
    case EXCP_BKPT:
    case EXCP_UDEF:
    case EXCP_SWI:
    case EXCP_HVC:
    case EXCP_HYP_TRAP:
    case EXCP_SMC:
        switch (syn_get_ec(env->exception.syndrome)) {
        case EC_ADVSIMDFPACCESSTRAP:
            /*
             * QEMU internal FP/SIMD syndromes from AArch32 include the
             * TA and coproc fields which are only exposed if the exception
             * is taken to AArch32 Hyp mode. Mask them out to get a valid
             * AArch64 format syndrome.
             */
            env->exception.syndrome &= ~MAKE_64BIT_MASK(0, 20);
            break;
        case EC_CP14RTTRAP:
        case EC_CP15RTTRAP:
        case EC_CP14DTTRAP:
            /*
             * For a trap on AArch32 MRC/MCR/LDC/STC the Rt field is currently
             * the raw register field from the insn; when taking this to
             * AArch64 we must convert it to the AArch64 view of the register
             * number. Notice that we read a 4-bit AArch32 register number and
             * write back a 5-bit AArch64 one.
             */
            rt = extract32(env->exception.syndrome, 5, 4);
            rt = aarch64_regnum(env, rt);
            env->exception.syndrome = deposit32(env->exception.syndrome,
                                                5, 5, rt);
            break;
        case EC_CP15RRTTRAP:
        case EC_CP14RRTTRAP:
            /* Similarly for MRRC/MCRR traps for Rt and Rt2 fields */
            rt = extract32(env->exception.syndrome, 5, 4);
            rt = aarch64_regnum(env, rt);
            env->exception.syndrome = deposit32(env->exception.syndrome,
                                                5, 5, rt);
            rt = extract32(env->exception.syndrome, 10, 4);
            rt = aarch64_regnum(env, rt);
            env->exception.syndrome = deposit32(env->exception.syndrome,
                                                10, 5, rt);
            break;
        }
        env->cp15.esr_el[new_el] = env->exception.syndrome;
        break;
    case EXCP_IRQ:
    case EXCP_VIRQ:
        addr += 0x80;
        break;
    case EXCP_FIQ:
    case EXCP_VFIQ:
        addr += 0x100;
        break;
    case EXCP_VSERR:
        addr += 0x180;
        /* Construct the SError syndrome from IDS and ISS fields. */
        env->exception.syndrome = syn_serror(env->cp15.vsesr_el2 & 0x1ffffff);
        env->cp15.esr_el[new_el] = env->exception.syndrome;
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
    }

    if (is_a64(env)) {
        old_mode = pstate_read(env);
        aarch64_save_sp(env, arm_current_el(env));
        env->elr_el[new_el] = env->pc;
    } else {
        old_mode = cpsr_read_for_spsr_elx(env);
        env->elr_el[new_el] = env->regs[15];

        aarch64_sync_32_to_64(env);

        env->condexec_bits = 0;
    }
    env->banked_spsr[aarch64_banked_spsr_index(new_el)] = old_mode;

    qemu_log_mask(CPU_LOG_INT, "...with ELR 0x%" PRIx64 "\n",
                  env->elr_el[new_el]);

    if (cpu_isar_feature(aa64_pan, cpu)) {
        /* The value of PSTATE.PAN is normally preserved, except when ... */
        new_mode |= old_mode & PSTATE_PAN;
        switch (new_el) {
        case 2:
            /* ... the target is EL2 with HCR_EL2.{E2H,TGE} == '11' ...  */
            if ((arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE))
                != (HCR_E2H | HCR_TGE)) {
                break;
            }
            /* fall through */
        case 1:
            /* ... the target is EL1 ... */
            /* ... and SCTLR_ELx.SPAN == 0, then set to 1.  */
            if ((env->cp15.sctlr_el[new_el] & SCTLR_SPAN) == 0) {
                new_mode |= PSTATE_PAN;
            }
            break;
        }
    }
    if (cpu_isar_feature(aa64_mte, cpu)) {
        new_mode |= PSTATE_TCO;
    }

    if (cpu_isar_feature(aa64_ssbs, cpu)) {
        if (env->cp15.sctlr_el[new_el] & SCTLR_DSSBS_64) {
            new_mode |= PSTATE_SSBS;
        } else {
            new_mode &= ~PSTATE_SSBS;
        }
    }

    pstate_write(env, PSTATE_DAIF | new_mode);
    env->aarch64 = true;
    aarch64_restore_sp(env, new_el);
    helper_rebuild_hflags_a64(env, new_el);

    env->pc = addr;

    qemu_log_mask(CPU_LOG_INT, "...to EL%d PC 0x%" PRIx64 " PSTATE 0x%x\n",
                  new_el, env->pc, pstate_read(env));
}

/*
 * Do semihosting call and set the appropriate return value. All the
 * permission and validity checks have been done at translate time.
 *
 * We only see semihosting exceptions in TCG only as they are not
 * trapped to the hypervisor in KVM.
 */
#ifdef CONFIG_TCG
static void tcg_handle_semihosting(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (is_a64(env)) {
        qemu_log_mask(CPU_LOG_INT,
                      "...handling as semihosting call 0x%" PRIx64 "\n",
                      env->xregs[0]);
        do_common_semihosting(cs);
        env->pc += 4;
    } else {
        qemu_log_mask(CPU_LOG_INT,
                      "...handling as semihosting call 0x%x\n",
                      env->regs[0]);
        do_common_semihosting(cs);
        env->regs[15] += env->thumb ? 2 : 4;
    }
}
#endif

/*
 * Handle a CPU exception for A and R profile CPUs.
 * Do any appropriate logging, handle PSCI calls, and then hand off
 * to the AArch64-entry or AArch32-entry function depending on the
 * target exception level's register width.
 *
 * Note: this is used for both TCG (as the do_interrupt tcg op),
 *       and KVM to re-inject guest debug exceptions, and to
 *       inject a Synchronous-External-Abort.
 */
void arm_cpu_do_interrupt(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    unsigned int new_el = env->exception.target_el;

    assert(!arm_feature(env, ARM_FEATURE_M));

    arm_log_exception(cs);
    qemu_log_mask(CPU_LOG_INT, "...from EL%d to EL%d\n", arm_current_el(env),
                  new_el);
    if (qemu_loglevel_mask(CPU_LOG_INT)
        && !excp_is_internal(cs->exception_index)) {
        qemu_log_mask(CPU_LOG_INT, "...with ESR 0x%x/0x%" PRIx32 "\n",
                      syn_get_ec(env->exception.syndrome),
                      env->exception.syndrome);
    }

    if (tcg_enabled() && arm_is_psci_call(cpu, cs->exception_index)) {
        arm_handle_psci_call(cpu);
        qemu_log_mask(CPU_LOG_INT, "...handled as PSCI call\n");
        return;
    }

    /*
     * Semihosting semantics depend on the register width of the code
     * that caused the exception, not the target exception level, so
     * must be handled here.
     */
#ifdef CONFIG_TCG
    if (cs->exception_index == EXCP_SEMIHOST) {
        tcg_handle_semihosting(cs);
        return;
    }
#endif

    /*
     * Hooks may change global state so BQL should be held, also the
     * BQL needs to be held for any modification of
     * cs->interrupt_request.
     */
    g_assert(qemu_mutex_iothread_locked());

    arm_call_pre_el_change_hook(cpu);

    assert(!excp_is_internal(cs->exception_index));
    if (arm_el_is_aa64(env, new_el)) {
        arm_cpu_do_interrupt_aarch64(cs);
    } else {
        arm_cpu_do_interrupt_aarch32(cs);
    }

    arm_call_el_change_hook(cpu);

    if (!kvm_enabled()) {
        cs->interrupt_request |= CPU_INTERRUPT_EXITTB;
    }
}
#endif /* !CONFIG_USER_ONLY */

uint64_t arm_sctlr(CPUARMState *env, int el)
{
    /* Only EL0 needs to be adjusted for EL1&0 or EL2&0. */
    if (el == 0) {
        ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, 0);
        el = mmu_idx == ARMMMUIdx_E20_0 ? 2 : 1;
    }
    return env->cp15.sctlr_el[el];
}

int aa64_va_parameter_tbi(uint64_t tcr, ARMMMUIdx mmu_idx)
{
    if (regime_has_2_ranges(mmu_idx)) {
        return extract64(tcr, 37, 2);
    } else if (regime_is_stage2(mmu_idx)) {
        return 0; /* VTCR_EL2 */
    } else {
        /* Replicate the single TBI bit so we always have 2 bits.  */
        return extract32(tcr, 20, 1) * 3;
    }
}

int aa64_va_parameter_tbid(uint64_t tcr, ARMMMUIdx mmu_idx)
{
    if (regime_has_2_ranges(mmu_idx)) {
        return extract64(tcr, 51, 2);
    } else if (regime_is_stage2(mmu_idx)) {
        return 0; /* VTCR_EL2 */
    } else {
        /* Replicate the single TBID bit so we always have 2 bits.  */
        return extract32(tcr, 29, 1) * 3;
    }
}

static int aa64_va_parameter_tcma(uint64_t tcr, ARMMMUIdx mmu_idx)
{
    if (regime_has_2_ranges(mmu_idx)) {
        return extract64(tcr, 57, 2);
    } else {
        /* Replicate the single TCMA bit so we always have 2 bits.  */
        return extract32(tcr, 30, 1) * 3;
    }
}

static ARMGranuleSize tg0_to_gran_size(int tg)
{
    switch (tg) {
    case 0:
        return Gran4K;
    case 1:
        return Gran64K;
    case 2:
        return Gran16K;
    default:
        return GranInvalid;
    }
}

static ARMGranuleSize tg1_to_gran_size(int tg)
{
    switch (tg) {
    case 1:
        return Gran16K;
    case 2:
        return Gran4K;
    case 3:
        return Gran64K;
    default:
        return GranInvalid;
    }
}

static inline bool have4k(ARMCPU *cpu, bool stage2)
{
    return stage2 ? cpu_isar_feature(aa64_tgran4_2, cpu)
        : cpu_isar_feature(aa64_tgran4, cpu);
}

static inline bool have16k(ARMCPU *cpu, bool stage2)
{
    return stage2 ? cpu_isar_feature(aa64_tgran16_2, cpu)
        : cpu_isar_feature(aa64_tgran16, cpu);
}

static inline bool have64k(ARMCPU *cpu, bool stage2)
{
    return stage2 ? cpu_isar_feature(aa64_tgran64_2, cpu)
        : cpu_isar_feature(aa64_tgran64, cpu);
}

static ARMGranuleSize sanitize_gran_size(ARMCPU *cpu, ARMGranuleSize gran,
                                         bool stage2)
{
    switch (gran) {
    case Gran4K:
        if (have4k(cpu, stage2)) {
            return gran;
        }
        break;
    case Gran16K:
        if (have16k(cpu, stage2)) {
            return gran;
        }
        break;
    case Gran64K:
        if (have64k(cpu, stage2)) {
            return gran;
        }
        break;
    case GranInvalid:
        break;
    }
    /*
     * If the guest selects a granule size that isn't implemented,
     * the architecture requires that we behave as if it selected one
     * that is (with an IMPDEF choice of which one to pick). We choose
     * to implement the smallest supported granule size.
     */
    if (have4k(cpu, stage2)) {
        return Gran4K;
    }
    if (have16k(cpu, stage2)) {
        return Gran16K;
    }
    assert(have64k(cpu, stage2));
    return Gran64K;
}

ARMVAParameters aa64_va_parameters(CPUARMState *env, uint64_t va,
                                   ARMMMUIdx mmu_idx, bool data)
{
    uint64_t tcr = regime_tcr(env, mmu_idx);
    bool epd, hpd, tsz_oob, ds, ha, hd;
    int select, tsz, tbi, max_tsz, min_tsz, ps, sh;
    ARMGranuleSize gran;
    ARMCPU *cpu = env_archcpu(env);
    bool stage2 = regime_is_stage2(mmu_idx);

    if (!regime_has_2_ranges(mmu_idx)) {
        select = 0;
        tsz = extract32(tcr, 0, 6);
        gran = tg0_to_gran_size(extract32(tcr, 14, 2));
        if (stage2) {
            /* VTCR_EL2 */
            hpd = false;
        } else {
            hpd = extract32(tcr, 24, 1);
        }
        epd = false;
        sh = extract32(tcr, 12, 2);
        ps = extract32(tcr, 16, 3);
        ha = extract32(tcr, 21, 1) && cpu_isar_feature(aa64_hafs, cpu);
        hd = extract32(tcr, 22, 1) && cpu_isar_feature(aa64_hdbs, cpu);
        ds = extract64(tcr, 32, 1);
    } else {
        bool e0pd;

        /*
         * Bit 55 is always between the two regions, and is canonical for
         * determining if address tagging is enabled.
         */
        select = extract64(va, 55, 1);
        if (!select) {
            tsz = extract32(tcr, 0, 6);
            gran = tg0_to_gran_size(extract32(tcr, 14, 2));
            epd = extract32(tcr, 7, 1);
            sh = extract32(tcr, 12, 2);
            hpd = extract64(tcr, 41, 1);
            e0pd = extract64(tcr, 55, 1);
        } else {
            tsz = extract32(tcr, 16, 6);
            gran = tg1_to_gran_size(extract32(tcr, 30, 2));
            epd = extract32(tcr, 23, 1);
            sh = extract32(tcr, 28, 2);
            hpd = extract64(tcr, 42, 1);
            e0pd = extract64(tcr, 56, 1);
        }
        ps = extract64(tcr, 32, 3);
        ha = extract64(tcr, 39, 1) && cpu_isar_feature(aa64_hafs, cpu);
        hd = extract64(tcr, 40, 1) && cpu_isar_feature(aa64_hdbs, cpu);
        ds = extract64(tcr, 59, 1);

        if (e0pd && cpu_isar_feature(aa64_e0pd, cpu) &&
            regime_is_user(env, mmu_idx)) {
            epd = true;
        }
    }

    gran = sanitize_gran_size(cpu, gran, stage2);

    if (cpu_isar_feature(aa64_st, cpu)) {
        max_tsz = 48 - (gran == Gran64K);
    } else {
        max_tsz = 39;
    }

    /*
     * DS is RES0 unless FEAT_LPA2 is supported for the given page size;
     * adjust the effective value of DS, as documented.
     */
    min_tsz = 16;
    if (gran == Gran64K) {
        if (cpu_isar_feature(aa64_lva, cpu)) {
            min_tsz = 12;
        }
        ds = false;
    } else if (ds) {
        if (regime_is_stage2(mmu_idx)) {
            if (gran == Gran16K) {
                ds = cpu_isar_feature(aa64_tgran16_2_lpa2, cpu);
            } else {
                ds = cpu_isar_feature(aa64_tgran4_2_lpa2, cpu);
            }
        } else {
            if (gran == Gran16K) {
                ds = cpu_isar_feature(aa64_tgran16_lpa2, cpu);
            } else {
                ds = cpu_isar_feature(aa64_tgran4_lpa2, cpu);
            }
        }
        if (ds) {
            min_tsz = 12;
        }
    }

    if (tsz > max_tsz) {
        tsz = max_tsz;
        tsz_oob = true;
    } else if (tsz < min_tsz) {
        tsz = min_tsz;
        tsz_oob = true;
    } else {
        tsz_oob = false;
    }

    /* Present TBI as a composite with TBID.  */
    tbi = aa64_va_parameter_tbi(tcr, mmu_idx);
    if (!data) {
        tbi &= ~aa64_va_parameter_tbid(tcr, mmu_idx);
    }
    tbi = (tbi >> select) & 1;

    return (ARMVAParameters) {
        .tsz = tsz,
        .ps = ps,
        .sh = sh,
        .select = select,
        .tbi = tbi,
        .epd = epd,
        .hpd = hpd,
        .tsz_oob = tsz_oob,
        .ds = ds,
        .ha = ha,
        .hd = ha && hd,
        .gran = gran,
    };
}

/*
 * Note that signed overflow is undefined in C.  The following routines are
 * careful to use unsigned types where modulo arithmetic is required.
 * Failure to do so _will_ break on newer gcc.
 */

/* Signed saturating arithmetic.  */

/* Perform 16-bit signed saturating addition.  */
static inline uint16_t add16_sat(uint16_t a, uint16_t b)
{
    uint16_t res;

    res = a + b;
    if (((res ^ a) & 0x8000) && !((a ^ b) & 0x8000)) {
        if (a & 0x8000) {
            res = 0x8000;
        } else {
            res = 0x7fff;
        }
    }
    return res;
}

/* Perform 8-bit signed saturating addition.  */
static inline uint8_t add8_sat(uint8_t a, uint8_t b)
{
    uint8_t res;

    res = a + b;
    if (((res ^ a) & 0x80) && !((a ^ b) & 0x80)) {
        if (a & 0x80) {
            res = 0x80;
        } else {
            res = 0x7f;
        }
    }
    return res;
}

/* Perform 16-bit signed saturating subtraction.  */
static inline uint16_t sub16_sat(uint16_t a, uint16_t b)
{
    uint16_t res;

    res = a - b;
    if (((res ^ a) & 0x8000) && ((a ^ b) & 0x8000)) {
        if (a & 0x8000) {
            res = 0x8000;
        } else {
            res = 0x7fff;
        }
    }
    return res;
}

/* Perform 8-bit signed saturating subtraction.  */
static inline uint8_t sub8_sat(uint8_t a, uint8_t b)
{
    uint8_t res;

    res = a - b;
    if (((res ^ a) & 0x80) && ((a ^ b) & 0x80)) {
        if (a & 0x80) {
            res = 0x80;
        } else {
            res = 0x7f;
        }
    }
    return res;
}

#define ADD16(a, b, n) RESULT(add16_sat(a, b), n, 16);
#define SUB16(a, b, n) RESULT(sub16_sat(a, b), n, 16);
#define ADD8(a, b, n)  RESULT(add8_sat(a, b), n, 8);
#define SUB8(a, b, n)  RESULT(sub8_sat(a, b), n, 8);
#define PFX q

#include "op_addsub.h"

/* Unsigned saturating arithmetic.  */
static inline uint16_t add16_usat(uint16_t a, uint16_t b)
{
    uint16_t res;
    res = a + b;
    if (res < a) {
        res = 0xffff;
    }
    return res;
}

static inline uint16_t sub16_usat(uint16_t a, uint16_t b)
{
    if (a > b) {
        return a - b;
    } else {
        return 0;
    }
}

static inline uint8_t add8_usat(uint8_t a, uint8_t b)
{
    uint8_t res;
    res = a + b;
    if (res < a) {
        res = 0xff;
    }
    return res;
}

static inline uint8_t sub8_usat(uint8_t a, uint8_t b)
{
    if (a > b) {
        return a - b;
    } else {
        return 0;
    }
}

#define ADD16(a, b, n) RESULT(add16_usat(a, b), n, 16);
#define SUB16(a, b, n) RESULT(sub16_usat(a, b), n, 16);
#define ADD8(a, b, n)  RESULT(add8_usat(a, b), n, 8);
#define SUB8(a, b, n)  RESULT(sub8_usat(a, b), n, 8);
#define PFX uq

#include "op_addsub.h"

/* Signed modulo arithmetic.  */
#define SARITH16(a, b, n, op) do { \
    int32_t sum; \
    sum = (int32_t)(int16_t)(a) op (int32_t)(int16_t)(b); \
    RESULT(sum, n, 16); \
    if (sum >= 0) \
        ge |= 3 << (n * 2); \
    } while (0)

#define SARITH8(a, b, n, op) do { \
    int32_t sum; \
    sum = (int32_t)(int8_t)(a) op (int32_t)(int8_t)(b); \
    RESULT(sum, n, 8); \
    if (sum >= 0) \
        ge |= 1 << n; \
    } while (0)


#define ADD16(a, b, n) SARITH16(a, b, n, +)
#define SUB16(a, b, n) SARITH16(a, b, n, -)
#define ADD8(a, b, n)  SARITH8(a, b, n, +)
#define SUB8(a, b, n)  SARITH8(a, b, n, -)
#define PFX s
#define ARITH_GE

#include "op_addsub.h"

/* Unsigned modulo arithmetic.  */
#define ADD16(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint16_t)(a) + (uint32_t)(uint16_t)(b); \
    RESULT(sum, n, 16); \
    if ((sum >> 16) == 1) \
        ge |= 3 << (n * 2); \
    } while (0)

#define ADD8(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint8_t)(a) + (uint32_t)(uint8_t)(b); \
    RESULT(sum, n, 8); \
    if ((sum >> 8) == 1) \
        ge |= 1 << n; \
    } while (0)

#define SUB16(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint16_t)(a) - (uint32_t)(uint16_t)(b); \
    RESULT(sum, n, 16); \
    if ((sum >> 16) == 0) \
        ge |= 3 << (n * 2); \
    } while (0)

#define SUB8(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint8_t)(a) - (uint32_t)(uint8_t)(b); \
    RESULT(sum, n, 8); \
    if ((sum >> 8) == 0) \
        ge |= 1 << n; \
    } while (0)

#define PFX u
#define ARITH_GE

#include "op_addsub.h"

/* Halved signed arithmetic.  */
#define ADD16(a, b, n) \
  RESULT(((int32_t)(int16_t)(a) + (int32_t)(int16_t)(b)) >> 1, n, 16)
#define SUB16(a, b, n) \
  RESULT(((int32_t)(int16_t)(a) - (int32_t)(int16_t)(b)) >> 1, n, 16)
#define ADD8(a, b, n) \
  RESULT(((int32_t)(int8_t)(a) + (int32_t)(int8_t)(b)) >> 1, n, 8)
#define SUB8(a, b, n) \
  RESULT(((int32_t)(int8_t)(a) - (int32_t)(int8_t)(b)) >> 1, n, 8)
#define PFX sh

#include "op_addsub.h"

/* Halved unsigned arithmetic.  */
#define ADD16(a, b, n) \
  RESULT(((uint32_t)(uint16_t)(a) + (uint32_t)(uint16_t)(b)) >> 1, n, 16)
#define SUB16(a, b, n) \
  RESULT(((uint32_t)(uint16_t)(a) - (uint32_t)(uint16_t)(b)) >> 1, n, 16)
#define ADD8(a, b, n) \
  RESULT(((uint32_t)(uint8_t)(a) + (uint32_t)(uint8_t)(b)) >> 1, n, 8)
#define SUB8(a, b, n) \
  RESULT(((uint32_t)(uint8_t)(a) - (uint32_t)(uint8_t)(b)) >> 1, n, 8)
#define PFX uh

#include "op_addsub.h"

static inline uint8_t do_usad(uint8_t a, uint8_t b)
{
    if (a > b) {
        return a - b;
    } else {
        return b - a;
    }
}

/* Unsigned sum of absolute byte differences.  */
uint32_t HELPER(usad8)(uint32_t a, uint32_t b)
{
    uint32_t sum;
    sum = do_usad(a, b);
    sum += do_usad(a >> 8, b >> 8);
    sum += do_usad(a >> 16, b >> 16);
    sum += do_usad(a >> 24, b >> 24);
    return sum;
}

/* For ARMv6 SEL instruction.  */
uint32_t HELPER(sel_flags)(uint32_t flags, uint32_t a, uint32_t b)
{
    uint32_t mask;

    mask = 0;
    if (flags & 1) {
        mask |= 0xff;
    }
    if (flags & 2) {
        mask |= 0xff00;
    }
    if (flags & 4) {
        mask |= 0xff0000;
    }
    if (flags & 8) {
        mask |= 0xff000000;
    }
    return (a & mask) | (b & ~mask);
}

/*
 * CRC helpers.
 * The upper bytes of val (above the number specified by 'bytes') must have
 * been zeroed out by the caller.
 */
uint32_t HELPER(crc32)(uint32_t acc, uint32_t val, uint32_t bytes)
{
    uint8_t buf[4];

    stl_le_p(buf, val);

    /* zlib crc32 converts the accumulator and output to one's complement.  */
    return crc32(acc ^ 0xffffffff, buf, bytes) ^ 0xffffffff;
}

uint32_t HELPER(crc32c)(uint32_t acc, uint32_t val, uint32_t bytes)
{
    uint8_t buf[4];

    stl_le_p(buf, val);

    /* Linux crc32c converts the output to one's complement.  */
    return crc32c(acc, buf, bytes) ^ 0xffffffff;
}

/*
 * Return the exception level to which FP-disabled exceptions should
 * be taken, or 0 if FP is enabled.
 */
int fp_exception_el(CPUARMState *env, int cur_el)
{
#ifndef CONFIG_USER_ONLY
    uint64_t hcr_el2;

    /*
     * CPACR and the CPTR registers don't exist before v6, so FP is
     * always accessible
     */
    if (!arm_feature(env, ARM_FEATURE_V6)) {
        return 0;
    }

    if (arm_feature(env, ARM_FEATURE_M)) {
        /* CPACR can cause a NOCP UsageFault taken to current security state */
        if (!v7m_cpacr_pass(env, env->v7m.secure, cur_el != 0)) {
            return 1;
        }

        if (arm_feature(env, ARM_FEATURE_M_SECURITY) && !env->v7m.secure) {
            if (!extract32(env->v7m.nsacr, 10, 1)) {
                /* FP insns cause a NOCP UsageFault taken to Secure */
                return 3;
            }
        }

        return 0;
    }

    hcr_el2 = arm_hcr_el2_eff(env);

    /*
     * The CPACR controls traps to EL1, or PL1 if we're 32 bit:
     * 0, 2 : trap EL0 and EL1/PL1 accesses
     * 1    : trap only EL0 accesses
     * 3    : trap no accesses
     * This register is ignored if E2H+TGE are both set.
     */
    if ((hcr_el2 & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        int fpen = FIELD_EX64(env->cp15.cpacr_el1, CPACR_EL1, FPEN);

        switch (fpen) {
        case 1:
            if (cur_el != 0) {
                break;
            }
            /* fall through */
        case 0:
        case 2:
            /* Trap from Secure PL0 or PL1 to Secure PL1. */
            if (!arm_el_is_aa64(env, 3)
                && (cur_el == 3 || arm_is_secure_below_el3(env))) {
                return 3;
            }
            if (cur_el <= 1) {
                return 1;
            }
            break;
        }
    }

    /*
     * The NSACR allows A-profile AArch32 EL3 and M-profile secure mode
     * to control non-secure access to the FPU. It doesn't have any
     * effect if EL3 is AArch64 or if EL3 doesn't exist at all.
     */
    if ((arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
         cur_el <= 2 && !arm_is_secure_below_el3(env))) {
        if (!extract32(env->cp15.nsacr, 10, 1)) {
            /* FP insns act as UNDEF */
            return cur_el == 2 ? 2 : 1;
        }
    }

    /*
     * CPTR_EL2 is present in v7VE or v8, and changes format
     * with HCR_EL2.E2H (regardless of TGE).
     */
    if (cur_el <= 2) {
        if (hcr_el2 & HCR_E2H) {
            switch (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, FPEN)) {
            case 1:
                if (cur_el != 0 || !(hcr_el2 & HCR_TGE)) {
                    break;
                }
                /* fall through */
            case 0:
            case 2:
                return 2;
            }
        } else if (arm_is_el2_enabled(env)) {
            if (FIELD_EX64(env->cp15.cptr_el[2], CPTR_EL2, TFP)) {
                return 2;
            }
        }
    }

    /* CPTR_EL3 : present in v8 */
    if (FIELD_EX64(env->cp15.cptr_el[3], CPTR_EL3, TFP)) {
        /* Trap all FP ops to EL3 */
        return 3;
    }
#endif
    return 0;
}

/* Return the exception level we're running at if this is our mmu_idx */
int arm_mmu_idx_to_el(ARMMMUIdx mmu_idx)
{
    if (mmu_idx & ARM_MMU_IDX_M) {
        return mmu_idx & ARM_MMU_IDX_M_PRIV;
    }

    switch (mmu_idx) {
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E20_0:
        return 0;
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
        return 1;
    case ARMMMUIdx_E2:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
        return 2;
    case ARMMMUIdx_E3:
        return 3;
    default:
        g_assert_not_reached();
    }
}

#ifndef CONFIG_TCG
ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate)
{
    g_assert_not_reached();
}
#endif

static bool arm_pan_enabled(CPUARMState *env)
{
    if (is_a64(env)) {
        return env->pstate & PSTATE_PAN;
    } else {
        return env->uncached_cpsr & CPSR_PAN;
    }
}

ARMMMUIdx arm_mmu_idx_el(CPUARMState *env, int el)
{
    ARMMMUIdx idx;
    uint64_t hcr;

    if (arm_feature(env, ARM_FEATURE_M)) {
        return arm_v7m_mmu_idx_for_secstate(env, env->v7m.secure);
    }

    /* See ARM pseudo-function ELIsInHost.  */
    switch (el) {
    case 0:
        hcr = arm_hcr_el2_eff(env);
        if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
            idx = ARMMMUIdx_E20_0;
        } else {
            idx = ARMMMUIdx_E10_0;
        }
        break;
    case 1:
        if (arm_pan_enabled(env)) {
            idx = ARMMMUIdx_E10_1_PAN;
        } else {
            idx = ARMMMUIdx_E10_1;
        }
        break;
    case 2:
        /* Note that TGE does not apply at EL2.  */
        if (arm_hcr_el2_eff(env) & HCR_E2H) {
            if (arm_pan_enabled(env)) {
                idx = ARMMMUIdx_E20_2_PAN;
            } else {
                idx = ARMMMUIdx_E20_2;
            }
        } else {
            idx = ARMMMUIdx_E2;
        }
        break;
    case 3:
        return ARMMMUIdx_E3;
    default:
        g_assert_not_reached();
    }

    return idx;
}

ARMMMUIdx arm_mmu_idx(CPUARMState *env)
{
    return arm_mmu_idx_el(env, arm_current_el(env));
}

static CPUARMTBFlags rebuild_hflags_common(CPUARMState *env, int fp_el,
                                           ARMMMUIdx mmu_idx,
                                           CPUARMTBFlags flags)
{
    DP_TBFLAG_ANY(flags, FPEXC_EL, fp_el);
    DP_TBFLAG_ANY(flags, MMUIDX, arm_to_core_mmu_idx(mmu_idx));

    if (arm_singlestep_active(env)) {
        DP_TBFLAG_ANY(flags, SS_ACTIVE, 1);
    }
    return flags;
}

static CPUARMTBFlags rebuild_hflags_common_32(CPUARMState *env, int fp_el,
                                              ARMMMUIdx mmu_idx,
                                              CPUARMTBFlags flags)
{
    bool sctlr_b = arm_sctlr_b(env);

    if (sctlr_b) {
        DP_TBFLAG_A32(flags, SCTLR__B, 1);
    }
    if (arm_cpu_data_is_big_endian_a32(env, sctlr_b)) {
        DP_TBFLAG_ANY(flags, BE_DATA, 1);
    }
    DP_TBFLAG_A32(flags, NS, !access_secure_reg(env));

    return rebuild_hflags_common(env, fp_el, mmu_idx, flags);
}

static CPUARMTBFlags rebuild_hflags_m32(CPUARMState *env, int fp_el,
                                        ARMMMUIdx mmu_idx)
{
    CPUARMTBFlags flags = {};
    uint32_t ccr = env->v7m.ccr[env->v7m.secure];

    /* Without HaveMainExt, CCR.UNALIGN_TRP is RES1. */
    if (ccr & R_V7M_CCR_UNALIGN_TRP_MASK) {
        DP_TBFLAG_ANY(flags, ALIGN_MEM, 1);
    }

    if (arm_v7m_is_handler_mode(env)) {
        DP_TBFLAG_M32(flags, HANDLER, 1);
    }

    /*
     * v8M always applies stack limit checks unless CCR.STKOFHFNMIGN
     * is suppressing them because the requested execution priority
     * is less than 0.
     */
    if (arm_feature(env, ARM_FEATURE_V8) &&
        !((mmu_idx & ARM_MMU_IDX_M_NEGPRI) &&
          (ccr & R_V7M_CCR_STKOFHFNMIGN_MASK))) {
        DP_TBFLAG_M32(flags, STACKCHECK, 1);
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY) && env->v7m.secure) {
        DP_TBFLAG_M32(flags, SECURE, 1);
    }

    return rebuild_hflags_common_32(env, fp_el, mmu_idx, flags);
}

static CPUARMTBFlags rebuild_hflags_a32(CPUARMState *env, int fp_el,
                                        ARMMMUIdx mmu_idx)
{
    CPUARMTBFlags flags = {};
    int el = arm_current_el(env);

    if (arm_sctlr(env, el) & SCTLR_A) {
        DP_TBFLAG_ANY(flags, ALIGN_MEM, 1);
    }

    if (arm_el_is_aa64(env, 1)) {
        DP_TBFLAG_A32(flags, VFPEN, 1);
    }

    if (el < 2 && env->cp15.hstr_el2 &&
        (arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        DP_TBFLAG_A32(flags, HSTR_ACTIVE, 1);
    }

    if (env->uncached_cpsr & CPSR_IL) {
        DP_TBFLAG_ANY(flags, PSTATE__IL, 1);
    }

    /*
     * The SME exception we are testing for is raised via
     * AArch64.CheckFPAdvSIMDEnabled(), as called from
     * AArch32.CheckAdvSIMDOrFPEnabled().
     */
    if (el == 0
        && FIELD_EX64(env->svcr, SVCR, SM)
        && (!arm_is_el2_enabled(env)
            || (arm_el_is_aa64(env, 2) && !(env->cp15.hcr_el2 & HCR_TGE)))
        && arm_el_is_aa64(env, 1)
        && !sme_fa64(env, el)) {
        DP_TBFLAG_A32(flags, SME_TRAP_NONSTREAMING, 1);
    }

    return rebuild_hflags_common_32(env, fp_el, mmu_idx, flags);
}

static CPUARMTBFlags rebuild_hflags_a64(CPUARMState *env, int el, int fp_el,
                                        ARMMMUIdx mmu_idx)
{
    CPUARMTBFlags flags = {};
    ARMMMUIdx stage1 = stage_1_mmu_idx(mmu_idx);
    uint64_t tcr = regime_tcr(env, mmu_idx);
    uint64_t sctlr;
    int tbii, tbid;

    DP_TBFLAG_ANY(flags, AARCH64_STATE, 1);

    /* Get control bits for tagged addresses.  */
    tbid = aa64_va_parameter_tbi(tcr, mmu_idx);
    tbii = tbid & ~aa64_va_parameter_tbid(tcr, mmu_idx);

    DP_TBFLAG_A64(flags, TBII, tbii);
    DP_TBFLAG_A64(flags, TBID, tbid);

    if (cpu_isar_feature(aa64_sve, env_archcpu(env))) {
        int sve_el = sve_exception_el(env, el);

        /*
         * If either FP or SVE are disabled, translator does not need len.
         * If SVE EL > FP EL, FP exception has precedence, and translator
         * does not need SVE EL.  Save potential re-translations by forcing
         * the unneeded data to zero.
         */
        if (fp_el != 0) {
            if (sve_el > fp_el) {
                sve_el = 0;
            }
        } else if (sve_el == 0) {
            DP_TBFLAG_A64(flags, VL, sve_vqm1_for_el(env, el));
        }
        DP_TBFLAG_A64(flags, SVEEXC_EL, sve_el);
    }
    if (cpu_isar_feature(aa64_sme, env_archcpu(env))) {
        int sme_el = sme_exception_el(env, el);
        bool sm = FIELD_EX64(env->svcr, SVCR, SM);

        DP_TBFLAG_A64(flags, SMEEXC_EL, sme_el);
        if (sme_el == 0) {
            /* Similarly, do not compute SVL if SME is disabled. */
            int svl = sve_vqm1_for_el_sm(env, el, true);
            DP_TBFLAG_A64(flags, SVL, svl);
            if (sm) {
                /* If SVE is disabled, we will not have set VL above. */
                DP_TBFLAG_A64(flags, VL, svl);
            }
        }
        if (sm) {
            DP_TBFLAG_A64(flags, PSTATE_SM, 1);
            DP_TBFLAG_A64(flags, SME_TRAP_NONSTREAMING, !sme_fa64(env, el));
        }
        DP_TBFLAG_A64(flags, PSTATE_ZA, FIELD_EX64(env->svcr, SVCR, ZA));
    }

    sctlr = regime_sctlr(env, stage1);

    if (sctlr & SCTLR_A) {
        DP_TBFLAG_ANY(flags, ALIGN_MEM, 1);
    }

    if (arm_cpu_data_is_big_endian_a64(el, sctlr)) {
        DP_TBFLAG_ANY(flags, BE_DATA, 1);
    }

    if (cpu_isar_feature(aa64_pauth, env_archcpu(env))) {
        /*
         * In order to save space in flags, we record only whether
         * pauth is "inactive", meaning all insns are implemented as
         * a nop, or "active" when some action must be performed.
         * The decision of which action to take is left to a helper.
         */
        if (sctlr & (SCTLR_EnIA | SCTLR_EnIB | SCTLR_EnDA | SCTLR_EnDB)) {
            DP_TBFLAG_A64(flags, PAUTH_ACTIVE, 1);
        }
    }

    if (cpu_isar_feature(aa64_bti, env_archcpu(env))) {
        /* Note that SCTLR_EL[23].BT == SCTLR_BT1.  */
        if (sctlr & (el == 0 ? SCTLR_BT0 : SCTLR_BT1)) {
            DP_TBFLAG_A64(flags, BT, 1);
        }
    }

    /* Compute the condition for using AccType_UNPRIV for LDTR et al. */
    if (!(env->pstate & PSTATE_UAO)) {
        switch (mmu_idx) {
        case ARMMMUIdx_E10_1:
        case ARMMMUIdx_E10_1_PAN:
            /* TODO: ARMv8.3-NV */
            DP_TBFLAG_A64(flags, UNPRIV, 1);
            break;
        case ARMMMUIdx_E20_2:
        case ARMMMUIdx_E20_2_PAN:
            /*
             * Note that EL20_2 is gated by HCR_EL2.E2H == 1, but EL20_0 is
             * gated by HCR_EL2.<E2H,TGE> == '11', and so is LDTR.
             */
            if (env->cp15.hcr_el2 & HCR_TGE) {
                DP_TBFLAG_A64(flags, UNPRIV, 1);
            }
            break;
        default:
            break;
        }
    }

    if (env->pstate & PSTATE_IL) {
        DP_TBFLAG_ANY(flags, PSTATE__IL, 1);
    }

    if (cpu_isar_feature(aa64_mte, env_archcpu(env))) {
        /*
         * Set MTE_ACTIVE if any access may be Checked, and leave clear
         * if all accesses must be Unchecked:
         * 1) If no TBI, then there are no tags in the address to check,
         * 2) If Tag Check Override, then all accesses are Unchecked,
         * 3) If Tag Check Fail == 0, then Checked access have no effect,
         * 4) If no Allocation Tag Access, then all accesses are Unchecked.
         */
        if (allocation_tag_access_enabled(env, el, sctlr)) {
            DP_TBFLAG_A64(flags, ATA, 1);
            if (tbid
                && !(env->pstate & PSTATE_TCO)
                && (sctlr & (el == 0 ? SCTLR_TCF0 : SCTLR_TCF))) {
                DP_TBFLAG_A64(flags, MTE_ACTIVE, 1);
            }
        }
        /* And again for unprivileged accesses, if required.  */
        if (EX_TBFLAG_A64(flags, UNPRIV)
            && tbid
            && !(env->pstate & PSTATE_TCO)
            && (sctlr & SCTLR_TCF0)
            && allocation_tag_access_enabled(env, 0, sctlr)) {
            DP_TBFLAG_A64(flags, MTE0_ACTIVE, 1);
        }
        /* Cache TCMA as well as TBI. */
        DP_TBFLAG_A64(flags, TCMA, aa64_va_parameter_tcma(tcr, mmu_idx));
    }

    return rebuild_hflags_common(env, fp_el, mmu_idx, flags);
}

static CPUARMTBFlags rebuild_hflags_internal(CPUARMState *env)
{
    int el = arm_current_el(env);
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    if (is_a64(env)) {
        return rebuild_hflags_a64(env, el, fp_el, mmu_idx);
    } else if (arm_feature(env, ARM_FEATURE_M)) {
        return rebuild_hflags_m32(env, fp_el, mmu_idx);
    } else {
        return rebuild_hflags_a32(env, fp_el, mmu_idx);
    }
}

void arm_rebuild_hflags(CPUARMState *env)
{
    env->hflags = rebuild_hflags_internal(env);
}

/*
 * If we have triggered a EL state change we can't rely on the
 * translator having passed it to us, we need to recompute.
 */
void HELPER(rebuild_hflags_m32_newel)(CPUARMState *env)
{
    int el = arm_current_el(env);
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_m32(env, fp_el, mmu_idx);
}

void HELPER(rebuild_hflags_m32)(CPUARMState *env, int el)
{
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_m32(env, fp_el, mmu_idx);
}

/*
 * If we have triggered a EL state change we can't rely on the
 * translator having passed it to us, we need to recompute.
 */
void HELPER(rebuild_hflags_a32_newel)(CPUARMState *env)
{
    int el = arm_current_el(env);
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);
    env->hflags = rebuild_hflags_a32(env, fp_el, mmu_idx);
}

void HELPER(rebuild_hflags_a32)(CPUARMState *env, int el)
{
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_a32(env, fp_el, mmu_idx);
}

void HELPER(rebuild_hflags_a64)(CPUARMState *env, int el)
{
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_a64(env, el, fp_el, mmu_idx);
}

static inline void assert_hflags_rebuild_correctly(CPUARMState *env)
{
#ifdef CONFIG_DEBUG_TCG
    CPUARMTBFlags c = env->hflags;
    CPUARMTBFlags r = rebuild_hflags_internal(env);

    if (unlikely(c.flags != r.flags || c.flags2 != r.flags2)) {
        fprintf(stderr, "TCG hflags mismatch "
                        "(current:(0x%08x,0x" TARGET_FMT_lx ")"
                        " rebuilt:(0x%08x,0x" TARGET_FMT_lx ")\n",
                c.flags, c.flags2, r.flags, r.flags2);
        abort();
    }
#endif
}

static bool mve_no_pred(CPUARMState *env)
{
    /*
     * Return true if there is definitely no predication of MVE
     * instructions by VPR or LTPSIZE. (Returning false even if there
     * isn't any predication is OK; generated code will just be
     * a little worse.)
     * If the CPU does not implement MVE then this TB flag is always 0.
     *
     * NOTE: if you change this logic, the "recalculate s->mve_no_pred"
     * logic in gen_update_fp_context() needs to be updated to match.
     *
     * We do not include the effect of the ECI bits here -- they are
     * tracked in other TB flags. This simplifies the logic for
     * "when did we emit code that changes the MVE_NO_PRED TB flag
     * and thus need to end the TB?".
     */
    if (cpu_isar_feature(aa32_mve, env_archcpu(env))) {
        return false;
    }
    if (env->v7m.vpr) {
        return false;
    }
    if (env->v7m.ltpsize < 4) {
        return false;
    }
    return true;
}

void cpu_get_tb_cpu_state(CPUARMState *env, target_ulong *pc,
                          target_ulong *cs_base, uint32_t *pflags)
{
    CPUARMTBFlags flags;

    assert_hflags_rebuild_correctly(env);
    flags = env->hflags;

    if (EX_TBFLAG_ANY(flags, AARCH64_STATE)) {
        *pc = env->pc;
        if (cpu_isar_feature(aa64_bti, env_archcpu(env))) {
            DP_TBFLAG_A64(flags, BTYPE, env->btype);
        }
    } else {
        *pc = env->regs[15];

        if (arm_feature(env, ARM_FEATURE_M)) {
            if (arm_feature(env, ARM_FEATURE_M_SECURITY) &&
                FIELD_EX32(env->v7m.fpccr[M_REG_S], V7M_FPCCR, S)
                != env->v7m.secure) {
                DP_TBFLAG_M32(flags, FPCCR_S_WRONG, 1);
            }

            if ((env->v7m.fpccr[env->v7m.secure] & R_V7M_FPCCR_ASPEN_MASK) &&
                (!(env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK) ||
                 (env->v7m.secure &&
                  !(env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK)))) {
                /*
                 * ASPEN is set, but FPCA/SFPA indicate that there is no
                 * active FP context; we must create a new FP context before
                 * executing any FP insn.
                 */
                DP_TBFLAG_M32(flags, NEW_FP_CTXT_NEEDED, 1);
            }

            bool is_secure = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_S_MASK;
            if (env->v7m.fpccr[is_secure] & R_V7M_FPCCR_LSPACT_MASK) {
                DP_TBFLAG_M32(flags, LSPACT, 1);
            }

            if (mve_no_pred(env)) {
                DP_TBFLAG_M32(flags, MVE_NO_PRED, 1);
            }
        } else {
            /*
             * Note that XSCALE_CPAR shares bits with VECSTRIDE.
             * Note that VECLEN+VECSTRIDE are RES0 for M-profile.
             */
            if (arm_feature(env, ARM_FEATURE_XSCALE)) {
                DP_TBFLAG_A32(flags, XSCALE_CPAR, env->cp15.c15_cpar);
            } else {
                DP_TBFLAG_A32(flags, VECLEN, env->vfp.vec_len);
                DP_TBFLAG_A32(flags, VECSTRIDE, env->vfp.vec_stride);
            }
            if (env->vfp.xregs[ARM_VFP_FPEXC] & (1 << 30)) {
                DP_TBFLAG_A32(flags, VFPEN, 1);
            }
        }

        DP_TBFLAG_AM32(flags, THUMB, env->thumb);
        DP_TBFLAG_AM32(flags, CONDEXEC, env->condexec_bits);
    }

    /*
     * The SS_ACTIVE and PSTATE_SS bits correspond to the state machine
     * states defined in the ARM ARM for software singlestep:
     *  SS_ACTIVE   PSTATE.SS   State
     *     0            x       Inactive (the TB flag for SS is always 0)
     *     1            0       Active-pending
     *     1            1       Active-not-pending
     * SS_ACTIVE is set in hflags; PSTATE__SS is computed every TB.
     */
    if (EX_TBFLAG_ANY(flags, SS_ACTIVE) && (env->pstate & PSTATE_SS)) {
        DP_TBFLAG_ANY(flags, PSTATE__SS, 1);
    }

    *pflags = flags.flags;
    *cs_base = flags.flags2;
}

#ifdef TARGET_AARCH64
/*
 * The manual says that when SVE is enabled and VQ is widened the
 * implementation is allowed to zero the previously inaccessible
 * portion of the registers.  The corollary to that is that when
 * SVE is enabled and VQ is narrowed we are also allowed to zero
 * the now inaccessible portion of the registers.
 *
 * The intent of this is that no predicate bit beyond VQ is ever set.
 * Which means that some operations on predicate registers themselves
 * may operate on full uint64_t or even unrolled across the maximum
 * uint64_t[4].  Performing 4 bits of host arithmetic unconditionally
 * may well be cheaper than conditionals to restrict the operation
 * to the relevant portion of a uint16_t[16].
 */
void aarch64_sve_narrow_vq(CPUARMState *env, unsigned vq)
{
    int i, j;
    uint64_t pmask;

    assert(vq >= 1 && vq <= ARM_MAX_VQ);
    assert(vq <= env_archcpu(env)->sve_max_vq);

    /* Zap the high bits of the zregs.  */
    for (i = 0; i < 32; i++) {
        memset(&env->vfp.zregs[i].d[2 * vq], 0, 16 * (ARM_MAX_VQ - vq));
    }

    /* Zap the high bits of the pregs and ffr.  */
    pmask = 0;
    if (vq & 3) {
        pmask = ~(-1ULL << (16 * (vq & 3)));
    }
    for (j = vq / 4; j < ARM_MAX_VQ / 4; j++) {
        for (i = 0; i < 17; ++i) {
            env->vfp.pregs[i].p[j] &= pmask;
        }
        pmask = 0;
    }
}

static uint32_t sve_vqm1_for_el_sm_ena(CPUARMState *env, int el, bool sm)
{
    int exc_el;

    if (sm) {
        exc_el = sme_exception_el(env, el);
    } else {
        exc_el = sve_exception_el(env, el);
    }
    if (exc_el) {
        return 0; /* disabled */
    }
    return sve_vqm1_for_el_sm(env, el, sm);
}

/*
 * Notice a change in SVE vector size when changing EL.
 */
void aarch64_sve_change_el(CPUARMState *env, int old_el,
                           int new_el, bool el0_a64)
{
    ARMCPU *cpu = env_archcpu(env);
    int old_len, new_len;
    bool old_a64, new_a64, sm;

    /* Nothing to do if no SVE.  */
    if (!cpu_isar_feature(aa64_sve, cpu)) {
        return;
    }

    /* Nothing to do if FP is disabled in either EL.  */
    if (fp_exception_el(env, old_el) || fp_exception_el(env, new_el)) {
        return;
    }

    old_a64 = old_el ? arm_el_is_aa64(env, old_el) : el0_a64;
    new_a64 = new_el ? arm_el_is_aa64(env, new_el) : el0_a64;

    /*
     * Both AArch64.TakeException and AArch64.ExceptionReturn
     * invoke ResetSVEState when taking an exception from, or
     * returning to, AArch32 state when PSTATE.SM is enabled.
     */
    sm = FIELD_EX64(env->svcr, SVCR, SM);
    if (old_a64 != new_a64 && sm) {
        arm_reset_sve_state(env);
        return;
    }

    /*
     * DDI0584A.d sec 3.2: "If SVE instructions are disabled or trapped
     * at ELx, or not available because the EL is in AArch32 state, then
     * for all purposes other than a direct read, the ZCR_ELx.LEN field
     * has an effective value of 0".
     *
     * Consider EL2 (aa64, vq=4) -> EL0 (aa32) -> EL1 (aa64, vq=0).
     * If we ignore aa32 state, we would fail to see the vq4->vq0 transition
     * from EL2->EL1.  Thus we go ahead and narrow when entering aa32 so that
     * we already have the correct register contents when encountering the
     * vq0->vq0 transition between EL0->EL1.
     */
    old_len = new_len = 0;
    if (old_a64) {
        old_len = sve_vqm1_for_el_sm_ena(env, old_el, sm);
    }
    if (new_a64) {
        new_len = sve_vqm1_for_el_sm_ena(env, new_el, sm);
    }

    /* When changing vector length, clear inaccessible state.  */
    if (new_len < old_len) {
        aarch64_sve_narrow_vq(env, new_len + 1);
    }
}
#endif
