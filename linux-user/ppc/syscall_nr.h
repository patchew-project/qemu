/*
 * This file contains the system call numbers.
 */

#ifndef LINUX_USER_PPC_SYSCALL_NR_H
#define LINUX_USER_PPC_SYSCALL_NR_H

#if !defined(TARGET_PPC64) || defined(TARGET_ABI32)
#include "syscall32_nr.h"
#else
#include "syscall64_nr.h"
#endif

#endif /* LINUX_USER_PPC_SYSCALL_NR_H */
