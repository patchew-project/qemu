    {
        int i;

        for (i = 0; i < 32; i++) {
            cpu_set_gpr(env, i, regs->gpr[i]);
        }
        env->pc = regs->pc;
        cpu_set_sr(env, regs->sr);
    }
