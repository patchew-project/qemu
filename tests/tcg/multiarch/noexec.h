/*
 * Common code for arch-specific MMU_INST_FETCH fault testing.
 *
 * Declare struct arch_noexec_test before including this file and define
 * arch_check_mcontext() after that.
 */

#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

/* Forward declarations. */

static void arch_check_mcontext(const struct arch_noexec_test *test,
                                const mcontext_t *ctx);

/* Utility functions. */

static void safe_print(const char *s)
{
    write(0, s, strlen(s));
}

static void safe_puts(const char *s)
{
    safe_print(s);
    safe_print("\n");
}

#define PAGE_ALIGN(p) (void *)((unsigned long)(p) & ~0xfffUL)

/* Testing infrastructure. */

struct noexec_test {
    const char *name;
    void (*func)(int);
    void *page;
    void *expected_si_addr;
    struct arch_noexec_test arch;
};

static const struct noexec_test *current_noexec_test;

static void handle_segv(int sig, siginfo_t *info, void *ucontext)
{
    int err;

    if (current_noexec_test == NULL) {
        safe_puts("[  FAILED  ] unexpected SEGV");
        _exit(1);
    }

    if (info->si_addr != current_noexec_test->expected_si_addr) {
        safe_puts("[  FAILED  ] wrong si_addr");
        _exit(1);
    }

    arch_check_mcontext(&current_noexec_test->arch,
                        &((ucontext_t *)ucontext)->uc_mcontext);

    err = mprotect(current_noexec_test->page, 0x1000, PROT_READ | PROT_EXEC);
    if (err != 0) {
        safe_puts("[  FAILED  ] mprotect() failed");
        _exit(1);
    }

    current_noexec_test = NULL;
}

static void test_noexec_1(const struct noexec_test *test)
{
    int ret;

    /* Trigger TB creation in order to test invalidation. */
    test->func(0);

    ret = mprotect(test->page, 0x1000, PROT_NONE);
    assert(ret == 0);

    /* Trigger SEGV and check that handle_segv() ran. */
    current_noexec_test = test;
    test->func(0);
    assert(current_noexec_test == NULL);
}

static int test_noexec(struct noexec_test *tests, size_t n_tests)
{
    struct sigaction act;
    size_t i;
    int err;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_segv;
    act.sa_flags = SA_SIGINFO;
    err = sigaction(SIGSEGV, &act, NULL);
    assert(err == 0);

    for (i = 0; i < n_tests; i++) {
        struct noexec_test *test = &tests[i];

        safe_print("[ RUN      ] ");
        safe_puts(test->name);
        test_noexec_1(test);
        safe_puts("[       OK ]");
    }

    safe_puts("[  PASSED  ]");

    return EXIT_SUCCESS;
}
