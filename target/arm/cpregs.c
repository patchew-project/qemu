/*
 * ARM CP registers - common functionality
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpregs.h"

static bool raw_accessors_invalid(const ARMCPRegInfo *ri)
{
   /*
    * Return true if the regdef would cause an assertion if you called
    * read_raw_cp_reg() or write_raw_cp_reg() on it (ie if it is a
    * program bug for it not to have the NO_RAW flag).
    * NB that returning false here doesn't necessarily mean that calling
    * read/write_raw_cp_reg() is safe, because we can't distinguish "has
    * read/write access functions which are safe for raw use" from "has
    * read/write access functions which have side effects but has forgotten
    * to provide raw access functions".
    * The tests here line up with the conditions in read/write_raw_cp_reg()
    * and assertions in raw_read()/raw_write().
    */
    if ((ri->type & ARM_CP_CONST) ||
        ri->fieldoffset ||
        ((ri->raw_writefn || ri->writefn) && (ri->raw_readfn || ri->readfn))) {
        return false;
    }
    return true;
}

/*
 * Private utility function for define_one_arm_cp_reg_with_opaque():
 * add a single reginfo struct to the hash table.
 */
static void add_cpreg_to_hashtable(ARMCPU *cpu, const ARMCPRegInfo *r,
                                   void *opaque, CPState state,
                                   CPSecureState secstate,
                                   int crm, int opc1, int opc2,
                                   const char *name)
{
    CPUARMState *env = &cpu->env;
    uint32_t key;
    ARMCPRegInfo *r2;
    bool is64 = r->type & ARM_CP_64BIT;
    bool ns = secstate & ARM_CP_SECSTATE_NS;
    int cp = r->cp;
    size_t name_len;
    bool make_const;

    switch (state) {
    case ARM_CP_STATE_AA32:
        /* We assume it is a cp15 register if the .cp field is left unset. */
        if (cp == 0 && r->state == ARM_CP_STATE_BOTH) {
            cp = 15;
        }
        key = ENCODE_CP_REG(cp, is64, ns, r->crn, crm, opc1, opc2);
        break;
    case ARM_CP_STATE_AA64:
        /*
         * To allow abbreviation of ARMCPRegInfo definitions, we treat
         * cp == 0 as equivalent to the value for "standard guest-visible
         * sysreg".  STATE_BOTH definitions are also always "standard sysreg"
         * in their AArch64 view (the .cp value may be non-zero for the
         * benefit of the AArch32 view).
         */
        if (cp == 0 || r->state == ARM_CP_STATE_BOTH) {
            cp = CP_REG_ARM64_SYSREG_CP;
        }
        key = ENCODE_AA64_CP_REG(cp, r->crn, crm, r->opc0, opc1, opc2);
        break;
    default:
        g_assert_not_reached();
    }

    /* Overriding of an existing definition must be explicitly requested. */
    if (!(r->type & ARM_CP_OVERRIDE)) {
        const ARMCPRegInfo *oldreg = get_arm_cp_reginfo(cpu->cp_regs, key);
        if (oldreg) {
            assert(oldreg->type & ARM_CP_OVERRIDE);
        }
    }

    /*
     * Eliminate registers that are not present because the EL is missing.
     * Doing this here makes it easier to put all registers for a given
     * feature into the same ARMCPRegInfo array and define them all at once.
     */
    make_const = false;
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        /*
         * An EL2 register without EL2 but with EL3 is (usually) RES0.
         * See rule RJFFP in section D1.1.3 of DDI0487H.a.
         */
        int min_el = ctz32(r->access) / 2;
        if (min_el == 2 && !arm_feature(env, ARM_FEATURE_EL2)) {
            if (r->type & ARM_CP_EL3_NO_EL2_UNDEF) {
                return;
            }
            make_const = !(r->type & ARM_CP_EL3_NO_EL2_KEEP);
        }
    } else {
        CPAccessRights max_el = (arm_feature(env, ARM_FEATURE_EL2)
                                 ? PL2_RW : PL1_RW);
        if ((r->access & max_el) == 0) {
            return;
        }
    }

    /* Combine cpreg and name into one allocation. */
    name_len = strlen(name) + 1;
    r2 = g_malloc(sizeof(*r2) + name_len);
    *r2 = *r;
    r2->name = memcpy(r2 + 1, name, name_len);

    /*
     * Update fields to match the instantiation, overwiting wildcards
     * such as CP_ANY, ARM_CP_STATE_BOTH, or ARM_CP_SECSTATE_BOTH.
     */
    r2->cp = cp;
    r2->crm = crm;
    r2->opc1 = opc1;
    r2->opc2 = opc2;
    r2->state = state;
    r2->secure = secstate;
    if (opaque) {
        r2->opaque = opaque;
    }

    if (make_const) {
        /* This should not have been a very special register to begin. */
        int old_special = r2->type & ARM_CP_SPECIAL_MASK;
        assert(old_special == 0 || old_special == ARM_CP_NOP);
        /*
         * Set the special function to CONST, retaining the other flags.
         * This is important for e.g. ARM_CP_SVE so that we still
         * take the SVE trap if CPTR_EL3.EZ == 0.
         */
        r2->type = (r2->type & ~ARM_CP_SPECIAL_MASK) | ARM_CP_CONST;
        /*
         * Usually, these registers become RES0, but there are a few
         * special cases like VPIDR_EL2 which have a constant non-zero
         * value with writes ignored.
         */
        if (!(r->type & ARM_CP_EL3_NO_EL2_C_NZ)) {
            r2->resetvalue = 0;
        }
        /*
         * ARM_CP_CONST has precedence, so removing the callbacks and
         * offsets are not strictly necessary, but it is potentially
         * less confusing to debug later.
         */
        r2->readfn = NULL;
        r2->writefn = NULL;
        r2->raw_readfn = NULL;
        r2->raw_writefn = NULL;
        r2->resetfn = NULL;
        r2->fieldoffset = 0;
        r2->bank_fieldoffsets[0] = 0;
        r2->bank_fieldoffsets[1] = 0;
    } else {
        bool isbanked = r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1];

        if (isbanked) {
            /*
             * Register is banked (using both entries in array).
             * Overwriting fieldoffset as the array is only used to define
             * banked registers but later only fieldoffset is used.
             */
            r2->fieldoffset = r->bank_fieldoffsets[ns];
        }
        if (state == ARM_CP_STATE_AA32) {
            if (isbanked) {
                /*
                 * If the register is banked then we don't need to migrate or
                 * reset the 32-bit instance in certain cases:
                 *
                 * 1) If the register has both 32-bit and 64-bit instances
                 *    then we can count on the 64-bit instance taking care
                 *    of the non-secure bank.
                 * 2) If ARMv8 is enabled then we can count on a 64-bit
                 *    version taking care of the secure bank.  This requires
                 *    that separate 32 and 64-bit definitions are provided.
                 */
                if ((r->state == ARM_CP_STATE_BOTH && ns) ||
                    (arm_feature(env, ARM_FEATURE_V8) && !ns)) {
                    r2->type |= ARM_CP_ALIAS;
                }
            } else if ((secstate != r->secure) && !ns) {
                /*
                 * The register is not banked so we only want to allow
                 * migration of the non-secure instance.
                 */
                r2->type |= ARM_CP_ALIAS;
            }

            if (HOST_BIG_ENDIAN &&
                r->state == ARM_CP_STATE_BOTH && r2->fieldoffset) {
                r2->fieldoffset += sizeof(uint32_t);
            }
        }
    }

    /*
     * By convention, for wildcarded registers only the first
     * entry is used for migration; the others are marked as
     * ALIAS so we don't try to transfer the register
     * multiple times. Special registers (ie NOP/WFI) are
     * never migratable and not even raw-accessible.
     */
    if (r2->type & ARM_CP_SPECIAL_MASK) {
        r2->type |= ARM_CP_NO_RAW;
    }
    if (((r->crm == CP_ANY) && crm != 0) ||
        ((r->opc1 == CP_ANY) && opc1 != 0) ||
        ((r->opc2 == CP_ANY) && opc2 != 0)) {
        r2->type |= ARM_CP_ALIAS | ARM_CP_NO_GDB;
    }

    /*
     * Check that raw accesses are either forbidden or handled. Note that
     * we can't assert this earlier because the setup of fieldoffset for
     * banked registers has to be done first.
     */
    if (!(r2->type & ARM_CP_NO_RAW)) {
        assert(!raw_accessors_invalid(r2));
    }

    g_hash_table_insert(cpu->cp_regs, (gpointer)(uintptr_t)key, r2);
}

void define_one_arm_cp_reg_with_opaque(ARMCPU *cpu,
                                       const ARMCPRegInfo *r, void *opaque)
{
    /*
     * Define implementations of coprocessor registers.
     * We store these in a hashtable because typically
     * there are less than 150 registers in a space which
     * is 16*16*16*8*8 = 262144 in size.
     * Wildcarding is supported for the crm, opc1 and opc2 fields.
     * If a register is defined twice then the second definition is
     * used, so this can be used to define some generic registers and
     * then override them with implementation specific variations.
     * At least one of the original and the second definition should
     * include ARM_CP_OVERRIDE in its type bits -- this is just a guard
     * against accidental use.
     *
     * The state field defines whether the register is to be
     * visible in the AArch32 or AArch64 execution state. If the
     * state is set to ARM_CP_STATE_BOTH then we synthesise a
     * reginfo structure for the AArch32 view, which sees the lower
     * 32 bits of the 64 bit register.
     *
     * Only registers visible in AArch64 may set r->opc0; opc0 cannot
     * be wildcarded. AArch64 registers are always considered to be 64
     * bits; the ARM_CP_64BIT* flag applies only to the AArch32 view of
     * the register, if any.
     */
    int crm, opc1, opc2;
    int crmmin = (r->crm == CP_ANY) ? 0 : r->crm;
    int crmmax = (r->crm == CP_ANY) ? 15 : r->crm;
    int opc1min = (r->opc1 == CP_ANY) ? 0 : r->opc1;
    int opc1max = (r->opc1 == CP_ANY) ? 7 : r->opc1;
    int opc2min = (r->opc2 == CP_ANY) ? 0 : r->opc2;
    int opc2max = (r->opc2 == CP_ANY) ? 7 : r->opc2;
    CPState state;

    /* 64 bit registers have only CRm and Opc1 fields */
    assert(!((r->type & ARM_CP_64BIT) && (r->opc2 || r->crn)));
    /* op0 only exists in the AArch64 encodings */
    assert((r->state != ARM_CP_STATE_AA32) || (r->opc0 == 0));
    /* AArch64 regs are all 64 bit so ARM_CP_64BIT is meaningless */
    assert((r->state != ARM_CP_STATE_AA64) || !(r->type & ARM_CP_64BIT));
    /*
     * This API is only for Arm's system coprocessors (14 and 15) or
     * (M-profile or v7A-and-earlier only) for implementation defined
     * coprocessors in the range 0..7.  Our decode assumes this, since
     * 8..13 can be used for other insns including VFP and Neon. See
     * valid_cp() in translate.c.  Assert here that we haven't tried
     * to use an invalid coprocessor number.
     */
    switch (r->state) {
    case ARM_CP_STATE_BOTH:
        /* 0 has a special meaning, but otherwise the same rules as AA32. */
        if (r->cp == 0) {
            break;
        }
        /* fall through */
    case ARM_CP_STATE_AA32:
        if (arm_feature(&cpu->env, ARM_FEATURE_V8) &&
            !arm_feature(&cpu->env, ARM_FEATURE_M)) {
            assert(r->cp >= 14 && r->cp <= 15);
        } else {
            assert(r->cp < 8 || (r->cp >= 14 && r->cp <= 15));
        }
        break;
    case ARM_CP_STATE_AA64:
        assert(r->cp == 0 || r->cp == CP_REG_ARM64_SYSREG_CP);
        break;
    default:
        g_assert_not_reached();
    }
    /*
     * The AArch64 pseudocode CheckSystemAccess() specifies that op1
     * encodes a minimum access level for the register. We roll this
     * runtime check into our general permission check code, so check
     * here that the reginfo's specified permissions are strict enough
     * to encompass the generic architectural permission check.
     */
    if (r->state != ARM_CP_STATE_AA32) {
        CPAccessRights mask;
        switch (r->opc1) {
        case 0:
            /* min_EL EL1, but some accessible to EL0 via kernel ABI */
            mask = PL0U_R | PL1_RW;
            break;
        case 1: case 2:
            /* min_EL EL1 */
            mask = PL1_RW;
            break;
        case 3:
            /* min_EL EL0 */
            mask = PL0_RW;
            break;
        case 4:
        case 5:
            /* min_EL EL2 */
            mask = PL2_RW;
            break;
        case 6:
            /* min_EL EL3 */
            mask = PL3_RW;
            break;
        case 7:
            /* min_EL EL1, secure mode only (we don't check the latter) */
            mask = PL1_RW;
            break;
        default:
            /* broken reginfo with out-of-range opc1 */
            g_assert_not_reached();
        }
        /* assert our permissions are not too lax (stricter is fine) */
        assert((r->access & ~mask) == 0);
    }

    /*
     * Check that the register definition has enough info to handle
     * reads and writes if they are permitted.
     */
    if (!(r->type & (ARM_CP_SPECIAL_MASK | ARM_CP_CONST))) {
        if (r->access & PL3_R) {
            assert((r->fieldoffset ||
                   (r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1])) ||
                   r->readfn);
        }
        if (r->access & PL3_W) {
            assert((r->fieldoffset ||
                   (r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1])) ||
                   r->writefn);
        }
    }

    for (crm = crmmin; crm <= crmmax; crm++) {
        for (opc1 = opc1min; opc1 <= opc1max; opc1++) {
            for (opc2 = opc2min; opc2 <= opc2max; opc2++) {
                for (state = ARM_CP_STATE_AA32;
                     state <= ARM_CP_STATE_AA64; state++) {
                    if (r->state != state && r->state != ARM_CP_STATE_BOTH) {
                        continue;
                    }
                    if (state == ARM_CP_STATE_AA32) {
                        /*
                         * Under AArch32 CP registers can be common
                         * (same for secure and non-secure world) or banked.
                         */
                        char *name;

                        switch (r->secure) {
                        case ARM_CP_SECSTATE_S:
                        case ARM_CP_SECSTATE_NS:
                            add_cpreg_to_hashtable(cpu, r, opaque, state,
                                                   r->secure, crm, opc1, opc2,
                                                   r->name);
                            break;
                        case ARM_CP_SECSTATE_BOTH:
                            name = g_strdup_printf("%s_S", r->name);
                            add_cpreg_to_hashtable(cpu, r, opaque, state,
                                                   ARM_CP_SECSTATE_S,
                                                   crm, opc1, opc2, name);
                            g_free(name);
                            add_cpreg_to_hashtable(cpu, r, opaque, state,
                                                   ARM_CP_SECSTATE_NS,
                                                   crm, opc1, opc2, r->name);
                            break;
                        default:
                            g_assert_not_reached();
                        }
                    } else {
                        /*
                         * AArch64 registers get mapped to non-secure instance
                         * of AArch32
                         */
                        add_cpreg_to_hashtable(cpu, r, opaque, state,
                                               ARM_CP_SECSTATE_NS,
                                               crm, opc1, opc2, r->name);
                    }
                }
            }
        }
    }
}

/* Define a whole list of registers */
void define_arm_cp_regs_with_opaque_len(ARMCPU *cpu, const ARMCPRegInfo *regs,
                                        void *opaque, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        define_one_arm_cp_reg_with_opaque(cpu, regs + i, opaque);
    }
}

/*
 * Modify ARMCPRegInfo for access from userspace.
 *
 * This is a data driven modification directed by
 * ARMCPRegUserSpaceInfo. All registers become ARM_CP_CONST as
 * user-space cannot alter any values and dynamic values pertaining to
 * execution state are hidden from user space view anyway.
 */
void modify_arm_cp_regs_with_len(ARMCPRegInfo *regs, size_t regs_len,
                                 const ARMCPRegUserSpaceInfo *mods,
                                 size_t mods_len)
{
    for (size_t mi = 0; mi < mods_len; ++mi) {
        const ARMCPRegUserSpaceInfo *m = mods + mi;
        GPatternSpec *pat = NULL;

        if (m->is_glob) {
            pat = g_pattern_spec_new(m->name);
        }
        for (size_t ri = 0; ri < regs_len; ++ri) {
            ARMCPRegInfo *r = regs + ri;

            if (pat && g_pattern_match_string(pat, r->name)) {
                r->type = ARM_CP_CONST;
                r->access = PL0U_R;
                r->resetvalue = 0;
                /* continue */
            } else if (strcmp(r->name, m->name) == 0) {
                r->type = ARM_CP_CONST;
                r->access = PL0U_R;
                r->resetvalue &= m->exported_bits;
                r->resetvalue |= m->fixed_bits;
                break;
            }
        }
        if (pat) {
            g_pattern_spec_free(pat);
        }
    }
}

void arm_cp_write_ignore(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    /* Helper coprocessor write function for write-ignore registers */
}

uint64_t arm_cp_read_zero(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Helper coprocessor write function for read-as-zero registers */
    return 0;
}

void arm_cp_reset_ignore(CPUARMState *env, const ARMCPRegInfo *opaque)
{
    /* Helper coprocessor reset function for do-nothing-on-reset registers */
}
