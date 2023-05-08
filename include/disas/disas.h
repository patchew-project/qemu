#ifndef QEMU_DISAS_H
#define QEMU_DISAS_H

#include "exec/hwaddr.h"

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, const void *code, unsigned long size);
void target_disas(FILE *out, CPUState *cpu, hwaddr code, long size);

char *plugin_disas(CPUState *cpu, uint64_t addr, size_t size);

/* Look up symbol for debugging purpose.  Returns "" if unknown. */
const char *lookup_symbol(hwaddr orig_addr);

void monitor_disas(Monitor *mon, CPUState *cpu,
                   hwaddr pc, int nb_insn, int is_physical);

struct syminfo;
struct elf32_sym;
struct elf64_sym;

typedef const char *(*lookup_symbol_t)(struct syminfo *s, uint64_t orig_addr);

struct syminfo {
    lookup_symbol_t lookup_symbol;
    unsigned int disas_num_syms;
    union {
      struct elf32_sym *elf32;
      struct elf64_sym *elf64;
    } disas_symtab;
    const char *disas_strtab;
    struct syminfo *next;
};

/* Filled in by elfload.c.  Simplistic, but will do for now. */
extern struct syminfo *syminfos;

#endif /* QEMU_DISAS_H */
