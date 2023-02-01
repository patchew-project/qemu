/*
 * Test that a fork()ed process terminates after __builtin_trap().
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    struct rlimit nodump;
    pid_t err, pid;
    int wstatus;

    pid = fork();
    assert(pid != -1);
    if (pid == 0) {
        /* We are about to crash on purpose; disable core dumps. */
        if (getrlimit(RLIMIT_CORE, &nodump)) {
            return EXIT_FAILURE;
        }
        nodump.rlim_cur = 0;
        if (setrlimit(RLIMIT_CORE, &nodump)) {
            return EXIT_FAILURE;
        }
        /*
         * An alternative would be to dereference a NULL pointer, but that
         * would be an UB in C.
         */
#if defined(__MICROBLAZE__)
        /*
         * gcc emits "bri 0", which is an endless loop.
         * Take glibc's ABORT_INSTRUCTION.
         */
        asm volatile("brki r0,-1");
#else
        __builtin_trap();
#endif
    }
    err = waitpid(pid, &wstatus, 0);
    assert(err == pid);
    assert(WIFSIGNALED(wstatus));

    return EXIT_SUCCESS;
}
