/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static void on_fpe(int sig, siginfo_t *info, void *context)
{
    (void)context;
    _exit(sig != SIGFPE || info->si_code != FPE_INTDIV);
}

int main(void)
{
    struct sigaction act = { .sa_sigaction = on_fpe, .sa_flags = SA_SIGINFO };

    assert(sigemptyset(&act.sa_mask) == 0);
    assert(sigaction(SIGFPE, &act, NULL) == 0);
    for (int bits = 16; bits <= 32; bits += 16) {
        pid_t child = fork();
        int status;

        assert(child >= 0);
        if (child == 0) {
            unsigned int low = 0;
            unsigned int high = bits == 16 ? 0x8000 : 0x80000000;

            if (bits == 16) {
                asm volatile("idivw %%cx" : "+a"(low), "+d"(high)
                             : "c"(-1) : "cc");
            } else {
                asm volatile("idivl %%ecx" : "+a"(low), "+d"(high)
                             : "c"(-1) : "cc");
            }
            _exit(1);
        }
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    return 0;
}
