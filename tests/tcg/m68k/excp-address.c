/*
 * Test m68k address exception
 */

#define _GNU_SOURCE 1
#include <signal.h>
#include <stdlib.h>

static void sig_handler(int sig, siginfo_t *si, void *puc)
{
    exit(0);
}

int main(int argc, char **argv)
{
    struct sigaction act = {
        .sa_sigaction = sig_handler,
        .sa_flags = SA_SIGINFO
    };

    sigaction(SIGBUS, &act, NULL);

    /*
     * addl %d0,#0 -- with immediate as destination is illegal.
     * Buggy qemu interpreted the insn as 5 words: 2 for immediate source
     * and another 2 for immediate destination.  Provide all that padding
     * so that abort gets called.
     */
    asm volatile(".word 0xd1bc,0,0,0,0");

    abort();
}
