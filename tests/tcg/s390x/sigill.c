#include <assert.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

extern char expected_si_addr[];
extern char expected_psw_addr[];

static void handle_signal(int sig, siginfo_t *info, void *ucontext)
{
    if (sig != SIGILL) {
        _exit(1);
    }

    if (info->si_addr != expected_si_addr) {
        _exit(2);
    }

    if (((ucontext_t *)ucontext)->uc_mcontext.psw.addr !=
            (unsigned long)expected_psw_addr) {
        _exit(3);
    }
}

int main(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_signal;
    act.sa_flags = SA_SIGINFO;

    int ret = sigaction(SIGILL, &act, NULL);
    assert(ret == 0);

    asm volatile("expected_si_addr:\t.byte\t0x00,0x00\n"
                 "expected_psw_addr:");

    return 0;
}
