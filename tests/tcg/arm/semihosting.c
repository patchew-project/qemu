/*
 * linux-user semihosting checks
 *
 * Copyright (c) 2019
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE  /* asprintf is a GNU extension */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "semicall.h"

int main(int argc, char *argv[argc])
{
#if defined(__arm__)
    uintptr_t exit_code = 0x20026;
#else
    uintptr_t exit_block[2] = {0x20026, 0};
    uintptr_t exit_code = (uintptr_t) &exit_block;
#endif
    struct {
        void *heap_base;
        void *heap_limit;
        void *stack_base;
        void *stack_limit;
    } info;
    void *ptr_to_info = (void *) &info;
    char *heap_info, *stack_info;
    void *brk = sbrk(0);

    __semi_call(SYS_WRITE0, (uintptr_t) "Hello World\n");

    memset(&info, 0, sizeof(info));
    __semi_call(SYS_HEAPINFO, (uintptr_t) &ptr_to_info);

    asprintf(&heap_info, "heap: %p -> %p\n", info.heap_base, info.heap_limit);
    __semi_call(SYS_WRITE0, (uintptr_t) heap_info);
    if (info.heap_base != brk) {
        sprintf(heap_info, "heap mismatch: %p\n", brk);
        __semi_call(SYS_WRITE0, (uintptr_t) heap_info);
        return -1;
    }

    asprintf(&stack_info, "stack: %p -> %p\n", info.stack_base, info.stack_limit);
    __semi_call(SYS_WRITE0, (uintptr_t) stack_info);
    free(heap_info);
    free(stack_info);

    __semi_call(SYS_REPORTEXC, exit_code);
    /* if we get here we failed */
    return -1;
}
