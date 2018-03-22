    {
        int i;
        cpsr_write(env, regs->uregs[16], CPSR_USER | CPSR_EXEC,
                   CPSRWriteByInstr);
        for(i = 0; i < 16; i++) {
            env->regs[i] = regs->uregs[i];
        }
#ifdef TARGET_WORDS_BIGENDIAN
        /* Enable BE8.  */
        if (EF_ARM_EABI_VERSION(info->elf_flags) >= EF_ARM_EABI_VER4
            && (info->elf_flags & EF_ARM_BE8)) {
            env->uncached_cpsr |= CPSR_E;
            env->cp15.sctlr_el[1] |= SCTLR_E0E;
        } else {
            env->cp15.sctlr_el[1] |= SCTLR_B;
        }
#endif
    }

    ts->stack_base = info->start_stack;
    ts->heap_base = info->brk;
    /* This will be filled in on the first SYS_HEAPINFO call.  */
    ts->heap_limit = 0;
