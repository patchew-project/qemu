#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"

uint32_t vfp_get_fpscr(CPUARMState *env)
{
    return 0;
}

void vfp_set_fpscr(CPUARMState *env, uint32_t val)
{
}

bool arm_is_psci_call(ARMCPU *cpu, int excp_type)
{
    return false;
}

void arm_handle_psci_call(ARMCPU *cpu)
{
    abort();
}
