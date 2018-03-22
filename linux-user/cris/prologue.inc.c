    {
	    env->regs[0] = regs->r0;
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
	    env->regs[14] = info->start_stack;
	    env->regs[15] = regs->acr;
	    env->pc = regs->erp;
    }
