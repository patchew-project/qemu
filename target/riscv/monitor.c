/*
 * QEMU monitor for RISC-V
 *
 * Copyright (c) 2019 Bin Meng <bmeng.cn@gmail.com>
 * Copyright (c) 2021 Siemens AG, konrad.schwarz@siemens.com
 *
 * RISC-V specific monitor commands implementation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_bits.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"

#ifdef TARGET_RISCV64
#define PTE_HEADER_FIELDS       "vaddr            paddr            "\
                                "size             attr\n"
#define PTE_HEADER_DELIMITER    "---------------- ---------------- "\
                                "---------------- -------\n"
#else
#define PTE_HEADER_FIELDS       "vaddr    paddr            size     attr\n"
#define PTE_HEADER_DELIMITER    "-------- ---------------- -------- -------\n"
#endif

/* Perform linear address sign extension */
static target_ulong addr_canonical(int va_bits, target_ulong addr)
{
#ifdef TARGET_RISCV64
    if (addr & (1UL << (va_bits - 1))) {
        addr |= (hwaddr)-(1L << va_bits);
    }
#endif

    return addr;
}

static void print_pte_header(Monitor *mon)
{
    monitor_printf(mon, PTE_HEADER_FIELDS);
    monitor_printf(mon, PTE_HEADER_DELIMITER);
}

static void print_pte(Monitor *mon, int va_bits, target_ulong vaddr,
                      hwaddr paddr, target_ulong size, int attr)
{
    /* santity check on vaddr */
    if (vaddr >= (1UL << va_bits)) {
        return;
    }

    if (!size) {
        return;
    }

    monitor_printf(mon, TARGET_FMT_lx " " TARGET_FMT_plx " " TARGET_FMT_lx
                   " %c%c%c%c%c%c%c\n",
                   addr_canonical(va_bits, vaddr),
                   paddr, size,
                   attr & PTE_R ? 'r' : '-',
                   attr & PTE_W ? 'w' : '-',
                   attr & PTE_X ? 'x' : '-',
                   attr & PTE_U ? 'u' : '-',
                   attr & PTE_G ? 'g' : '-',
                   attr & PTE_A ? 'a' : '-',
                   attr & PTE_D ? 'd' : '-');
}

static void walk_pte(Monitor *mon, hwaddr base, target_ulong start,
                     int level, int ptidxbits, int ptesize, int va_bits,
                     target_ulong *vbase, hwaddr *pbase, hwaddr *last_paddr,
                     target_ulong *last_size, int *last_attr)
{
    hwaddr pte_addr;
    hwaddr paddr;
    target_ulong pgsize;
    target_ulong pte;
    int ptshift;
    int attr;
    int idx;

    if (level < 0) {
        return;
    }

    ptshift = level * ptidxbits;
    pgsize = 1UL << (PGSHIFT + ptshift);

    for (idx = 0; idx < (1UL << ptidxbits); idx++) {
        pte_addr = base + idx * ptesize;
        cpu_physical_memory_read(pte_addr, &pte, ptesize);

        paddr = (hwaddr)(pte >> PTE_PPN_SHIFT) << PGSHIFT;
        attr = pte & 0xff;

        /* PTE has to be valid */
        if (attr & PTE_V) {
            if (attr & (PTE_R | PTE_W | PTE_X)) {
                /*
                 * A leaf PTE has been found
                 *
                 * If current PTE's permission bits differ from the last one,
                 * or current PTE's ppn does not make a contiguous physical
                 * address block together with the last one, print out the last
                 * contiguous mapped block details.
                 */
                if ((*last_attr != attr) ||
                    (*last_paddr + *last_size != paddr)) {
                    print_pte(mon, va_bits, *vbase, *pbase,
                              *last_paddr + *last_size - *pbase, *last_attr);

                    *vbase = start;
                    *pbase = paddr;
                    *last_attr = attr;
                }

                *last_paddr = paddr;
                *last_size = pgsize;
            } else {
                /* pointer to the next level of the page table */
                walk_pte(mon, paddr, start, level - 1, ptidxbits, ptesize,
                         va_bits, vbase, pbase, last_paddr,
                         last_size, last_attr);
            }
        }

        start += pgsize;
    }

}

static void mem_info_svxx(Monitor *mon, CPUArchState *env)
{
    int levels, ptidxbits, ptesize, vm, va_bits;
    hwaddr base;
    target_ulong vbase;
    hwaddr pbase;
    hwaddr last_paddr;
    target_ulong last_size;
    int last_attr;

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        base = (hwaddr)get_field(env->satp, SATP32_PPN) << PGSHIFT;
        vm = get_field(env->satp, SATP32_MODE);
    } else {
        base = (hwaddr)get_field(env->satp, SATP64_PPN) << PGSHIFT;
        vm = get_field(env->satp, SATP64_MODE);
    }

    switch (vm) {
    case VM_1_10_SV32:
        levels = 2;
        ptidxbits = 10;
        ptesize = 4;
        break;
    case VM_1_10_SV39:
        levels = 3;
        ptidxbits = 9;
        ptesize = 8;
        break;
    case VM_1_10_SV48:
        levels = 4;
        ptidxbits = 9;
        ptesize = 8;
        break;
    case VM_1_10_SV57:
        levels = 5;
        ptidxbits = 9;
        ptesize = 8;
        break;
    default:
        g_assert_not_reached();
        break;
    }

    /* calculate virtual address bits */
    va_bits = PGSHIFT + levels * ptidxbits;

    /* print header */
    print_pte_header(mon);

    vbase = -1;
    pbase = -1;
    last_paddr = -1;
    last_size = 0;
    last_attr = 0;

    /* walk page tables, starting from address 0 */
    walk_pte(mon, base, 0, levels - 1, ptidxbits, ptesize, va_bits,
             &vbase, &pbase, &last_paddr, &last_size, &last_attr);

    /* don't forget the last one */
    print_pte(mon, va_bits, vbase, pbase,
              last_paddr + last_size - pbase, last_attr);
}

void hmp_info_mem(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        monitor_printf(mon, "S-mode MMU unavailable\n");
        return;
    }

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        if (!(env->satp & SATP32_MODE)) {
            monitor_printf(mon, "No translation or protection\n");
            return;
        }
    } else {
        if (!(env->satp & SATP64_MODE)) {
            monitor_printf(mon, "No translation or protection\n");
            return;
        }
    }

    mem_info_svxx(mon, env);
}

static const MonitorDef monitor_defs[] = {
# define MONITORDEF_RISCV_GPR(NO, ALIAS)\
    { "x" #NO #ALIAS, offsetof(CPURISCVState, gpr[NO]) },

    MONITORDEF_RISCV_GPR(0, |zero)
    MONITORDEF_RISCV_GPR(1, |ra)
    MONITORDEF_RISCV_GPR(2, |sp)
    MONITORDEF_RISCV_GPR(3, |gp)
    MONITORDEF_RISCV_GPR(4, |tp)
    MONITORDEF_RISCV_GPR(5, |t0)
    MONITORDEF_RISCV_GPR(6, |t1)
    MONITORDEF_RISCV_GPR(7, |t2)
    MONITORDEF_RISCV_GPR(8, |s0|fp)
    MONITORDEF_RISCV_GPR(9, |s1)
    MONITORDEF_RISCV_GPR(10, |a0)
    MONITORDEF_RISCV_GPR(11, |a1)
    MONITORDEF_RISCV_GPR(12, |a2)
    MONITORDEF_RISCV_GPR(13, |a3)
    MONITORDEF_RISCV_GPR(14, |a4)
    MONITORDEF_RISCV_GPR(15, |a5)
    MONITORDEF_RISCV_GPR(16, |a6)
    MONITORDEF_RISCV_GPR(17, |a7)
    MONITORDEF_RISCV_GPR(18, |s2)
    MONITORDEF_RISCV_GPR(19, |s3)
    MONITORDEF_RISCV_GPR(20, |s4)
    MONITORDEF_RISCV_GPR(21, |s5)
    MONITORDEF_RISCV_GPR(22, |s6)
    MONITORDEF_RISCV_GPR(23, |s7)
    MONITORDEF_RISCV_GPR(24, |s8)
    MONITORDEF_RISCV_GPR(25, |s9)
    MONITORDEF_RISCV_GPR(26, |s10)
    MONITORDEF_RISCV_GPR(27, |s11)
    MONITORDEF_RISCV_GPR(28, |t3)
    MONITORDEF_RISCV_GPR(29, |t4)
    MONITORDEF_RISCV_GPR(30, |t5)
    MONITORDEF_RISCV_GPR(31, |t6)

    { },
};

const MonitorDef *target_monitor_defs(void)
{
    return monitor_defs;
}

int target_get_monitor_def(CPUState *cs, const char *name, uint64_t *pval)
{

    target_ulong ret_value;
    CPURISCVState *const env = &RISCV_CPU (cs)->env;
    riscv_csr_operations *op;
    for (op = csr_ops; 1[&csr_ops] > op; ++op) {
        if (!op->name) {
            continue;
        }
        if (!strcmp(name, op->name)) {
            if (RISCV_EXCP_NONE != riscv_csrrw_debug(env, op - csr_ops,
                                 &ret_value,
                                 0 /* new_value */,
                                 0 /* write_mask */))
                return -1;
            *pval = ret_value;
            return 0;
        }
    }
    return -1;
}
