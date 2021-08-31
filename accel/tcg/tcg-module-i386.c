#include "qemu/osdep.h"
#include "cpu.h"

static void i386_update_cpu_stub(CPUX86State *cpu)
{
}

struct TCGI386ModuleOps tcg_i386 = {
    .update_fp_status = i386_update_cpu_stub,
};
