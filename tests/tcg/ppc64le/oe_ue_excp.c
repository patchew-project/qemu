#include <stdio.h>
#include <float.h>
#include <sys/prctl.h>

#define FP_OE (1ull << 6)
#define FP_UE (1ull << 5)

typedef union {
    double d;
    long long ll;
} ll_fp;

double asm_fmul (double a, double b)
{
    double t;
    asm (
        "lfd 0, %1\n\t"
        "lfd 1, %2\n\t"
        "fmul 2, 0, 1\n\t"
        "stfd 2, %0\n\t"
        :"=m"(t)
        :"m"(a),"m"(b)
        );
    return t;
}

double asm_fdiv (double a, double b)
{
    double t;
    asm (
        "lfd 0, %1\n\t"
        "lfd 1, %2\n\t"
        "fdiv 2, 0, 1\n\t"
        "stfd 2, %0\n\t"
        :"=m"(t)
        :"m"(a),"m"(b)
        );
    return t;
}

int main ()
{
    int i, ok = 1;
    ll_fp fpscr, t;

    prctl(PR_SET_FPEXC, PR_FP_EXC_DISABLED);

    fpscr.ll = FP_UE | FP_OE;
    __builtin_mtfsf (0b11111111, fpscr.d);
    fpscr.d = __builtin_mffs ();
    printf("fpscr = %016llx\n", fpscr.ll);

    ll_fp ch[] =
                {
                    { .ll = 0x1b64f1c1b0000000ull },
                    { .ll = 0x1b64f1c1b0000001ull },
                    { .ll = 0x1b90de3410000000ull },
                    { .ll = 0x1b90de3410000000ull },
                    { .ll = 0x5fcfffe4965a17e0ull },
                    { .ll = 0x5fcfffe4965a17e0ull },
                    { .ll = 0x2003ffffffffffffull },
                    { .ll = 0x2003ffffffffffffull }
                };

    ll_fp a[] =
                {
                    { .ll = 0x00005ca8ull },
                    { .ll = 0x0000badcull },
                    { .ll = 0x7fdfffe816d77b00ull },
                    { .d  = DBL_MAX }
                };

    ll_fp b[] =
                {
                    { .ll = 0x00001cefull },
                    { .ll = 0x00005c70ull },
                    { .ll = 0x7fdfffFC7F7FFF00ull },
                    { .d  = 2.5 }
                };

    for (i = 0; i < 4; i++) {
        t.d = asm_fmul(a[i].d, b[i].d);
        if (t.ll != ch[2 * i].ll) {
            ok = 0;
            printf ("Mismatch on fmul n %d:\n\tresult:   %016llx\n\t"
                    "expected: %016llx\n", i, t.ll, ch[2 * i].ll);
        } else {
            printf ("Ok on fmul n %d\n", i);
        }
        t.d = asm_fdiv(a[i].d, 1.0/b[i].d);
        if (t.ll != ch[2 * i + 1].ll) {
            ok = 0;
            printf ("Mismatch on fdiv n %d:\n\tresult:   %016llx\n\t"
                    "expected: %016llx\n", i, t.ll, ch[2 * i + 1].ll);
        } else {
            printf ("Ok on fdiv n %d\n", i);
        }
    }
    fpscr.d = __builtin_mffs ();
    printf("fpscr = %016llx\n", fpscr.ll);
    if(!ok) {
        return -1;
    }
    return 0;
}
