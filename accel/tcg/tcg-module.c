#include "qemu/osdep.h"
#include "tcg/tcg-module.h"

static void update_cpu_stub(CPUState *cpu)
{
}

static void tlb_flush_page_stub(CPUState *cpu, target_ulong addr)
{
}

#if defined(CONFIG_SOFTMMU)
static void tlb_reset_dirty_stub(CPUState *cpu, ram_addr_t start1, ram_addr_t length)
{
}

static bool tlb_plugin_lookup_stub(CPUState *cpu, target_ulong addr, int mmu_idx,
                                   bool is_store, struct qemu_plugin_hwaddr *data)
{
    return false;
}
#endif

static void tcg_exec_realizefn_stub(CPUState *cpu, Error **errp)
{
}

static void tb_invalidate_phys_range_stub(tb_page_addr_t start, tb_page_addr_t end)
{
}

static void tb_check_watchpoint_stub(CPUState *cpu, uintptr_t retaddr)
{
}

struct TCGModuleOps tcg = {
    .tlb_flush = update_cpu_stub,
    .tlb_flush_page = tlb_flush_page_stub,
#if defined(CONFIG_SOFTMMU)
    .tlb_reset_dirty = tlb_reset_dirty_stub,
    .tlb_plugin_lookup = tlb_plugin_lookup_stub,
#endif
    .tcg_exec_realizefn = tcg_exec_realizefn_stub,
    .tcg_exec_unrealizefn = update_cpu_stub,
    .tb_flush = update_cpu_stub,
    .tb_invalidate_phys_range = tb_invalidate_phys_range_stub,
    .tb_check_watchpoint = tb_check_watchpoint_stub,
};
