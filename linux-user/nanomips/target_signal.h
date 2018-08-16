#ifndef NANOMIPS_TARGET_SIGNAL_H
#define NANOMIPS_TARGET_SIGNAL_H

#include "../generic/signal.h"
#undef TARGET_SIGRTMIN
#define TARGET_SIGRTMIN       35

/* this struct defines a stack used during syscall handling */
typedef struct target_sigaltstack {
    abi_long ss_sp;
    abi_ulong ss_size;
    abi_long ss_flags;
} target_stack_t;

/* sigaltstack controls */
#define TARGET_SS_ONSTACK     1
#define TARGET_SS_DISABLE     2

#define TARGET_MINSIGSTKSZ    6144
#define TARGET_SIGSTKSZ       12288

#endif
