#include "qemu/osdep.h"
#include "tcg/tcg-module.h"

static void update_cpu_stub(CPUState *cpu)
{
}

static void tlb_flush_page_stub(CPUState *cpu, target_ulong addr)
{
}

struct TCGModuleOps tcg = {
    .tlb_flush = update_cpu_stub,
    .tlb_flush_page = tlb_flush_page_stub,
};
