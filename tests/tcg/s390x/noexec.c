#define _GNU_SOURCE

struct arch_noexec_test {
    void *expected_pswa;
    unsigned long expected_r2;
};

#include "../multiarch/noexec.h"

static void arch_check_mcontext(const struct arch_noexec_test *test,
                                const mcontext_t *ctx) {
    if (ctx->psw.addr != (unsigned long)test->expected_pswa) {
        safe_puts("[  FAILED  ] wrong psw.addr");
        _exit(1);
    }

    if (ctx->gregs[2] != test->expected_r2) {
        safe_puts("[  FAILED  ] wrong r2");
        _exit(1);
    }
}

#define DEFINE_NX(name, offset) \
    void name ## _1(int); \
    void name ## _2(int); \
    void name ## _exrl(int); \
    extern const short name ## _end[]; \
    asm(/* Go to the specified page offset. */ \
        ".align 0x1000\n" \
        ".org .+" #offset "\n" \
        /* %r2 is 0 on entry, overwrite it with 1. */ \
        ".globl " #name "_1\n" \
        #name "_1:\n" \
        ".cfi_startproc\n" \
        "lgfi %r2,1\n" \
        /* Overwrite %2 with 2. */ \
        ".globl " #name "_2\n" \
        #name "_2:\n" \
        "lgfi %r2,2\n" \
        "br %r14\n" \
        /* End of code. */ \
        ".globl " #name "_end\n" \
        #name "_end:\n" \
        ".cfi_endproc\n" \
        /* Go to the next page. */ \
        ".align 0x1000\n" \
        /* Break alignment. */ \
        "nopr %r7\n" \
        ".globl " #name "_exrl\n" \
        #name "_exrl:\n" \
        ".cfi_startproc\n" \
        "exrl %r0," #name "_2\n" \
        "br %r14\n" \
        ".cfi_endproc");

/* noexec_1 is executable, noexec_2 is non-executable. */
DEFINE_NX(noexec, 0xffa);

/*
 * noexec_cross_1 is executable, noexec_cross_2 crosses non-executable page
 * boundary.
 */
DEFINE_NX(noexec_cross, 0xff8);

/* noexec_full_1 and noexec_full_2 are non-executable. */
DEFINE_NX(noexec_full, 0x322);

int main(void)
{
    struct noexec_test noexec_tests[] = {
        {
            .name = "Fallthrough",
            .func = noexec_1,
            .page = noexec_2,
            .expected_si_addr = noexec_2,
            .arch = {
                .expected_pswa = noexec_2,
                .expected_r2 = 1,
            },
        },
        {
            .name = "Jump",
            .func = noexec_2,
            .page = noexec_2,
            .expected_si_addr = noexec_2,
            .arch = {
                .expected_pswa = noexec_2,
                .expected_r2 = 0,
            },
        },
        {
            .name = "EXRL",
            .func = noexec_exrl,
            .page = noexec_2,
            .expected_si_addr = PAGE_ALIGN(noexec_end),
            .arch = {
                .expected_pswa = noexec_exrl,
                .expected_r2 = 0,
            },
        },
        {
            .name = "Fallthrough [cross]",
            .func = noexec_cross_1,
            .page = PAGE_ALIGN(noexec_cross_end),
            .expected_si_addr = PAGE_ALIGN(noexec_cross_end),
            .arch = {
                .expected_pswa = noexec_cross_2,
                .expected_r2 = 1,
            },
        },
        {
            .name = "Jump [cross]",
            .func = noexec_cross_2,
            .page = PAGE_ALIGN(noexec_cross_end),
            .expected_si_addr = PAGE_ALIGN(noexec_cross_end),
            .arch = {
                .expected_pswa = noexec_cross_2,
                .expected_r2 = 0,
            },
        },
        {
            .name = "EXRL [cross]",
            .func = noexec_cross_exrl,
            .page = PAGE_ALIGN(noexec_cross_end),
            .expected_si_addr = PAGE_ALIGN(noexec_cross_end),
            .arch = {
                .expected_pswa = noexec_cross_exrl,
                .expected_r2 = 0,
            },
        },
        {
            .name = "Jump [full]",
            .func = noexec_full_1,
            .page = PAGE_ALIGN(noexec_full_1),
            .expected_si_addr = PAGE_ALIGN(noexec_full_1),
            .arch = {
                .expected_pswa = noexec_full_1,
                .expected_r2 = 0,
            },
        },
    };

    return test_noexec(noexec_tests,
                       sizeof(noexec_tests) / sizeof(noexec_tests[0]));
}
