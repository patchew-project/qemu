#ifndef TCG_MODULE_H
#define TCG_MODULE_H

#include "exec/exec-all.h"

struct TCGModuleOps {
    void (*tlb_flush)(CPUState *cpu);
    void (*tlb_flush_page)(CPUState *cpu, target_ulong addr);
#if defined(CONFIG_SOFTMMU)
    void (*tlb_reset_dirty)(CPUState *cpu, ram_addr_t start1, ram_addr_t length);
    bool (*tlb_plugin_lookup)(CPUState *cpu, target_ulong addr, int mmu_idx,
                              bool is_store, struct qemu_plugin_hwaddr *data);
#endif
    void (*tcg_exec_unrealizefn)(CPUState *cpu);
    void (*tcg_exec_realizefn)(CPUState *cpu, Error **errp);
    void (*tb_flush)(CPUState *cpu);
    void (*tb_invalidate_phys_range)(tb_page_addr_t start, tb_page_addr_t end);
    void (*tb_check_watchpoint)(CPUState *cpu, uintptr_t retaddr);
};
extern struct TCGModuleOps tcg;

#endif /* TCG_MODULE_H */
