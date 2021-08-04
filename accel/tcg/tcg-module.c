#include "qemu/osdep.h"
#include "tcg/tcg-module.h"

static void update_cpu_stub(CPUState *cpu)
{
}

struct TCGModuleOps tcg = {
    .tlb_flush = update_cpu_stub,
};
