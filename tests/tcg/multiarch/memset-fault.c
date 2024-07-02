#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>

#if defined(__powerpc64__)
/* Needed for PT_* constants */
#include <asm/ptrace.h>
#endif

static void *ptr;
static void *pc;

static void test(void)
{
#ifdef __aarch64__
    void *t;
    asm("adr %0,1f; str %0,%1; 1: dc zva,%2"
        : "=&r"(t), "=m"(pc) : "r"(ptr));
#elif defined(__powerpc64__)
    void *t;
    asm("bl 0f; 0: mflr %0; addi %0,%0,1f-0b; std %0,%1; 1: dcbz 0,%2"
        : "=&r"(t), "=m"(pc) : "r"(ptr) : "lr");
#elif defined(__s390x__)
    void *t;
    asm("larl %0,1f; stg %0,%1; 1: xc 0(256,%2),0(%2)"
        : "=&r"(t), "=m"(pc) : "r"(ptr));
#else
    *(int *)ptr = 0;
#endif
}

static void *host_signal_pc(ucontext_t *uc)
{
#ifdef __aarch64__
    return (void *)uc->uc_mcontext.pc;
#elif defined(__powerpc64__)
    return (void *)uc->uc_mcontext.gp_regs[PT_NIP];
#elif defined(__s390x__)
    return (void *)uc->uc_mcontext.psw.addr;
#else
    return NULL;
#endif
}

static void sigsegv(int sig, siginfo_t *info, void *uc)
{
    assert(info->si_addr == ptr);
    assert(host_signal_pc(uc) == pc);
    exit(0);
}

int main(void)
{
    static const struct sigaction sa = {
        .sa_flags = SA_SIGINFO,
        .sa_sigaction = sigsegv
    };
    size_t size;
    int r;

    size = getpagesize();
    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
               MAP_ANON | MAP_PRIVATE, -1, 0);
    assert(ptr != MAP_FAILED);

    test();

    r = sigaction(SIGSEGV, &sa, NULL);
    assert(r == 0);
    r = mprotect(ptr, size, PROT_NONE);
    assert(r == 0);

    test();
    abort();
}
