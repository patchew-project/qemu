#ifndef OPENRISC_TARGET_SYSCALL_H
#define OPENRISC_TARGET_SYSCALL_H

struct target_pt_regs {
    abi_ulong gpr[32];
    abi_ulong pc;
    abi_ulong sr;
};

#define UNAME_MACHINE "openrisc"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_MINSIGSTKSZ 2048
#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2

#define MMAP_SHIFT TARGET_PAGE_BITS

#endif /* OPENRISC_TARGET_SYSCALL_H */
