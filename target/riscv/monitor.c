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

static void print_pte_header(Monitor *mon,
        char const vaddr_char, char const paddr_char)
{

# define        VIRTUAL_WIDTH\
        ((int) ((sizeof "ff" - sizeof "") * sizeof(target_ulong)))
# define        PHYSICAL_WIDTH\
        ((int) ((sizeof "ff" - sizeof "") * sizeof(hwaddr)))
# define        ATTRIBUTE_WIDTH ((int) (sizeof "rwxugad" - sizeof ""))

# define        VIRTUAL_COLUMN_WIDTH    (1 + VIRTUAL_WIDTH)
# define        PHYSICAL_COLUMN_WIDTH   (1 + PHYSICAL_WIDTH)

    static char const dashes[PHYSICAL_WIDTH] = "----------------";

    monitor_printf(mon,
            "%c%-*s%c%-*s%-*s%-*s\n"
            "%-*.*s%-*.*s%-*.*s%-*.*s\n",

            vaddr_char, VIRTUAL_COLUMN_WIDTH - 1, "addr",
            paddr_char, PHYSICAL_COLUMN_WIDTH - 1, "addr",
            VIRTUAL_COLUMN_WIDTH, "size",
            ATTRIBUTE_WIDTH, "attr",

            VIRTUAL_COLUMN_WIDTH, VIRTUAL_WIDTH, dashes,
            PHYSICAL_COLUMN_WIDTH, PHYSICAL_WIDTH, dashes,
            VIRTUAL_COLUMN_WIDTH, VIRTUAL_WIDTH, dashes,
            ATTRIBUTE_WIDTH, ATTRIBUTE_WIDTH, dashes);
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

# if 4 == TARGET_LONG_SIZE
#       define  TARGET_xFMT     PRIx32
# elif 8 == TARGET_LONG_SIZE
#       define  TARGET_xFMT     PRIx64
# else
#       error TARGET_LONG_SIZE not handled
# endif

    /* note: RISC-V physical addresses are actually xlen + 2 bits long
    OTHO, QEMU wil probably never support addresses longer than 64 bits */
    monitor_printf(mon,
            "%-*.*" TARGET_xFMT
            "%-*.*" PRIx64
            "%-*.*" TARGET_xFMT
            "%c%c%c%c%c%c%c\n",
            VIRTUAL_COLUMN_WIDTH, VIRTUAL_WIDTH, addr_canonical(va_bits, vaddr),
            PHYSICAL_COLUMN_WIDTH, PHYSICAL_WIDTH, paddr,
            VIRTUAL_COLUMN_WIDTH, VIRTUAL_WIDTH, size,
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
                     int guest,
                     target_ulong *vbase, hwaddr *pbase, hwaddr *last_paddr,
                     target_ulong *last_size, int *last_attr)
{
    hwaddr pte_addr;
    hwaddr paddr;
    target_ulong pgsize;
    target_ulong pte;
    int ptshift;
    int attr;
    int idx, idx_end;

    if (level < 0) {
        return;
    }

    ptshift = level * ptidxbits;
    pgsize = 1UL << (PGSHIFT + ptshift);

    for (idx = 0, idx_end = 1 << (ptidxbits + (guest ? 2 : 0));
            idx_end > idx; idx++) {
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
                         va_bits,
                         0 /* guest */,
                         vbase, pbase, last_paddr,
                         last_size, last_attr);
            }
        }

        start += pgsize;
    }

}

static void mem_info_svxx(Monitor *mon, CPUArchState *env,
        target_ulong const atp,
        int guest, char const vaddr_char, char const paddr_char)
{
    int levels, ptidxbits, ptesize, vm, va_bits;
    hwaddr base;
    target_ulong vbase;
    hwaddr pbase;
    hwaddr last_paddr;
    target_ulong last_size;
    int last_attr;

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        base = (hwaddr)get_field(atp, SATP32_PPN) << PGSHIFT;
        vm = get_field(atp, SATP32_MODE);
    } else {
        base = (hwaddr)get_field(atp, SATP64_PPN) << PGSHIFT;
        vm = get_field(atp, SATP64_MODE);
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
    print_pte_header(mon, vaddr_char, paddr_char);

    vbase = -1;
    pbase = -1;
    last_paddr = -1;
    last_size = 0;
    last_attr = 0;

    /* walk page tables, starting from address 0 */
    walk_pte(mon, base, 0, levels - 1, ptidxbits, ptesize, va_bits,
             guest,
             &vbase, &pbase, &last_paddr, &last_size, &last_attr);

    /* don't forget the last one */
    print_pte(mon, va_bits, vbase, pbase,
              last_paddr + last_size - pbase, last_attr);
}

void hmp_info_mem(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;
    target_ulong atp;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        monitor_printf(mon, "S-mode MMU unavailable\n");
        return;
    }

    atp = env->satp;
    if (riscv_cpu_mxl(env) == MXL_RV32) {
        if (!(atp & SATP32_MODE)) {
            monitor_printf(mon, "No translation or protection\n");
            return;
        }
    } else {
        if (!(atp & SATP64_MODE)) {
            monitor_printf(mon, "No translation or protection\n");
            return;
        }
    }

    mem_info_svxx(mon, env, atp, 0, 'v', 'p');
}

void hmp_info_gmem(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;
    target_ulong atp;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    if (!riscv_has_ext(env, RVH)) {
        monitor_printf(mon, "hypervisor extension not available\n");
        return;
    }

    atp = env->hgatp;
    if (!((MXL_RV32 == riscv_cpu_mxl(env) ? SATP32_MODE : SATP64_MODE)
            & atp)) {
        monitor_printf(mon, "No translation or protection\n");
        return;
    }

    mem_info_svxx(mon, env, atp, 1, 'g', 'p');
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
