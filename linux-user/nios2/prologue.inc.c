    {
        env->regs[0] = 0;
        env->regs[1] = regs->r1;
        env->regs[2] = regs->r2;
        env->regs[3] = regs->r3;
        env->regs[4] = regs->r4;
        env->regs[5] = regs->r5;
        env->regs[6] = regs->r6;
        env->regs[7] = regs->r7;
        env->regs[8] = regs->r8;
        env->regs[9] = regs->r9;
        env->regs[10] = regs->r10;
        env->regs[11] = regs->r11;
        env->regs[12] = regs->r12;
        env->regs[13] = regs->r13;
        env->regs[14] = regs->r14;
        env->regs[15] = regs->r15;
        /* TODO: unsigned long  orig_r2; */
        env->regs[R_RA] = regs->ra;
        env->regs[R_FP] = regs->fp;
        env->regs[R_SP] = regs->sp;
        env->regs[R_GP] = regs->gp;
        env->regs[CR_ESTATUS] = regs->estatus;
        env->regs[R_EA] = regs->ea;
        /* TODO: unsigned long  orig_r7; */

        /* Emulate eret when starting thread. */
        env->regs[R_PC] = regs->ea;
    }
