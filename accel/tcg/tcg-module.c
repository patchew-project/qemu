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
#endif

struct TCGModuleOps tcg = {
    .tlb_flush = update_cpu_stub,
    .tlb_flush_page = tlb_flush_page_stub,
#if defined(CONFIG_SOFTMMU)
    .tlb_reset_dirty = tlb_reset_dirty_stub,
#endif
};
