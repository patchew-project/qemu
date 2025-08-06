#ifndef SH4_TARGET_SYSCALL_H
#define SH4_TARGET_SYSCALL_H

#define UNAME_MACHINE "sh4"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#define TARGET_FORCE_SHMLBA

static inline abi_ulong target_shmlba(CPUSH4State *env)
{
    return 0x4000;
}

#endif /* SH4_TARGET_SYSCALL_H */
