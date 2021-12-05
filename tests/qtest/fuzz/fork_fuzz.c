/*
 * Fork-based fuzzing helpers
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

#include "qemu/osdep.h"
#include "fork_fuzz.h"


void counter_shm_init(void)
{
    /* Copy what's in the counter region to a temporary buffer.. */
    void *copy = malloc(&__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START);
    memcpy(copy,
           &__FUZZ_COUNTERS_START,
           &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START);

    /* Map a shared region over the counter region */
    if (mmap(&__FUZZ_COUNTERS_START,
             &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START,
             PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS,
             0, 0) == MAP_FAILED) {
        perror("Error: ");
        exit(1);
    }

    /* Copy the original data back to the counter-region */
    memcpy(&__FUZZ_COUNTERS_START, copy,
           &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START);
    free(copy);
}

/* Returns true in child process */
bool fork_fuzzer_and_wait(void)
{
    pid_t pid;
    int wstatus;

    pid = fork();
    if (pid < 0) {
        perror("fork");
        abort();
    }

    if (pid == 0) {
        return true;
    }

    if (waitpid(pid, &wstatus, 0) < 0) {
        perror("waitpid");
        abort();
    }

    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
        abort();
    }

    return false;
}
