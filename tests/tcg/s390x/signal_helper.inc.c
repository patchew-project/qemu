#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <setjmp.h>
#include "helper.h"

jmp_buf jmp_env;

static void sig_sigill(int sig)
{
    if (sig != SIGILL) {
        check("Wrong signal received", false);
    }
    longjmp(jmp_env, 1);
}

#define CHECK_SIGILL(STATEMENT)                             \
do {                                                        \
    struct sigaction act;                                   \
                                                            \
    act.sa_handler = sig_sigill;                            \
    act.sa_flags = 0;                                       \
    if (sigaction(SIGILL, &act, NULL)) {                    \
        check("SIGILL handler not registered", false);      \
    }                                                       \
                                                            \
    if (setjmp(jmp_env) == 0) {                             \
        STATEMENT;                                          \
        check("SIGILL not triggered", false);               \
    }                                                       \
                                                            \
    act.sa_handler = SIG_DFL;                               \
    sigemptyset(&act.sa_mask);                              \
    act.sa_flags = 0;                                       \
    if (sigaction(SIGILL, &act, NULL)) {                    \
        check("SIGILL handler not unregistered", false);    \
    }                                                       \
} while (0)
