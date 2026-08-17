/*
 * A loop whose only back edge is an indirect branch must still be
 * interruptible.
 *
 * Blocks that dispatch indirectly do not emit the icount_decr poll; a pending
 * exit instead poisons the inline jump cache probe so that the dispatch falls
 * into helper_lookup_tb_ptr(), which returns to the main loop. If that
 * mechanism breaks, this program never leaves the loop and the test times
 * out rather than failing an assertion.
 *
 * A computed goto is used deliberately: a plain while(1) closes the cycle
 * with a direct backward branch, which is still polled, and so would not
 * exercise the path under test.
 */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t fired;
static volatile unsigned long iterations;

static void handler(int sig)
{
    fired = 1;
}

int main(void)
{
    /*
     * Indexing a table with a volatile index, rather than jumping through a
     * volatile pointer: gcc happily proves a single-valued pointer constant
     * and emits a direct branch, which is the case this test is not about.
     */
    void *target[2];
    volatile int idx = 0;

    assert(signal(SIGALRM, handler) != SIG_ERR);
    alarm(1);

    target[0] = &&spin;
    target[1] = &&out;
spin:
    iterations++;
    if (!fired) {
        goto *target[idx];
    }
out:

    printf("interrupted after %lu iterations\n", iterations);
    return 0;
}
