/*
 * Test the lowest and the highest real-time signals.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool seen_sigrtmin, seen_sigrtmax;

static void handle_signal(int sig)
{
    if (sig == SIGRTMIN) {
        seen_sigrtmin = true;
    } else if (sig == SIGRTMAX) {
        seen_sigrtmax = true;
    } else {
        _exit(1);
    }
}

int main(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_handler = handle_signal;
    assert(sigaction(SIGRTMIN, &act, NULL) == 0);
    assert(sigaction(SIGRTMAX, &act, NULL) == 0);

    assert(kill(getpid(), SIGRTMIN) == 0);
    assert(seen_sigrtmin);
    assert(kill(getpid(), SIGRTMAX) == 0);
    assert(seen_sigrtmax);

    return EXIT_SUCCESS;
}
