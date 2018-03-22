    {
        int i;

        if (!(arm_feature(env, ARM_FEATURE_AARCH64))) {
            fprintf(stderr,
                    "The selected ARM CPU does not support 64 bit mode\n");
            exit(EXIT_FAILURE);
        }

        for (i = 0; i < 31; i++) {
            env->xregs[i] = regs->regs[i];
        }
        env->pc = regs->pc;
        env->xregs[31] = regs->sp;
#ifdef TARGET_WORDS_BIGENDIAN
        env->cp15.sctlr_el[1] |= SCTLR_E0E;
        for (i = 1; i < 4; ++i) {
            env->cp15.sctlr_el[i] |= SCTLR_EE;
        }
#endif
    }
