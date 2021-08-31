#ifndef TCG_MODULE_H
#define TCG_MODULE_H

#include "exec/exec-all.h"

struct TCGModuleOps {
    void (*tlb_flush)(CPUState *cpu);
    void (*tlb_flush_page)(CPUState *cpu, target_ulong addr);
};
extern struct TCGModuleOps tcg;

#endif /* TCG_MODULE_H */
