/*
 * QEMU RISC-V Native Debug Support (TCG specific)
 *
 * Copyright (c) 2022 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This provides the native debug support via the Trigger Module, as defined
 * in the RISC-V Debug Specification:
 * https://github.com/riscv/riscv-debug-spec/raw/master/riscv-debug-stable.pdf
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"

void riscv_cpu_debug_excp_handler(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (cs->watchpoint_hit) {
        if (cs->watchpoint_hit->flags & BP_CPU) {
            do_trigger_action(env, DBG_ACTION_BP);
        }
    } else {
        if (cpu_breakpoint_test(cs, env->pc, BP_CPU)) {
            do_trigger_action(env, DBG_ACTION_BP);
        }
    }
}

bool riscv_cpu_debug_check_breakpoint(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    CPUBreakpoint *bp;
    target_ulong ctrl;
    target_ulong pc;
    int trigger_type;
    int i;

    QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
        for (i = 0; i < RV_MAX_TRIGGERS; i++) {
            trigger_type = get_trigger_type(env, i);

            switch (trigger_type) {
            case TRIGGER_TYPE_AD_MATCH:
                /* type 2 trigger cannot be fired in VU/VS mode */
                if (env->virt_enabled) {
                    return false;
                }

                ctrl = env->tdata1[i];
                pc = env->tdata2[i];

                if ((ctrl & TYPE2_EXEC) && (bp->pc == pc)) {
                    /* check U/S/M bit against current privilege level */
                    if ((ctrl >> 3) & BIT(env->priv)) {
                        return true;
                    }
                }
                break;
            case TRIGGER_TYPE_AD_MATCH6:
                ctrl = env->tdata1[i];
                pc = env->tdata2[i];

                if ((ctrl & TYPE6_EXEC) && (bp->pc == pc)) {
                    if (env->virt_enabled) {
                        /* check VU/VS bit against current privilege level */
                        if ((ctrl >> 23) & BIT(env->priv)) {
                            return true;
                        }
                    } else {
                        /* check U/S/M bit against current privilege level */
                        if ((ctrl >> 3) & BIT(env->priv)) {
                            return true;
                        }
                    }
                }
                break;
            default:
                /* other trigger types are not supported or irrelevant */
                break;
            }
        }
    }

    return false;
}

bool riscv_cpu_debug_check_watchpoint(CPUState *cs, CPUWatchpoint *wp)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    target_ulong ctrl;
    target_ulong addr;
    int trigger_type;
    int flags;
    int i;

    for (i = 0; i < RV_MAX_TRIGGERS; i++) {
        trigger_type = get_trigger_type(env, i);

        switch (trigger_type) {
        case TRIGGER_TYPE_AD_MATCH:
            /* type 2 trigger cannot be fired in VU/VS mode */
            if (env->virt_enabled) {
                return false;
            }

            ctrl = env->tdata1[i];
            addr = env->tdata2[i];
            flags = 0;

            if (ctrl & TYPE2_LOAD) {
                flags |= BP_MEM_READ;
            }
            if (ctrl & TYPE2_STORE) {
                flags |= BP_MEM_WRITE;
            }

            if ((wp->flags & flags) && (wp->vaddr == addr)) {
                /* check U/S/M bit against current privilege level */
                if ((ctrl >> 3) & BIT(env->priv)) {
                    return true;
                }
            }
            break;
        case TRIGGER_TYPE_AD_MATCH6:
            ctrl = env->tdata1[i];
            addr = env->tdata2[i];
            flags = 0;

            if (ctrl & TYPE6_LOAD) {
                flags |= BP_MEM_READ;
            }
            if (ctrl & TYPE6_STORE) {
                flags |= BP_MEM_WRITE;
            }

            if ((wp->flags & flags) && (wp->vaddr == addr)) {
                if (env->virt_enabled) {
                    /* check VU/VS bit against current privilege level */
                    if ((ctrl >> 23) & BIT(env->priv)) {
                        return true;
                    }
                } else {
                    /* check U/S/M bit against current privilege level */
                    if ((ctrl >> 3) & BIT(env->priv)) {
                        return true;
                    }
                }
            }
            break;
        default:
            /* other trigger types are not supported */
            break;
        }
    }

    return false;
}
