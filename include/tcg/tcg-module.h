#ifndef TCG_MODULE_H
#define TCG_MODULE_H

struct TCGModuleOps {
    void (*tlb_flush)(CPUState *cpu);
};
extern struct TCGModuleOps tcg;

#endif /* TCG_MODULE_H */
