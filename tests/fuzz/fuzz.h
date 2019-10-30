/*
 * fuzzing driver
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef FUZZER_H_
#define FUZZER_H_

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "tests/libqtest.h"


typedef struct FuzzTarget {
    const char *name;         /* command-line option(FUZZ_TARGET) for the target */
    const char *description;  /* help text */


    /* returns the arg-list that is passed to qemu/softmmu init() */
    char* (*get_init_cmdline)(struct FuzzTarget *);

    /*
     * will run once, prior to running qemu/softmmu init.
     * eg: set up shared-memory for communication with the child-process
     */
    void(*pre_vm_init)(void);

    /*
     * will run once, prior to to the fuzz-loop.
     * eg: detect the memory map
     */
    void(*pre_fuzz)(QTestState *);

    /*
     * accepts and executes an input from libfuzzer. this is repeatedly
     * executed during the fuzzing loop. Its should handle setup, input
     * execution and cleanup
     */
    void(*fuzz)(QTestState *, const unsigned char *, size_t);

} FuzzTarget;

void flush_events(QTestState *);
void reboot(QTestState *);

/*
 * makes a copy of *target and adds it to the target-list.
 * i.e. fine to set up target on the caller's stack
 */
void fuzz_add_target(FuzzTarget *target);

int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size);
int LLVMFuzzerInitialize(int *argc, char ***argv, char ***envp);

#endif

