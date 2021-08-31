#include "qemu/osdep.h"
#include "cpu.h"

static void i386_update_cpu_stub(CPUX86State *cpu)
{
}

static void i386_update_stub(void)
{
}

static void x86_register_ferr_irq_stub(qemu_irq irq)
{
}

struct TCGI386ModuleOps tcg_i386 = {
    .update_fp_status = i386_update_cpu_stub,
    .update_mxcsr_status = i386_update_cpu_stub,
    .update_mxcsr_from_sse_status = i386_update_cpu_stub,
    .x86_register_ferr_irq = x86_register_ferr_irq_stub,
    .cpu_set_ignne = i386_update_stub,
};
