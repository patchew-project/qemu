    {
        int i;
        for (i = 1; i < 32; i++) {
            env->gr[i] = regs->gr[i];
        }
        env->iaoq_f = regs->iaoq[0];
        env->iaoq_b = regs->iaoq[1];
    }
