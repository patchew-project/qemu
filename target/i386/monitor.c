/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qapi-commands-misc.h"


/********************* x86 specific hooks for printing page table stuff ****/

const char *names[7] = {(char *)NULL, "PTE", "PDE", "PDP", "PML4", "Pml5",
                        (char *)NULL};
static char *pg_bits(hwaddr ent)
{
    static char buf[32];
    snprintf(buf, 32, "%c%c%c%c%c%c%c%c%c%c",
            ent & PG_NX_MASK ? 'X' : '-',
            ent & PG_GLOBAL_MASK ? 'G' : '-',
            ent & PG_PSE_MASK ? 'S' : '-',
            ent & PG_DIRTY_MASK ? 'D' : '-',
            ent & PG_ACCESSED_MASK ? 'A' : '-',
            ent & PG_PCD_MASK ? 'C' : '-',
            ent & PG_PWT_MASK ? 'T' : '-',
            ent & PG_USER_MASK ? 'U' : '-',
            ent & PG_RW_MASK ? 'W' : '-',
            ent & PG_PRESENT_MASK ? 'P' : '-');
    return buf;
}

bool x86_mon_init_page_table_iterator(Monitor *mon,
                                      struct mem_print_state *state)
{
    CPUArchState *env;
    state->mon = mon;
    state->flush_interior = false;
    state->require_physical_contiguity = false;

    for (int i = 0; i < MAX_HEIGHT; i++) {
        state->vstart[i] = -1;
        state->last_offset[i] = 0;
    }
    state->start_height = 0;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return false;
    }
    state->env = env;

    if (!(env->cr[0] & CR0_PG_MASK)) {
        monitor_printf(mon, "PG disabled\n");
        return false;
    }

    /* set va and pa width */
    if (env->cr[4] & CR4_PAE_MASK) {
        state->paw = 13;
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            if (env->cr[4] & CR4_LA57_MASK) {
                state->vaw = 15;
                state->max_height = 5;
            } else {
                state->vaw = 12;
                state->max_height = 4;
            }
        } else
#endif
        {
            state->vaw = 8;
            state->max_height = 3;
        }
    } else {
        state->max_height = 2;
        state->vaw = 8;
        state->paw = 8;
    }

    return true;
}

void x86_mon_info_pg_print_header(Monitor *mon, struct mem_print_state *state)
{
    /* Header line */
    monitor_printf(mon, "%-*s %-13s %-10s %*s%s\n",
                   3 + 2 * (state->vaw - 3), "VPN range",
                   "Entry", "Flags",
                   2 * (state->max_height - 1), "", "Physical page(s)");
}


static void pg_print(CPUState *cs, Monitor *mon, uint64_t pt_ent,
                     target_ulong vaddr_s, target_ulong vaddr_l,
                     hwaddr paddr_s, hwaddr paddr_l,
                     int offset_s, int offset_l,
                     int height, int max_height, int vaw, int paw,
                     bool is_leaf)

{
    char buf[128];
    char *pos = buf, *end = buf + sizeof(buf);
    target_ulong size = x86_pte_leaf_page_size(cs, height);

    /* VFN range */
    pos += snprintf(pos, end - pos, "%*s[%0*"PRIx64"-%0*"PRIx64"] ",
                    (max_height - height) * 2, "",
                    vaw - 3, (uint64_t)vaddr_s >> 12,
                    vaw - 3, ((uint64_t)vaddr_l + size - 1) >> 12);

    /* Slot */
    if (vaddr_s == vaddr_l) {
        pos += snprintf(pos, end - pos, "%4s[%03x]    ",
                       names[height], offset_s);
    } else {
        pos += snprintf(pos, end - pos, "%4s[%03x-%03x]",
                       names[height], offset_s, offset_l);
    }

    /* Flags */
    pos += snprintf(pos, end - pos, " %s", pg_bits(pt_ent));


    /* Range-compressed PFN's */
    if (is_leaf) {
        if (vaddr_s == vaddr_l) {
            pos += snprintf(pos, end - pos, " %0*"PRIx64,
                            paw - 3, (uint64_t)paddr_s >> 12);
        } else {
            pos += snprintf(pos, end - pos, " %0*"PRIx64"-%0*"PRIx64,
                            paw - 3, (uint64_t)paddr_s >> 12,
                            paw - 3, (uint64_t)paddr_l >> 12);
        }
        pos = MIN(pos, end);
    }

    /* Trim line to fit screen */
    if (pos - buf > 79) {
        strcpy(buf + 77, "..");
    }

    monitor_printf(mon, "%s\n", buf);
}

uint64_t x86_pte_flags(uint64_t pte)
{
    return pte & (PG_USER_MASK | PG_RW_MASK |
                  PG_PRESENT_MASK);
}

/* Returns true if it emitted anything */
bool x86_mon_flush_print_pg_state(CPUState *cs, struct mem_print_state *state)
{
    bool ret = false;
    for (int i = state->start_height; i > 0; i--) {
        if (state->vstart[i] == -1) {
            break;
        }
        PTE_t my_pte;
        my_pte.pte64_t = state->ent[i];
        ret = true;
        pg_print(cs, state->mon, state->ent[i],
                 state->vstart[i], state->vend[i],
                 state->pstart, state->pend,
                 state->offset[i], state->last_offset[i],
                 i, state->max_height, state->vaw, state->paw,
                 x86_pte_leaf(cs, i, &my_pte));
    }

    return ret;
}

/* Perform linear address sign extension */
static hwaddr addr_canonical(CPUArchState *env, hwaddr addr)
{
#ifdef TARGET_X86_64
    if (env->cr[4] & CR4_LA57_MASK) {
        if (addr & (1ULL << 56)) {
            addr |= (hwaddr)-(1LL << 57);
        }
    } else {
        if (addr & (1ULL << 47)) {
            addr |= (hwaddr)-(1LL << 48);
        }
    }
#endif
    return addr;
}

void x86_mon_print_pte(Monitor *mon, CPUArchState *env, hwaddr addr,
                       hwaddr pte)
{
    char buf[128];
    char *pos = buf, *end = buf + sizeof(buf);

    addr = addr_canonical(env, addr);

    pos += snprintf(pos, end - pos, HWADDR_FMT_plx ": " HWADDR_FMT_plx " ",
                    addr, (hwaddr) (pte & PG_ADDRESS_MASK));

    pos += snprintf(pos, end - pos, " %s", pg_bits(pte));

    /* Trim line to fit screen */
    if (pos - buf > 79) {
        strcpy(buf + 77, "..");
    }

    monitor_printf(mon, "%s\n", buf);
}

static
int mem_print_tlb(CPUState *cs, void *data, PTE_t *pte,
                  vaddr vaddr_in, int height, int offset)
{
    struct mem_print_state *state = (struct mem_print_state *) data;
    CPUClass *cc = CPU_GET_CLASS(cs);

    cc->sysemu_ops->mon_print_pte(state->mon, state->env, vaddr_in,
                                  pte->pte64_t);
    return 0;
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    struct mem_print_state state;

    CPUState *cs = mon_get_cpu(mon);
    if (!cs) {
        monitor_printf(mon, "Unable to get CPUState.  Internal error\n");
        return;
    }

    CPUClass *cc = CPU_GET_CLASS(cs);

    if ((!cc->sysemu_ops->pte_child)
        || (!cc->sysemu_ops->pte_leaf)
        || (!cc->sysemu_ops->pte_leaf_page_size)
        || (!cc->sysemu_ops->page_table_entries_per_node)
        || (!cc->sysemu_ops->pte_flags)
        || (!cc->sysemu_ops->mon_print_pte)
        || (!cc->sysemu_ops->mon_init_page_table_iterator)) {
        monitor_printf(mon, "Info tlb unsupported on this ISA\n");
    }

    if (!cc->sysemu_ops->mon_init_page_table_iterator(mon, &state)) {
        monitor_printf(mon, "Unable to initialize page table iterator\n");
        return;
    }

    /**
     * 'info tlb' visits only leaf PTEs marked present.
     * It does not check other protection bits.
     */
    for_each_pte(cs, &mem_print_tlb, &state, false, false);
}

bool x86_mon_print_mem(CPUState *cs, struct mem_print_state *state)
{
    CPUArchState *env = state->env;
    int i = 0;

    /* We need to figure out the lowest populated level */
    for ( ; i < state->max_height; i++) {
        if (state->vstart[i] != -1) {
            break;
        }
    }

    hwaddr vstart = state->vstart[i];
    hwaddr end = state->vend[i] + x86_pte_leaf_page_size(cs, i);
    int prot = x86_pte_flags(state->ent[i]);


    monitor_printf(state->mon, HWADDR_FMT_plx "-" HWADDR_FMT_plx " "
                   HWADDR_FMT_plx " %c%c%c\n",
                   addr_canonical(env, vstart),
                   addr_canonical(env, end),
                   addr_canonical(env, end - vstart),
                   prot & PG_USER_MASK ? 'u' : '-',
                   'r',
                   prot & PG_RW_MASK ? 'w' : '-');
    return true;
}

void hmp_info_mem(Monitor *mon, const QDict *qdict)
{
    struct mem_print_state state;
    CPUState *cs = mon_get_cpu(mon);

    if (!cs) {
        monitor_printf(mon, "Unable to get CPUState.  Internal error\n");
        return;
    }

    CPUClass *cc = CPU_GET_CLASS(cs);

    if ((!cc->sysemu_ops->pte_child)
        || (!cc->sysemu_ops->pte_leaf)
        || (!cc->sysemu_ops->pte_leaf_page_size)
        || (!cc->sysemu_ops->page_table_entries_per_node)
        || (!cc->sysemu_ops->pte_flags)
        || (!cc->sysemu_ops->mon_print_mem)
        || (!cc->sysemu_ops->mon_init_page_table_iterator)) {
        monitor_printf(mon, "Info tlb unsupported on this ISA\n");
    }

    if (!cc->sysemu_ops->mon_init_page_table_iterator(mon, &state)) {
        monitor_printf(mon, "Unable to initialize page table iterator\n");
        return;
    }

    state.flusher = cc->sysemu_ops->mon_print_mem;

    /**
     * We must visit interior entries to update prot
     */
    for_each_pte(cs, &compressing_iterator, &state, true, false);

    /* Flush the last entry, if needed */
    cc->sysemu_ops->mon_print_mem(cs, &state);
}

void hmp_mce(Monitor *mon, const QDict *qdict)
{
    X86CPU *cpu;
    CPUState *cs;
    int cpu_index = qdict_get_int(qdict, "cpu_index");
    int bank = qdict_get_int(qdict, "bank");
    uint64_t status = qdict_get_int(qdict, "status");
    uint64_t mcg_status = qdict_get_int(qdict, "mcg_status");
    uint64_t addr = qdict_get_int(qdict, "addr");
    uint64_t misc = qdict_get_int(qdict, "misc");
    int flags = MCE_INJECT_UNCOND_AO;

    if (qdict_get_try_bool(qdict, "broadcast", false)) {
        flags |= MCE_INJECT_BROADCAST;
    }
    cs = qemu_get_cpu(cpu_index);
    if (cs != NULL) {
        cpu = X86_CPU(cs);
        cpu_x86_inject_mce(mon, cpu, bank, status, mcg_status, addr, misc,
                           flags);
    }
}

static target_long monitor_get_pc(Monitor *mon, const struct MonitorDef *md,
                                  int val)
{
    CPUArchState *env = mon_get_cpu_env(mon);
    return env->eip + env->segs[R_CS].base;
}

const MonitorDef monitor_defs[] = {
#define SEG(name, seg) \
    { name, offsetof(CPUX86State, segs[seg].selector), NULL, MD_I32 },\
    { name ".base", offsetof(CPUX86State, segs[seg].base) },\
    { name ".limit", offsetof(CPUX86State, segs[seg].limit), NULL, MD_I32 },

    { "eax", offsetof(CPUX86State, regs[0]) },
    { "ecx", offsetof(CPUX86State, regs[1]) },
    { "edx", offsetof(CPUX86State, regs[2]) },
    { "ebx", offsetof(CPUX86State, regs[3]) },
    { "esp|sp", offsetof(CPUX86State, regs[4]) },
    { "ebp|fp", offsetof(CPUX86State, regs[5]) },
    { "esi", offsetof(CPUX86State, regs[6]) },
    { "edi", offsetof(CPUX86State, regs[7]) },
#ifdef TARGET_X86_64
    { "r8", offsetof(CPUX86State, regs[8]) },
    { "r9", offsetof(CPUX86State, regs[9]) },
    { "r10", offsetof(CPUX86State, regs[10]) },
    { "r11", offsetof(CPUX86State, regs[11]) },
    { "r12", offsetof(CPUX86State, regs[12]) },
    { "r13", offsetof(CPUX86State, regs[13]) },
    { "r14", offsetof(CPUX86State, regs[14]) },
    { "r15", offsetof(CPUX86State, regs[15]) },
#endif
    { "eflags", offsetof(CPUX86State, eflags) },
    { "eip", offsetof(CPUX86State, eip) },
    SEG("cs", R_CS)
    SEG("ds", R_DS)
    SEG("es", R_ES)
    SEG("ss", R_SS)
    SEG("fs", R_FS)
    SEG("gs", R_GS)
    { "pc", 0, monitor_get_pc, },
    { NULL },
};

const MonitorDef *target_monitor_defs(void)
{
    return monitor_defs;
}
