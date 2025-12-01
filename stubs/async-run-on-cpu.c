#include "qemu/osdep.h"
#include "hw/core/cpu.h"

void async_run_on_cpu(CPUState *cpu, run_on_cpu_func func, run_on_cpu_data data)
{
    abort();
}
