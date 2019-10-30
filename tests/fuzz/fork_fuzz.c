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

uintptr_t feature_shm;

void counter_shm_init(void)
{
    int fd = shm_open("/qemu-fuzz-cntrs", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("Error: ");
        exit(1);
    }
    if (ftruncate(fd, &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START) == -1) {
        perror("Error: ");
        exit(1);
    }
    /* Copy what's in the counter region to the shm.. */
    void *rptr = mmap(NULL ,
            &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memcpy(rptr,
           &__FUZZ_COUNTERS_START,
           &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START);

    munmap(rptr, &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START);

    /* And map the shm over the counter region */
    rptr = mmap(&__FUZZ_COUNTERS_START,
            &__FUZZ_COUNTERS_END - &__FUZZ_COUNTERS_START,
            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (!rptr) {
        perror("Error: ");
        exit(1);
    }
    return;
}


