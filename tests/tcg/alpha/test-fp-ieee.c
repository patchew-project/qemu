/*
 * Test IEEE T-floating (double) arithmetic exception behavior.
 */
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>

#define FPCR_INED       (1UL << 62)
#define FPCR_UNFD       (1UL << 61)
#define FPCR_DYN_NORMAL (2UL << 58)
#define FPCR_IOV        (1UL << 57)
#define FPCR_INE        (1UL << 56)
#define FPCR_UNF        (1UL << 55)
#define FPCR_OVF        (1UL << 54)
#define FPCR_DZE        (1UL << 53)
#define FPCR_INV        (1UL << 52)
#define FPCR_OVFD       (1UL << 51)
#define FPCR_DZED       (1UL << 50)
#define FPCR_INVD       (1UL << 49)
#define FPCR_STATUS_MASK (FPCR_IOV | FPCR_INE | FPCR_UNF \
                          | FPCR_OVF | FPCR_DZE | FPCR_INV)

static long run_addt_su(long *exc_out, double a, double b)
{
    unsigned long reset = FPCR_INED | FPCR_UNFD | FPCR_OVFD | FPCR_DZED
                          | FPCR_INVD | FPCR_DYN_NORMAL;
    double r;
    long e;

    asm ("excb\n\t"
         "mt_fpcr %4\n\t"
         "excb\n\t"
         "addt/su %2,%3,%0\n\t"
         "excb\n\t"
         "mf_fpcr %1\n\t"
         "excb\n\t"
         : "=f"(r), "=f"(e)
         : "f"(a), "f"(b), "f"(reset));

    *exc_out = e & FPCR_STATUS_MASK;
    long ri;
    __builtin_memcpy(&ri, &r, 8);
    return ri;
}

static long run_mult_su(long *exc_out, double a, double b)
{
    unsigned long reset = FPCR_INED | FPCR_UNFD | FPCR_OVFD | FPCR_DZED
                          | FPCR_INVD | FPCR_DYN_NORMAL;
    double r;
    long e;

    asm ("excb\n\t"
         "mt_fpcr %4\n\t"
         "excb\n\t"
         "mult/su %2,%3,%0\n\t"
         "excb\n\t"
         "mf_fpcr %1\n\t"
         "excb\n\t"
         : "=f"(r), "=f"(e)
         : "f"(a), "f"(b), "f"(reset));

    *exc_out = e & FPCR_STATUS_MASK;
    long ri;
    __builtin_memcpy(&ri, &r, 8);
    return ri;
}

static sigjmp_buf jmpbuf;
static volatile int got_sigfpe;

static void sigfpe_handler(int sig)
{
    got_sigfpe = 1;
    siglongjmp(jmpbuf, 1);
}

static int test_bare_inf_add_finite(void)
{
    double a = __builtin_inf();
    double b = 1.0;
    double r = 0.0;

    got_sigfpe = 0;
    signal(SIGFPE, sigfpe_handler);
    if (sigsetjmp(jmpbuf, 1) == 0) {
        asm volatile ("addt %1,%2,%0" : "=f"(r) : "f"(a), "f"(b));
    }
    signal(SIGFPE, SIG_DFL);

    if (got_sigfpe) {
        printf("FAIL bare addt Inf+1.0: spurious SIGFPE\n");
        return 1;
    }
    unsigned long ri;
    __builtin_memcpy(&ri, &r, 8);
    if (ri != 0x7ff0000000000000ul) {
        printf("FAIL bare addt Inf+1.0: result %016lx (expected 7ff0000000000000)\n", ri);
        return 1;
    }
    return 0;
}

static int test_su_inf_add_finite(void)
{
    double a = __builtin_inf();
    double b = 1.0;
    double r = 0.0;

    got_sigfpe = 0;
    signal(SIGFPE, sigfpe_handler);
    if (sigsetjmp(jmpbuf, 1) == 0) {
        asm volatile ("addt/su %1,%2,%0" : "=f"(r) : "f"(a), "f"(b));
    }
    signal(SIGFPE, SIG_DFL);

    if (got_sigfpe) {
        printf("FAIL addt/su Inf+1.0: spurious SIGFPE\n");
        return 1;
    }
    unsigned long ri;
    __builtin_memcpy(&ri, &r, 8);
    if (ri != 0x7ff0000000000000ul) {
        printf("FAIL addt/su Inf+1.0: result %016lx (expected 7ff0000000000000)\n", ri);
        return 1;
    }
    return 0;
}

int main(void)
{
    static const struct {
        double a, b;
        long r;           /* expected result bit pattern; 0 when result is NaN */
        long e;           /* expected FPCR exception bits */
        long (*func)(long *, double, double);
    } T[] = {
        /* addt/su: Inf + finite = Inf, no exception */
        { __builtin_inf(),  1.0,             0x7ff0000000000000l, 0,        run_addt_su },
        /* addt/su: Inf + Inf = Inf, no exception */
        { __builtin_inf(),  __builtin_inf(), 0x7ff0000000000000l, 0,        run_addt_su },
        /* addt/su: Inf + (-Inf) = NaN, invalid operation */
        { __builtin_inf(), -__builtin_inf(), 0,                   FPCR_INV, run_addt_su },
        /* mult/su: Inf * finite = Inf, no exception */
        { __builtin_inf(),  2.0,             0x7ff0000000000000l, 0,        run_mult_su },
        /* mult/su: 0 * Inf = NaN, invalid operation */
        { 0.0,              __builtin_inf(), 0,                   FPCR_INV, run_mult_su },
    };

    int i, err = 0;

    err |= test_bare_inf_add_finite();
    err |= test_su_inf_add_finite();

    for (i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        long e, r = T[i].func(&e, T[i].a, T[i].b);

        /* NaN payload is implementation-defined; skip result check. */
        int result_ok = (T[i].e & FPCR_INV) ? 1 : (r == T[i].r);
        if (!result_ok || e != T[i].e) {
            printf("Fail %a %a: expect (%016lx : %04lx) got (%016lx : %04lx)\n",
                   T[i].a, T[i].b,
                   T[i].r, T[i].e >> 48,
                   r, e >> 48);
            err = 1;
        }
    }

    if (!err) {
        puts("OK");
    }
    return err;
}
