#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <stdint.h>

void __attribute__((noinline)) bar(void)
{
    exit(EXIT_SUCCESS);
}

void __attribute__((noinline, ms_abi)) foo(void)
{
    /*
     * With ms_abi, there are call-saved xmm registers, which are forced
     * to the stack around the call to sysv_abi bar().  If the signal
     * stack frame is not properly aligned, movaps will raise #GP.
     */
    bar();
}

void sighandler(int num)
{
    void* sp = __builtin_dwarf_cfa();
    assert((uintptr_t)sp % 16 == 0);
    foo();
}

int main(void)
{
    signal(SIGUSR1, sighandler);
    raise(SIGUSR1);
    abort();
}
