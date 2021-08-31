#ifndef TCG_MODULE_H
#define TCG_MODULE_H

#include "exec/exec-all.h"

struct TCGModuleOps {
    void (*tlb_flush)(CPUState *cpu);
    void (*tlb_flush_page)(CPUState *cpu, target_ulong addr);
#if defined(CONFIG_SOFTMMU)
    void (*tlb_reset_dirty)(CPUState *cpu, ram_addr_t start1, ram_addr_t length);
#endif
};
extern struct TCGModuleOps tcg;

#endif /* TCG_MODULE_H */
