#ifndef NIOS2_TARGET_SYSCALL_H
#define NIOS2_TARGET_SYSCALL_H

#define UNAME_MACHINE "nios2"
#define UNAME_MINIMUM_RELEASE "3.19.0"

struct target_pt_regs {
    target_ulong sp;
    target_ulong pc;
};

#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#endif /* NIOS2_TARGET_SYSCALL_H */
