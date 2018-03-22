    {
        int i;
        for (i = 0; i < TILEGX_R_COUNT; i++) {
            env->regs[i] = regs->regs[i];
        }
        for (i = 0; i < TILEGX_SPR_COUNT; i++) {
            env->spregs[i] = 0;
        }
        env->pc = regs->pc;
    }
