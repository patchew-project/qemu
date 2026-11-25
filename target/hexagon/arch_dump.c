/*
 * Hexagon system guest ELF core dumps.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "elf.h"
#include "qapi/error.h"
#include "qemu/range.h"
#include "system/dump.h"
#include "system/memory_mapping.h"
#include "hw/hexagon/hexagon_tlb.h"
#include "internal.h"
#include "macros.h"
#include "sys_macros.h"
#include "core-dump.h"

/* Linux include/linux/elfcore.h: struct elf_prstatus for the 32-bit ABI. */
typedef struct HexagonElfPrstatus {
    uint8_t pad1[24];
    uint32_t pr_pid;
    uint8_t pad2[44];
    HexagonUserRegs pr_reg;
    uint32_t pr_fpvalid;
} QEMU_PACKED HexagonElfPrstatus;

QEMU_BUILD_BUG_ON(offsetof(HexagonElfPrstatus, pr_reg) != 72);
QEMU_BUILD_BUG_ON(sizeof(HexagonElfPrstatus) != 268);

typedef struct HexagonElfNote {
    Elf32_Nhdr header;
    char name[8]; /* "CORE" including NUL, padded to a 4-byte boundary. */
    HexagonElfPrstatus prstatus;
} QEMU_PACKED HexagonElfNote;

int hexagon_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                                 int cpuid, DumpState *s)
{
    CPUHexagonState *env = cpu_env(cs);
    HexagonElfNote note = { 0 };
    HexagonUserRegs regs;
    g_autofree void *extra = NULL;
    size_t extra_size = hexagon_core_notes_size(env);
    int ret;

    note.header.n_namesz = cpu_to_dump32(s, sizeof("CORE"));
    note.header.n_descsz = cpu_to_dump32(s, sizeof(note.prstatus));
    note.header.n_type = cpu_to_dump32(s, NT_PRSTATUS);
    memcpy(note.name, "CORE", sizeof("CORE"));
    note.prstatus.pr_pid = cpu_to_dump32(s, cpuid);
    hexagon_core_copy_regs(&regs, env);
    memcpy(&note.prstatus.pr_reg, &regs, sizeof(regs));

    ret = f(&note, sizeof(note), s);
    if (ret < 0) {
        return ret;
    }

    extra = g_malloc(extra_size);
    hexagon_core_fill_notes(extra, env);
    return f(extra, extra_size, s);
}

void hexagon_get_dump_info(const CPUState *cs, ArchDumpInfo *info,
                           const GuestPhysBlockList *guest_phys_blocks)
{
    const HexagonCPU *cpu = HEXAGON_CPU(cs);

    info->d_machine = EM_HEXAGON;
    info->d_class = ELFCLASS32;
    info->d_endian = ELFDATA2LSB;
    /* The values of HexagonVersion match the EF_HEXAGON_MACH_* flags. */
    info->d_flags = cpu->cfg.hex_def->hex_version;
}

size_t hexagon_get_note_size(const CPUState *cs, int class, int machine,
                             int nr_cpus)
{
    CPUState *cpu;
    size_t size = 0;

    CPU_FOREACH(cpu) {
        size += sizeof(HexagonElfNote) + hexagon_core_notes_size(cpu_env(cpu));
    }
    return size;
}

static bool hexagon_mapping_overlaps(const MemoryMappingList *list,
                                     const HexagonTLBMapping *m)
{
    const MemoryMapping *other;

    QTAILQ_FOREACH(other, &list->head, next) {
        if (ranges_overlap(other->virt_addr, other->length, m->va, m->size)) {
            return true;
        }
    }
    return false;
}

bool hexagon_cpu_get_memory_mapping(CPUState *cs, MemoryMappingList *list,
                                    Error **errp)
{
    const HexagonCPU *cpu = HEXAGON_CPU(cs);
    uint32_t asid = GET_SSR_FIELD(SSR_ASID,
                                  cpu_env(cs)->t_sreg[HEX_SREG_SSR]);
    uint32_t num_entries;

    if (!cpu->tlb) {
        error_setg(errp, "Hexagon CPU has no TLB");
        return false;
    }

    num_entries = hexagon_tlb_get_num_entries(cpu->tlb);
    for (uint32_t i = 0; i < num_entries; i++) {
        HexagonTLBMapping m;

        if (!hexagon_tlb_decode_entry(hexagon_tlb_read(cpu->tlb, i), &m) ||
            (!m.global && m.asid != asid) ||
            hexagon_mapping_overlaps(list, &m)) {
            continue;
        }
        memory_mapping_list_add_merge_sorted(list, m.pa, m.va, m.size);
    }
    return true;
}
