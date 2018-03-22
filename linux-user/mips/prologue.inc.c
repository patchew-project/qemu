    {
        int i;

        for(i = 0; i < 32; i++) {
            env->active_tc.gpr[i] = regs->regs[i];
        }
        env->active_tc.PC = regs->cp0_epc & ~(target_ulong)1;
        if (regs->cp0_epc & 1) {
            env->hflags |= MIPS_HFLAG_M16;
        }
        if (((info->elf_flags & EF_MIPS_NAN2008) != 0) !=
            ((env->active_fpu.fcr31 & (1 << FCR31_NAN2008)) != 0)) {
            if ((env->active_fpu.fcr31_rw_bitmask &
                  (1 << FCR31_NAN2008)) == 0) {
                fprintf(stderr, "ELF binary's NaN mode not supported by CPU\n");
                exit(1);
            }
            if ((info->elf_flags & EF_MIPS_NAN2008) != 0) {
                env->active_fpu.fcr31 |= (1 << FCR31_NAN2008);
            } else {
                env->active_fpu.fcr31 &= ~(1 << FCR31_NAN2008);
            }
            restore_snan_bit_mode(env);
        }
    }
