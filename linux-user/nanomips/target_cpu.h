#ifndef NANOMIPS_TARGET_CPU_H
#define NANOMIPS_TARGET_CPU_H

static inline void cpu_clone_regs(CPUMIPSState *env, target_ulong newsp)
{
    if (newsp) {
        env->active_tc.gpr[29] = newsp;
    }
    env->active_tc.gpr[4] = 0;
}

static inline void cpu_set_tls(CPUMIPSState *env, target_ulong newtls)
{
    env->active_tc.CP0_UserLocal = newtls;
}

static inline abi_ulong get_sp_from_cpustate(CPUMIPSState *state)
{
    return state->active_tc.gpr[29];
}
#endif
