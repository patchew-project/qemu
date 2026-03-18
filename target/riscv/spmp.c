/*
 * QEMU RISC-V SPMP (S-mode Physical Memory Protection)
 *
 * Author:
 *   Luís Cunha <luisccunha8@gmail.com>
 *
 * Based on an earlier SPMP prototype by:
 *   Bicheng Yang <SuperYbc@outlook.com>
 *   Dong Du      <Ddnirvana1@gmail.com>
 *
 * This provides a RISC-V S-mode Physical Memory Protection interface.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "cpu.h"
#include "cpu_bits.h"
#include "trace.h"
#include "exec/target_page.h"

/*
 * Accessor method to extract address matching type 'a field' from cfg reg
 */
uint8_t spmp_get_a_field(uint8_t cfg)
{
    uint8_t a = cfg >> 3;
    return a & 0x3;
}

/*
 * Check whether mstatus.sum is set.
 */
static inline int sum_is_set(CPURISCVState *env)
{
    if (env->mstatus & MSTATUS_SUM) {
        return 1;
    }

    return 0;
}

void spmp_decode_napot(target_ulong a, target_ulong *sa, target_ulong *ea)
{
    /*
     * aaaa...aaa0   8-byte NAPOT range
     * aaaa...aa01   16-byte NAPOT range
     * aaaa...a011   32-byte NAPOT range
     * ...
     * aa01...1111   2^XLEN-byte NAPOT range
     * a011...1111   2^(XLEN+1)-byte NAPOT range
     * 0111...1111   2^(XLEN+2)-byte NAPOT range
     * 1111...1111   Reserved
     */
    a = (a << 2) | 0x3;
    *sa = a & (a + 1);
    *ea = a | (a + 1);
}

static void spmp_update_rule_addr(CPURISCVState *env, uint32_t spmp_index)
{
    uint8_t this_cfg = env->spmp_state.spmp[spmp_index].cfg_reg;
    target_ulong this_addr = env->spmp_state.spmp[spmp_index].addr_reg;
    target_ulong prev_addr = 0u;
    target_ulong sa = 0u;
    target_ulong ea = 0u;

    if (spmp_index >= 1u) {
        prev_addr = env->spmp_state.spmp[spmp_index - 1].addr_reg;
    }

    switch (spmp_get_a_field(this_cfg)) {
    case SPMP_AMATCH_OFF:
        sa = 0u;
        ea = -1;
        break;

    case SPMP_AMATCH_TOR:
        sa = prev_addr << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = (this_addr << 2) - 1u;
        break;

    case SPMP_AMATCH_NA4:
        sa = this_addr << 2; /* shift up from [xx:0] to [xx+2:2] */
        ea = (sa + 4u) - 1u;
        break;

    case SPMP_AMATCH_NAPOT:
        spmp_decode_napot(this_addr, &sa, &ea);
        break;

    default:
        sa = 0u;
        ea = 0u;
        break;
    }

    env->spmp_state.addr[spmp_index].sa = sa;
    env->spmp_state.addr[spmp_index].ea = ea;
}

static void spmp_update_rule_nums(CPURISCVState *env)
{
    int i;

    env->spmp_state.num_active_rules = 0;
    for (i = 0; i < MAX_RISCV_SPMPS; i++) {
        const uint8_t a_field =
            spmp_get_a_field(env->spmp_state.spmp[i].cfg_reg);
        if (SPMP_AMATCH_OFF != a_field) {
            env->spmp_state.num_active_rules++;
        }
    }
}

/*
 * Convert cfg/addr reg values here into simple 'sa' --> start address and 'ea'
 * end address values.
 * This function is called relatively infrequently whereas the check that
 * an address is within a spmp rule is called often, so optimise that one
 */
static void spmp_update_rule(CPURISCVState *env, uint32_t spmp_index)
{
    spmp_update_rule_addr(env, spmp_index);
    spmp_update_rule_nums(env);
}

static uint8_t spmp_is_in_range(CPURISCVState *env, int spmp_index,
                                target_ulong addr)
{
    if ((addr >= env->spmp_state.addr[spmp_index].sa)
        && (addr <= env->spmp_state.addr[spmp_index].ea)) {
        return 1;
    }

    return 0;
}

static bool spmp_get_spmpen_bit(CPURISCVState *env, int index)
{
    if (!riscv_cpu_cfg(env)->ext_sspmpen) {
        return true;
    }

    return (env->spmp_state.spmpen >> index) & 0x1;
}

/*
 * Check if the address has required RWX privs when no SPMP entry is matched.
 */
static bool spmp_hart_has_privs_default(CPURISCVState *env, target_ulong addr,
                                        spmp_priv_t *allowed_privs,
                                        target_ulong mode)
{
    bool ret;
    mode = env->virt_enabled ? PRV_U : mode;

    if ((!riscv_cpu_cfg(env)->spmp) || !(mode == PRV_U)) {
        /*
         * The SPMP proposal states three circumstances that the access
         * is allowed:
         * 1. The HW does not implement any SPMP entry.
         * 2. If the effective privilege mode of the access is S and no SPMP
         * entry matches
         * 3. The access mode is M.
         */
        ret = true;
        *allowed_privs = SPMP_READ | SPMP_WRITE | SPMP_EXEC;
    } else {
        /*
         * U-mode is not allowed to succeed if they don't match a rule,
         * but there are rules. We've checked for those rules earlier in this
         * function.
         */
        ret = false;
        *allowed_privs = 0;
    }

    return ret;
}


/*
 * Public Interface
 */

/*
 * Check if the address has required RWX privs to complete desired operation
 */
bool spmp_hart_has_privs(CPURISCVState *env, target_ulong addr,
    target_ulong size, spmp_priv_t privs, spmp_priv_t *allowed_privs,
    target_ulong mode)
{
    int i = 0;
    int ret = -1;
    int spmp_size = 0;
    target_ulong s = 0;
    target_ulong e = 0;
    bool spmpen_en = false;

    /* If it is either VS or VU mode, we treat it as U mode */
    mode = env->virt_enabled ? PRV_U : mode;

    /* Short cut for M-mode access */
    if (mode == PRV_M) {
        *allowed_privs = SPMP_READ | SPMP_WRITE | SPMP_EXEC;
        return true;
    }

    /* Short cut if no rules */
    if (env->spmp_state.num_active_rules == 0) {
        return spmp_hart_has_privs_default(env, addr, allowed_privs, mode);
    }

    if (size == 0) {
        if (riscv_cpu_cfg(env)->mmu) {
            /*
             * If size is unknown (0), assume that all bytes
             * from addr to the end of the page will be accessed.
             */
            spmp_size = -(addr | TARGET_PAGE_MASK);
        } else {
            spmp_size = sizeof(target_ulong);
        }
    } else {
        spmp_size = size;
    }

    /* It depends on mpmpdeleg */
    for (i = 0; i < env->spmp_state.num_deleg_rules; i++) {
        s = spmp_is_in_range(env, i, addr);
        e = spmp_is_in_range(env, i, addr + spmp_size - 1);
        spmpen_en = spmp_get_spmpen_bit(env, i);

        /* partially inside */
        if ((s + e) == 1) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: spmp violation - access is partially inside\n",
                          __func__);
            ret = 0;
            break;
        }

        /* fully inside */
        const uint8_t a_field =
            spmp_get_a_field(env->spmp_state.spmp[i].cfg_reg);

        /*
         * Convert the SPMP permissions to match the truth table in the
         * SPMP spec.
         */
        const uint8_t spmp_operation =
                                (env->spmp_state.spmp[i].cfg_reg & SPMP_EXEC)
                                | (env->spmp_state.spmp[i].cfg_reg & SPMP_WRITE)
                                | (env->spmp_state.spmp[i].cfg_reg & SPMP_READ);

        if (((s + e) == 2) && (SPMP_AMATCH_OFF != a_field) && spmpen_en) {
            /*
             * If the SPMP entry is not off, spmpen bit is set, and the address
             * is in range, do the priv check
             */

            /* Shared not set */
            if (!(env->spmp_state.spmp[i].cfg_reg & SPMP_SHARED)) {
                /*
                 *   Deny if:
                 *   S mode access, with SUM not set, and UMODE set.
                 *   U mode access, with UMODE not set.
                 */
                if ((mode == PRV_S && !sum_is_set(env) &&
                    (env->spmp_state.spmp[i].cfg_reg & SPMP_UMODE)) ||
                    (mode == PRV_U &&
                        !(env->spmp_state.spmp[i].cfg_reg & SPMP_UMODE))) {
                    *allowed_privs = 0;
                }
                /*
                 *   EnforceNoX if:
                 *   S mode access, with SUM set, and UMODE set.
                 *   Note: The specification has the table in RWX, the oposite
                 *   of the order in the cfg reg.
                 */
                else if (mode == PRV_S && sum_is_set(env) &&
                        (env->spmp_state.spmp[i].cfg_reg & SPMP_UMODE)) {
                    switch (spmp_operation) {
                    case 0:
                    case 2:
                    case 4:
                    case 6:
                        *allowed_privs = 0;
                        break;
                    case 1:
                    case 5:
                        *allowed_privs = SPMP_READ;
                        break;
                    case 3:
                    case 7:
                        *allowed_privs = SPMP_READ | SPMP_WRITE;
                        break;
                    default:
                        g_assert_not_reached();
                    }
                } else {
                    /* Check for reserved configs */
                    if (spmp_operation == 2 || spmp_operation == 6) {
                        *allowed_privs = 0;
                    } else {
                        /* U mode falls here - Enforce */
                        *allowed_privs = spmp_operation & 0x7;
                    }
                }
            }
            /* Set Shared bit */
            else {
                if (mode == PRV_S) {
                    /* Check for reserved configs */
                    if (spmp_operation == 2 || spmp_operation == 6) {
                        *allowed_privs = 0;
                    } else {
                        *allowed_privs = spmp_operation & 0x7;
                    }
                } else {
                    switch (spmp_operation) {
                    case 0:
                    case 2:
                    case 6:
                        *allowed_privs = 0;
                        break;
                    case 1:
                    case 3:
                        *allowed_privs = SPMP_READ;
                        break;
                    case 4:
                    case 7:
                        *allowed_privs = SPMP_EXEC;
                        break;
                    case 5:
                        *allowed_privs = SPMP_READ | SPMP_EXEC;
                        break;
                    default:
                        g_assert_not_reached();
                    }
                }
            }

            ret = ((privs & *allowed_privs) == privs);
            break;
        }
    }

    /* No rule matched */
    if (ret == -1) {
        return spmp_hart_has_privs_default(env, addr, allowed_privs, mode);
    }

    return ret == 1 ? true : false;
}

static bool is_entry_locked(CPURISCVState *env, int index)
{
    uint8_t next_a_field = SPMP_AMATCH_TOR;

    /*
     *  Verify if it is the last entry.
     *  If not, check if the next entry is TOR type.
     *  If it is TOR, check if either this or next entry is locked.
     */
    if (index < env->spmp_state.num_deleg_rules - 1) {
        next_a_field =
                    spmp_get_a_field(env->spmp_state.spmp[index + 1].cfg_reg);

        if (next_a_field == SPMP_AMATCH_TOR) {
            return (env->spmp_state.locked_rules >> index) & 0x1
                    || (env->spmp_state.locked_rules >> (index + 1)) & 0x1;
        }
    }

    /* Otherwise, just check this entry */
    return (env->spmp_state.locked_rules >> index) & 0x1;
}

/*
 * Accessor to set the cfg reg for a specific SPMP/HART
 * Bounds checks.
 */
void spmpcfg_csr_write(CPURISCVState *env, uint32_t reg_index,
    target_ulong val, bool m_mode_access)
{
    bool locked = m_mode_access ? false : is_entry_locked(env, reg_index);

    /* If within bounds and not locked */
    if (reg_index < env->spmp_state.num_deleg_rules && !locked) {

        env->spmp_state.spmp[reg_index].cfg_reg = val;
        /* Storing this allows for faster switching with the sspmpen ext */
        env->spmp_state.locked_rules |=
                                ((val & SPMP_LOCK) >> 7 & 0x1) << reg_index;

        spmp_update_rule(env, reg_index);
    } else {
        if (locked) {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: ignoring spmpcfg write - locked entry\n", __func__);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: ignoring spmpcfg write - out of bounds\n", __func__);
        }
    }
}

/*
 * Handle a read from a spmpcfg CSR
 */
target_ulong spmpcfg_csr_read(CPURISCVState *env, uint32_t reg_index)
{
    if (reg_index < env->spmp_state.num_deleg_rules) {
        return env->spmp_state.spmp[reg_index].cfg_reg;
    }

    return 0;
}

/*
 * Handle a write to a spmpaddr CSR
 */
void spmpaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
    target_ulong val, bool m_mode_access)
{
    bool locked = m_mode_access ? false : is_entry_locked(env, addr_index);

    /* If within bounds and not locked */
    if (addr_index < env->spmp_state.num_deleg_rules && !locked) {

        env->spmp_state.spmp[addr_index].addr_reg = val;
        spmp_update_rule(env, addr_index);
    } else {
        if (locked) {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: ignoring spmpaddr write - locked entry\n", __func__);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: ignoring spmpaddr write - out of bounds\n", __func__);
        }
    }
}

/*
 * Handle a read from a spmpaddr CSR
 */
target_ulong spmpaddr_csr_read(CPURISCVState *env, uint32_t addr_index)
{
    target_ulong val = 0;

    if (addr_index < env->spmp_state.num_deleg_rules) {
        val = env->spmp_state.spmp[addr_index].addr_reg;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: ignoring spmpaddr read - out of bounds\n", __func__);
    }

    return val;
}

/*
 * Handle a write to the sspmpen CSR
 */
void sspmpen_csr_write(CPURISCVState *env, uint64_t new_val)
{
    uint64_t mask = (env->spmp_state.num_deleg_rules == MAX_RISCV_SPMPS) ?
                    ~0ULL : ((1ULL << env->spmp_state.num_deleg_rules) - 1);

    /* If the rule is locked, the bit cannot be changed */
    env->spmp_state.spmpen =
                    (env->spmp_state.spmpen & env->spmp_state.locked_rules) |
                    (new_val & ~env->spmp_state.locked_rules);
    env->spmp_state.spmpen &= mask;
}

/*
 * Convert SPMP privilege to TLB page privilege.
 */
int spmp_priv_to_page_prot(spmp_priv_t spmp_priv)
{
    int prot = 0;

    if (spmp_priv & SPMP_READ) {
        prot |= PAGE_READ;
    }
    if (spmp_priv & SPMP_WRITE) {
        prot |= PAGE_WRITE;
    }
    if (spmp_priv & SPMP_EXEC) {
        prot |= PAGE_EXEC;
    }

    return prot;
}

void spmp_unlock_entries(CPURISCVState *env)
{
    /* Reset everything */
    for (int i = 0; i < MAX_RISCV_SPMPS; i++) {
        env->spmp_state.spmp[i].cfg_reg &= ~(SPMP_LOCK | SPMP_AMATCH);
    }

    env->spmp_state.locked_rules = 0;
    env->spmp_state.num_active_rules = 0;
}
