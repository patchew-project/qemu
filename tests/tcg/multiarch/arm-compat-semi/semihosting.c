/*
 * linux-user semihosting checks
 *
 * Copyright (c) 2019
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define SYS_WRITE0      0x04
#define SYS_HEAPINFO    0x16
#define SYS_REPORTEXC   0x18

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "semicall.h"

int main(int argc, char *argv[argc])
{
#if UINTPTR_MAX == UINT32_MAX
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

    __semi_call(SYS_WRITE0, (uintptr_t) "Checking HeapInfo\n");

    memset(&info, 0, sizeof(info));
    __semi_call(SYS_HEAPINFO, (uintptr_t) &ptr_to_info);

    if (info.heap_base == NULL || info.heap_limit == NULL) {
        printf("null heap: %p -> %p\n", info.heap_base, info.heap_limit);
        exit(1);
    } else if (info.heap_base != NULL && info.heap_limit != NULL) {
        /* Error if heap base is above limit */
        if ((uintptr_t) info.heap_base >= (uintptr_t) info.heap_limit) {
            printf("heap base %p >= heap_limit %p\n",
                   info.heap_base, info.heap_limit);
            exit(2);
        }
    }

    if (info.stack_base == NULL) {
        printf("null stack: %p -> %p\n", info.stack_base, info.stack_limit);
        exit(3);
    } else if (info.stack_base != NULL && info.stack_limit != NULL) {
        /* Error if stack base is below limit */
        if ((uintptr_t) info.stack_base < (uintptr_t) info.stack_limit) {
            printf("stack base %p < stack_limit %p\n",
                   info.stack_base, info.stack_limit);
            exit(4);
        }
    }
    printf("heap: %p -> %p\n", info.heap_base, info.heap_limit);
    printf("stack: %p -> %p\n", info.stack_base, info.stack_limit);

    __semi_call(SYS_WRITE0, (uintptr_t) "Passed HeapInfo checks");
    __semi_call(SYS_REPORTEXC, exit_code);
    /* if we get here we failed */
    return -1;
}
