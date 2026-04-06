/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../passthrough-protocol.h"
#include "passthrough-gtl.h"

static long passthrough_syscall(uint64_t op, uint64_t a2, uint64_t a3,
                                uint64_t a4)
{
    return syscall(PASSTHROUGH_MAGIC_SYSCALL, op, a2, a3, a4);
}

uint64_t passthrough_load_htl(const char *library)
{
    return (uint64_t)passthrough_syscall(PASSTHROUGH_OP_LOAD_HTL,
                                         (uint64_t)(uintptr_t)library, 0, 0);
}

uint64_t passthrough_dlsym(uint64_t handle, const char *symbol)
{
    return (uint64_t)passthrough_syscall(PASSTHROUGH_OP_DLSYM, handle,
                                         (uint64_t)(uintptr_t)symbol, 0);
}

void passthrough_invoke(PassthroughThunkEntry entry, void **args, void *ret)
{
    passthrough_syscall(PASSTHROUGH_OP_INVOKE,
                        (uint64_t)(uintptr_t)entry,
                        (uint64_t)(uintptr_t)args,
                        (uint64_t)(uintptr_t)ret);
}

void passthrough_close_htl(uint64_t handle)
{
    passthrough_syscall(PASSTHROUGH_OP_CLOSE_HTL, handle, 0, 0);
}
