/*
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Contributions from 2012-04-01 on are considered under GPL version 2,
 * or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/gdbstub.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#ifndef CONFIG_USER_ONLY
#include "ui/console.h"
#endif

#undef DEBUG_UC32

#ifdef DEBUG_UC32
#define DPRINTF(fmt, ...) printf("%s: " fmt , __func__, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

UniCore32CPU *uc32_cpu_init(const char *cpu_model)
{
    return UNICORE32_CPU(cpu_generic_init(TYPE_UNICORE32_CPU, cpu_model));
}

#ifndef CONFIG_USER_ONLY
void helper_cp0_set(CPUUniCore32State *env, uint32_t val, uint32_t creg,
        uint32_t cop)
{
    UniCore32CPU *cpu = uc32_env_get_cpu(env);

    /*
     * movc pp.nn, rn, #imm9
     *      rn: UCOP_REG_D
     *      nn: UCOP_REG_N
     *          1: sys control reg.
     *          2: page table base reg.
     *          3: data fault status reg.
     *          4: insn fault status reg.
     *          5: cache op. reg.
     *          6: tlb op. reg.
     *      imm9: split UCOP_IMM10 with bit5 is 0
     */
    switch (creg) {
    case 1:
        if (cop != 0) {
            goto unrecognized;
        }
        env->cp0.c1_sys = val;
        break;
    case 2:
        if (cop != 0) {
            goto unrecognized;
        }
        env->cp0.c2_base = val;
        break;
    case 3:
        if (cop != 0) {
            goto unrecognized;
        }
        env->cp0.c3_faultstatus = val;
        break;
    case 4:
        if (cop != 0) {
            goto unrecognized;
        }
        env->cp0.c4_faultaddr = val;
        break;
    case 5:
        switch (cop) {
        case 28:
            DPRINTF("Invalidate Entire I&D cache\n");
            return;
        case 20:
            DPRINTF("Invalidate Entire Icache\n");
            return;
        case 12:
            DPRINTF("Invalidate Entire Dcache\n");
            return;
        case 10:
            DPRINTF("Clean Entire Dcache\n");
            return;
        case 14:
            DPRINTF("Flush Entire Dcache\n");
            return;
        case 13:
            DPRINTF("Invalidate Dcache line\n");
            return;
        case 11:
            DPRINTF("Clean Dcache line\n");
            return;
        case 15:
            DPRINTF("Flush Dcache line\n");
            return;
        }
        break;
    case 6:
        if ((cop <= 6) && (cop >= 2)) {
            /* invalid all tlb */
            tlb_flush(CPU(cpu));
            return;
        }
        break;
    default:
        goto unrecognized;
    }
    return;
unrecognized:
    DPRINTF("Wrong register (%d) or wrong operation (%d) in cp0_set!\n",
            creg, cop);
}

uint32_t helper_cp0_get(CPUUniCore32State *env, uint32_t creg, uint32_t cop)
{
    /*
     * movc rd, pp.nn, #imm9
     *      rd: UCOP_REG_D
     *      nn: UCOP_REG_N
     *          0: cpuid and cachetype
     *          1: sys control reg.
     *          2: page table base reg.
     *          3: data fault status reg.
     *          4: insn fault status reg.
     *      imm9: split UCOP_IMM10 with bit5 is 0
     */
    switch (creg) {
    case 0:
        switch (cop) {
        case 0:
            return env->cp0.c0_cpuid;
        case 1:
            return env->cp0.c0_cachetype;
        }
        break;
    case 1:
        if (cop == 0) {
            return env->cp0.c1_sys;
        }
        break;
    case 2:
        if (cop == 0) {
            return env->cp0.c2_base;
        }
        break;
    case 3:
        if (cop == 0) {
            return env->cp0.c3_faultstatus;
        }
        break;
    case 4:
        if (cop == 0) {
            return env->cp0.c4_faultaddr;
        }
        break;
    }
    DPRINTF("Wrong register (%d) or wrong operation (%d) in cp0_set!\n",
            creg, cop);
    return 0;
}

#ifdef CONFIG_CURSES
/*
 * FIXME:
 *     1. curses windows will be blank when switching back
 *     2. backspace is not handled yet
 */
static void putc_on_screen(unsigned char ch)
{
    static WINDOW *localwin;
    static int init;

    if (!init) {
        /* Assume 80 * 30 screen to minimize the implementation */
        localwin = newwin(30, 80, 0, 0);
        scrollok(localwin, TRUE);
        init = TRUE;
    }

    if (isprint(ch)) {
        wprintw(localwin, "%c", ch);
    } else {
        switch (ch) {
        case '\n':
            wprintw(localwin, "%c", ch);
            break;
        case '\r':
            /* If '\r' is put before '\n', the curses window will destroy the
             * last print line. And meanwhile, '\n' implifies '\r' inside. */
            break;
        default: /* Not handled, so just print it hex code */
            wprintw(localwin, "-- 0x%x --", ch);
        }
    }

    wrefresh(localwin);
}
#else
#define putc_on_screen(c)               do { } while (0)
#endif

void helper_cp1_putc(target_ulong x)
{
    putc_on_screen((unsigned char)x);   /* Output to screen */
    DPRINTF("%c", x);                   /* Output to stdout */
}
#endif

#ifdef CONFIG_USER_ONLY
void switch_mode(CPUUniCore32State *env, int mode)
{
    UniCore32CPU *cpu = uc32_env_get_cpu(env);

    if (mode != ASR_MODE_USER) {
        cpu_abort(CPU(cpu), "Tried to switch out of user mode\n");
    }
}

void uc32_cpu_do_interrupt(CPUState *cs)
{
    cpu_abort(cs, "NO interrupt in user mode\n");
}

int uc32_cpu_handle_mmu_fault(CPUState *cs, vaddr address,
                              int access_type, int mmu_idx)
{
    cpu_abort(cs, "NO mmu fault in user mode\n");
    return 1;
}
#endif

static const char *cpu_mode_names[16] = {
    "USER", "REAL", "INTR", "PRIV", "UM14", "UM15", "UM16", "TRAP",
    "UM18", "UM19", "UM1A", "EXTN", "UM1C", "UM1D", "UM1E", "SUSR"
};

#undef UCF64_DUMP_STATE
#ifdef UCF64_DUMP_STATE
static void cpu_dump_state_ucf64(CPUUniCore32State *env, FILE *f,
        fprintf_function cpu_fprintf, int flags)
{
    int i;
    union {
        uint32_t i;
        float s;
    } s0, s1;
    CPU_DoubleU d;
    /* ??? This assumes float64 and double have the same layout.
       Oh well, it's only debug dumps.  */
    union {
        float64 f64;
        double d;
    } d0;

    for (i = 0; i < 16; i++) {
        d.d = env->ucf64.regs[i];
        s0.i = d.l.lower;
        s1.i = d.l.upper;
        d0.f64 = d.d;
        cpu_fprintf(f, "s%02d=%08x(%8g) s%02d=%08x(%8g)",
                    i * 2, (int)s0.i, s0.s,
                    i * 2 + 1, (int)s1.i, s1.s);
        cpu_fprintf(f, " d%02d=%" PRIx64 "(%8g)\n",
                    i, (uint64_t)d0.f64, d0.d);
    }
    cpu_fprintf(f, "FPSCR: %08x\n", (int)env->ucf64.xregs[UC32_UCF64_FPSCR]);
}
#else
#define cpu_dump_state_ucf64(env, file, pr, flags)      do { } while (0)
#endif

void uc32_cpu_dump_state(CPUState *cs, FILE *f,
                         fprintf_function cpu_fprintf, int flags)
{
    UniCore32CPU *cpu = UNICORE32_CPU(cs);
    CPUUniCore32State *env = &cpu->env;
    int i;
    uint32_t psr;

    for (i = 0; i < 32; i++) {
        cpu_fprintf(f, "R%02d=%08x", i, env->regs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }
    psr = cpu_asr_read(env);
    cpu_fprintf(f, "PSR=%08x %c%c%c%c %s\n",
                psr,
                psr & (1 << 31) ? 'N' : '-',
                psr & (1 << 30) ? 'Z' : '-',
                psr & (1 << 29) ? 'C' : '-',
                psr & (1 << 28) ? 'V' : '-',
                cpu_mode_names[psr & 0xf]);

    cpu_dump_state_ucf64(env, f, cpu_fprintf, flags);
}

bool uc32_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        UniCore32CPU *cpu = UNICORE32_CPU(cs);
        CPUUniCore32State *env = &cpu->env;

        if (!(env->uncached_asr & ASR_I)) {
            cs->exception_index = UC32_EXCP_INTR;
            uc32_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}
