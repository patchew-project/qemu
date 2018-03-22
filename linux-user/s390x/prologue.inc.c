    {
            int i;
            for (i = 0; i < 16; i++) {
                env->regs[i] = regs->gprs[i];
            }
            env->psw.mask = regs->psw.mask;
            env->psw.addr = regs->psw.addr;
    }
