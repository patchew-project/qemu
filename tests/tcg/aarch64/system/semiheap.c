/*
 * Semihosting System HEAPINFO Test
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <inttypes.h>
#include <stddef.h>
#include <minilib.h>

#define SYS_HEAPINFO    0x16

uintptr_t __semi_call(uintptr_t type, uintptr_t arg0)
{
    register uintptr_t t asm("x0") = type;
    register uintptr_t a0 asm("x1") = arg0;
    asm("hlt 0xf000"
        : "=r" (t)
        : "r" (t), "r" (a0));

    return t;
}

int main(int argc, char *argv[argc])
{
    struct {
        void *heap_base;
        void *heap_limit;
        void *stack_base;
        void *stack_limit;
    } info;
    void *ptr_to_info = (void *) &info;

    ml_printf("Semihosting Heap Info Test\n");

    /* memset(&info, 0, sizeof(info)); */
    __semi_call(SYS_HEAPINFO, (uintptr_t) &ptr_to_info);

    if (info.heap_base == NULL || info.heap_limit == NULL) {
        ml_printf("null heap: %p -> %p\n", info.heap_base, info.heap_limit);
        return -1;
    }

    /* Error if heap base is above limit */
    if ((uintptr_t) info.heap_base >= (uintptr_t) info.heap_limit) {
        ml_printf("heap base %p >= heap_limit %p\n",
               info.heap_base, info.heap_limit);
        return -2;
    }

    if (info.stack_base == NULL) {
        ml_printf("null stack: %p -> %p\n", info.stack_base, info.stack_limit);
        return -3;
    }

    /*
     * We don't check our local variables are inside the reported
     * stack because the runtime may select a different stack area (as
     * our boot.S code does). However we can check we don't clash with
     * the heap.
     */
    if (ptr_to_info > info.heap_base && ptr_to_info < info.heap_limit) {
        ml_printf("info appears to be inside the heap: %p in %p:%p\n",
               ptr_to_info, info.heap_base, info.heap_limit);
        return -4;
    }

    ml_printf("heap: %p -> %p\n", info.heap_base, info.heap_limit);
    ml_printf("stack: %p <- %p\n", info.stack_limit, info.stack_base);
    ml_printf("Passed HeapInfo checks\n");
    return 0;
}
