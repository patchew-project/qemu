/*
 * Functions related to disassembly from the monitor
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas-internal.h"
#include "disas/disas.h"
#include "exec/memory.h"
#include "hw/core/cpu.h"
#include "monitor/monitor.h"

static int
physical_read_memory(bfd_vma memaddr, bfd_byte *myaddr, int length,
                     struct disassemble_info *info)
{
    CPUDebug *s = container_of(info, CPUDebug, info);
    MemTxResult res;

    res = address_space_read(s->cpu->as, memaddr, MEMTXATTRS_UNSPECIFIED,
                             myaddr, length);
    return res == MEMTX_OK ? 0 : EIO;
}

/* Disassembler for the monitor.  */
void monitor_disas(Monitor *mon, CPUState *cpu,
                   hwaddr pc, int nb_insn, int is_physical)
{
    int count, i;
    CPUDebug s;
    g_autoptr(GString) ds = g_string_new("");

    disas_initialize_debug_target(&s, cpu);
    s.info.fprintf_func = disas_gstring_printf;
    s.info.stream = (FILE *)ds;  /* abuse this slot */

    if (is_physical) {
        s.info.read_memory_func = physical_read_memory;
    }
    s.info.buffer_vma = pc;

    if (s.info.cap_arch >= 0 && cap_disas_monitor(&s.info, pc, nb_insn)) {
        monitor_puts(mon, ds->str);
        return;
    }

    if (!s.info.print_insn) {
        monitor_printf(mon, "0x" HWADDR_FMT_plx
                       ": Asm output not supported on this arch\n", pc);
        return;
    }

    for (i = 0; i < nb_insn; i++) {
        g_string_append_printf(ds, "0x" HWADDR_FMT_plx ":  ", pc);
        count = s.info.print_insn(pc, &s.info);
        g_string_append_c(ds, '\n');
        if (count < 0) {
            break;
        }
        pc += count;
    }

    monitor_puts(mon, ds->str);
}
