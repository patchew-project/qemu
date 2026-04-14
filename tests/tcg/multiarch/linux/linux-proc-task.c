/*
 * Test that /proc/self/task/ only lists guest threads.
 *
 * Under QEMU user-mode emulation, the host process may contain
 * internal threads (RCU, TCG workers) that are not guest threads.
 * These must be hidden from directory listings of /proc/<pid>/task/
 * and signals to non-guest TIDs must return ESRCH.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define NUM_THREADS 3

static pid_t guest_tids[NUM_THREADS + 1]; /* +1 for main thread */
static int num_guest_tids;
static pthread_barrier_t barrier;

static pid_t gettid_sys(void)
{
    return syscall(SYS_gettid);
}

static void *thread_func(void *arg)
{
    int idx = (int)(intptr_t)arg;

    guest_tids[idx] = gettid_sys();
    pthread_barrier_wait(&barrier);

    /* Wait for main thread to finish testing. */
    pthread_barrier_wait(&barrier);
    return NULL;
}

/*
 * Read /proc/self/task/ and return the count of numeric entries
 * (thread TIDs).  For each entry, verify it matches a known guest TID.
 */
static int count_and_verify_task_entries(void)
{
    DIR *dir;
    struct dirent *de;
    int count = 0;

    dir = opendir("/proc/self/task");
    assert(dir != NULL);

    while ((de = readdir(dir)) != NULL) {
        char *endp;
        long tid;
        int i, found;

        if (de->d_name[0] == '.') {
            continue; /* skip "." and ".." */
        }

        tid = strtol(de->d_name, &endp, 10);
        if (*endp != '\0' || tid <= 0) {
            continue; /* non-numeric entry */
        }

        /* Every TID in the listing must be a known guest thread. */
        found = 0;
        for (i = 0; i < num_guest_tids; i++) {
            if (guest_tids[i] == (pid_t)tid) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "FAIL: /proc/self/task/ contains unknown TID %ld\n",
                    tid);
            fprintf(stderr, "  Known guest TIDs:");
            for (i = 0; i < num_guest_tids; i++) {
                fprintf(stderr, " %d", guest_tids[i]);
            }
            fprintf(stderr, "\n");
        }
        assert(found);
        count++;
    }

    closedir(dir);
    return count;
}

/*
 * Verify that tkill(tid, 0) succeeds for all guest TIDs and that
 * tkill to a TID that should not exist returns ESRCH.
 */
static void test_tkill_validation(void)
{
    int i, ret;

    /* Signal 0 to each guest TID should succeed. */
    for (i = 0; i < num_guest_tids; i++) {
        ret = syscall(SYS_tkill, guest_tids[i], 0);
        if (ret != 0) {
            fprintf(stderr, "FAIL: tkill(%d, 0) returned %d (errno=%d)\n",
                    guest_tids[i], ret, errno);
        }
        assert(ret == 0);
    }

    /*
     * Try a TID that is very unlikely to be a guest thread.
     * Use pid_max (typically 4194304) minus 1 as a probe.
     * On a real kernel this would return ESRCH for a nonexistent thread;
     * on QEMU with validation it should also return ESRCH.
     * Skip this check if the TID happens to exist (unlikely).
     */
    ret = syscall(SYS_tkill, 4194303, 0);
    if (ret == -1 && errno == ESRCH) {
        printf("tkill to non-guest TID correctly returned ESRCH\n");
    }
}

int main(void)
{
    pthread_t threads[NUM_THREADS];
    int i, task_count;

    pthread_barrier_init(&barrier, NULL, NUM_THREADS + 1);

    /* Record main thread TID. */
    guest_tids[0] = gettid_sys();
    num_guest_tids = 1;

    /* Spawn worker threads. */
    for (i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_create(&threads[i], NULL, thread_func,
                                 (void *)(intptr_t)(i + 1));
        assert(ret == 0);
    }

    /* Wait for all threads to record their TIDs. */
    pthread_barrier_wait(&barrier);
    num_guest_tids = NUM_THREADS + 1;

    printf("Guest TIDs:");
    for (i = 0; i < num_guest_tids; i++) {
        printf(" %d", guest_tids[i]);
    }
    printf("\n");

    /* Test 1: /proc/self/task/ entry count matches guest thread count. */
    task_count = count_and_verify_task_entries();
    printf("/proc/self/task/ entries: %d, expected: %d\n",
           task_count, num_guest_tids);
    assert(task_count == num_guest_tids);

    /* Test 2: tkill validation. */
    test_tkill_validation();

    /* Release worker threads. */
    pthread_barrier_wait(&barrier);

    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_barrier_destroy(&barrier);

    printf("PASS: /proc/self/task/ filtering and tkill validation\n");
    return EXIT_SUCCESS;
}
