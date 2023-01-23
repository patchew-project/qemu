#ifndef LINUX_USER_LOONGARCH_SYSCALL_NR_H
#define LINUX_USER_LOONGARCH_SYSCALL_NR_H

#include "syscall_base_nr.h"

/* Restore various syscalls for old world.  */
#if defined(TARGET_ABI_LOONGARCH64_OW)
#define TARGET_NR_newfstatat 79
#define TARGET_NR_fstat 80
#define TARGET_NR_getrlimit 163
#define TARGET_NR_setrlimit 164
#endif

#endif /* LINUX_USER_LOONGARCH_SYSCALL_NR_H */
