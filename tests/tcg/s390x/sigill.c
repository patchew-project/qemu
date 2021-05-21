#include <assert.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

/*
 * The labels for the instruction that generates a SIGILL and for the one that
 * follows it. They could have been defined in a separate .s file, but this
 * would complicate the build, so use the inline asm instead.
 */

void expected_si_addr(void);
void expected_psw_addr(void);

asm(".globl\texpected_si_addr\n"
    "expected_si_addr:\t.byte\t0x00,0x00\n"
    "\t.globl\texpected_psw_addr\n"
    "expected_psw_addr:\tbr\t%r14");

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
    int ret;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_signal;
    act.sa_flags = SA_SIGINFO;

    ret = sigaction(SIGILL, &act, NULL);
    assert(ret == 0);

    expected_si_addr();

    return 0;
}
