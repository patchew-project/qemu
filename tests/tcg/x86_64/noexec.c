#define _GNU_SOURCE

struct arch_noexec_test {
    void *expected_rip;
    unsigned long expected_rdi;
};

#include "../multiarch/noexec.h"

static void arch_check_mcontext(const struct arch_noexec_test *test,
                                const mcontext_t *ctx) {
    if (ctx->gregs[REG_RIP] != (unsigned long)test->expected_rip) {
        safe_puts("[  FAILED  ] wrong rip");
        _exit(1);
    }

    if (ctx->gregs[REG_RDI] != test->expected_rdi) {
        safe_puts("[  FAILED  ] wrong rdi");
        _exit(1);
    }
}

#define DEFINE_NX(name, offset) \
    void name ## _1(int); \
    void name ## _2(int); \
    extern const short name ## _end[]; \
    asm(/* Go to the specified page offset. */ \
        ".align 0x1000\n" \
        ".org .+" #offset "\n" \
        /* %rdi is 0 on entry, overwrite it with 1. */ \
        ".globl " #name "_1\n" \
        #name "_1:\n" \
        ".cfi_startproc\n" \
        "movq $1,%rdi\n" \
        /* Overwrite %rdi with 2. */ \
        ".globl " #name "_2\n" \
        #name "_2:\n" \
        "movq $2,%rdi\n" \
        "ret\n" \
        /* End of code. */ \
        ".globl " #name "_end\n" \
        #name "_end:\n" \
        ".cfi_endproc\n" \
        /* Go to the next page. */ \
        ".align 0x1000");

/* noexec_1 is executable, noexec_2 is non-executable. */
DEFINE_NX(noexec, 0xff9);

/*
 * noexec_cross_1 is executable, noexec_cross_2 crosses non-executable page
 * boundary.
 */
DEFINE_NX(noexec_cross, 0xff8);

/* noexec_full_1 and noexec_full_2 are non-executable. */
DEFINE_NX(noexec_full, 0x321);

int main(void)
{
    struct noexec_test noexec_tests[] = {
        {
            .name = "Fallthrough",
            .func = noexec_1,
            .page = noexec_2,
            .expected_si_addr = noexec_2,
            .arch = {
                .expected_rip = noexec_2,
                .expected_rdi = 1,
            },
        },
        {
            .name = "Jump",
            .func = noexec_2,
            .page = noexec_2,
            .expected_si_addr = noexec_2,
            .arch = {
                .expected_rip = noexec_2,
                .expected_rdi = 0,
            },
        },
        {
            .name = "Fallthrough [cross]",
            .func = noexec_cross_1,
            .page = PAGE_ALIGN(noexec_cross_end),
            .expected_si_addr = PAGE_ALIGN(noexec_cross_end),
            .arch = {
                .expected_rip = noexec_cross_2,
                .expected_rdi = 1,
            },
        },
        {
            .name = "Jump [cross]",
            .func = noexec_cross_2,
            .page = PAGE_ALIGN(noexec_cross_end),
            .expected_si_addr = PAGE_ALIGN(noexec_cross_end),
            .arch = {
                .expected_rip = noexec_cross_2,
                .expected_rdi = 0,
            },
        },
        {
            .name = "Jump [full]",
            .func = noexec_full_1,
            .page = PAGE_ALIGN(noexec_full_1),
            .expected_si_addr = noexec_full_1,
            .arch = {
                .expected_rip = noexec_full_1,
                .expected_rdi = 0,
            },
        },
    };

    return test_noexec(noexec_tests,
                       sizeof(noexec_tests) / sizeof(noexec_tests[0]));
}
