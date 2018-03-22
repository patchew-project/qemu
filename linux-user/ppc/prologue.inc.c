    {
        int i;

#if defined(TARGET_PPC64)
        int flag = (env->insns_flags2 & PPC2_BOOKE206) ? MSR_CM : MSR_SF;
#if defined(TARGET_ABI32)
        env->msr &= ~((target_ulong)1 << flag);
#else
        env->msr |= (target_ulong)1 << flag;
#endif
#endif
        env->nip = regs->nip;
        for(i = 0; i < 32; i++) {
            env->gpr[i] = regs->gpr[i];
        }
    }
