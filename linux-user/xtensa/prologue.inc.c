    {
        int i;
        for (i = 0; i < 16; ++i) {
            env->regs[i] = regs->areg[i];
        }
        env->sregs[WINDOW_START] = regs->windowstart;
        env->pc = regs->pc;
    }
