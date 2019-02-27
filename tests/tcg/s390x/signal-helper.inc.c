#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include "helper.h"

jmp_buf jmp_env;

static int signal_unblock(int sig)
{
    sigset_t intmask;

    if (sigemptyset(&intmask) ||
        sigaddset(&intmask, sig) ||
        sigprocmask(SIG_UNBLOCK, &intmask, NULL)) {
        return -errno;
    }
    return 0;
}

static void handle_sigill(int sig)
{
    if (sig != SIGILL) {
        check("Wrong signal received", false);
    }
    if (signal_unblock(sig)) {
        check("Cannot unblock signal", false);
    }
    longjmp(jmp_env, 1);
}

#define CHECK_SIGILL(STATEMENT)                         \
do {                                                    \
    if (signal(SIGILL, handle_sigill) == SIG_ERR) {     \
        check("SIGILL not registered", false);          \
    }                                                   \
    if (setjmp(jmp_env) == 0) {                         \
        STATEMENT;                                      \
        check("SIGILL not triggered", false);           \
    }                                                   \
    if (signal(SIGILL, SIG_DFL) == SIG_ERR) {           \
        check("SIGILL not registered", false);          \
    }                                                   \
} while (0)
