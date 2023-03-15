/* Check EXECUTE with relative long instructions as targets. */
#include <stdlib.h>
#include <stdio.h>

struct test {
    const char *name;
    long (*func)(long reg, long *cc);
    long exp_reg;
    long exp_mem;
    long exp_cc;
};

/*
 * Each test sets the MEM_IDXth element of the mem array to MEM and uses a
 * single relative long instruction on it. The other elements remain zero.
 * This is in order to prevent stumbling upon MEM in random memory in case
 * there is an off-by-a-small-value bug.
 *
 * Note that while gcc supports the ZL constraint for relative long operands,
 * clang doesn't, so the assembly code accesses mem[MEM_IDX] using MEM_ASM.
 */
long mem[0x1000];
#define MEM_IDX 0x800
#define MEM_ASM "mem+0x800*8"

/* Initial %r2 value. */
#define REG 0x1234567887654321

/* Initial mem[MEM_IDX] value. */
#define MEM 0xfedcba9889abcdef

/* Initial cc value. */
#define CC 0

/* Relative long instructions and their expected effects. */
#define FOR_EACH_INSN(F)                                                       \
    F(cgfrl,  REG,                 MEM,                2)                      \
    F(cghrl,  REG,                 MEM,                2)                      \
    F(cgrl,   REG,                 MEM,                2)                      \
    F(chrl,   REG,                 MEM,                1)                      \
    F(clgfrl, REG,                 MEM,                2)                      \
    F(clghrl, REG,                 MEM,                2)                      \
    F(clgrl,  REG,                 MEM,                1)                      \
    F(clhrl,  REG,                 MEM,                2)                      \
    F(clrl,   REG,                 MEM,                1)                      \
    F(crl,    REG,                 MEM,                1)                      \
    F(larl,   (long)&mem[MEM_IDX], MEM,                CC)                     \
    F(lgfrl,  0xfffffffffedcba98,  MEM,                CC)                     \
    F(lghrl,  0xfffffffffffffedc,  MEM,                CC)                     \
    F(lgrl,   MEM,                 MEM,                CC)                     \
    F(lhrl,   0x12345678fffffedc,  MEM,                CC)                     \
    F(llghrl, 0x000000000000fedc,  MEM,                CC)                     \
    F(llhrl,  0x123456780000fedc,  MEM,                CC)                     \
    F(lrl,    0x12345678fedcba98,  MEM,                CC)                     \
    F(stgrl,  REG,                 REG,                CC)                     \
    F(sthrl,  REG,                 0x4321ba9889abcdef, CC)                     \
    F(strl,   REG,                 0x8765432189abcdef, CC)

/* Test functions. */
#define DEFINE_EX_TEST(insn, exp_reg, exp_mem, exp_cc)                         \
    static long test_ex_ ## insn(long reg, long *cc)                           \
    {                                                                          \
        register long reg_val asm("r2");                                       \
        long cc_val, mask, target;                                             \
                                                                               \
        reg_val = reg;                                                         \
        asm("xgr %[cc_val],%[cc_val]\n"  /* initial cc */                      \
            "lghi %[mask],0x20\n"        /* make target use %r2 */             \
            "larl %[target],0f\n"                                              \
            "ex %[mask],0(%[target])\n"                                        \
            "jg 1f\n"                                                          \
            "0: " #insn " %%r0," MEM_ASM "\n"                                  \
            "1: ipm %[cc_val]\n"                                               \
            : [cc_val] "=&r" (cc_val)                                          \
            , [mask] "=&r" (mask)                                              \
            , [target] "=&r" (target)                                          \
            , [reg_val] "+&r" (reg_val)                                        \
            : : "cc", "memory");                                               \
        reg = reg_val;                                                         \
        *cc = (cc_val >> 28) & 3;                                              \
                                                                               \
        return reg_val;                                                        \
    }

#define DEFINE_EXRL_TEST(insn, exp_reg, exp_mem, exp_cc)                       \
    static long test_exrl_ ## insn(long reg, long *cc)                         \
    {                                                                          \
        register long reg_val asm("r2");                                       \
        long cc_val, mask;                                                     \
                                                                               \
        reg_val = reg;                                                         \
        asm("xgr %[cc_val],%[cc_val]\n"  /* initial cc */                      \
            "lghi %[mask],0x20\n"        /* make target use %r2 */             \
            "exrl %[mask],0f\n"                                                \
            "jg 1f\n"                                                          \
            "0: " #insn " %%r0," MEM_ASM "\n"                                  \
            "1: ipm %[cc_val]\n"                                               \
            : [cc_val] "=&r" (cc_val)                                          \
            , [mask] "=&r" (mask)                                              \
            , [reg_val] "+&r" (reg_val)                                        \
            : : "cc", "memory");                                               \
        reg = reg_val;                                                         \
        *cc = (cc_val >> 28) & 3;                                              \
                                                                               \
        return reg_val;                                                        \
    }

FOR_EACH_INSN(DEFINE_EX_TEST)
FOR_EACH_INSN(DEFINE_EXRL_TEST)

/* Test definitions. */
#define REGISTER_EX_EXRL_TEST(ex_insn, insn, _exp_reg, _exp_mem, _exp_cc)      \
    {                                                                          \
        .name = #ex_insn " " #insn,                                            \
        .func = test_ ## ex_insn ## _ ## insn,                                 \
        .exp_reg = (long)(_exp_reg),                                           \
        .exp_mem = (long)(_exp_mem),                                           \
        .exp_cc = (long)(_exp_cc),                                             \
    },

#define REGISTER_EX_TEST(insn, exp_reg, exp_mem, exp_cc)                       \
    REGISTER_EX_EXRL_TEST(ex, insn, exp_reg, exp_mem, exp_cc)

#define REGISTER_EXRL_TEST(insn, exp_reg, exp_mem, exp_cc)                     \
    REGISTER_EX_EXRL_TEST(exrl, insn, exp_reg, exp_mem, exp_cc)

static const struct test tests[] = {
    FOR_EACH_INSN(REGISTER_EX_TEST)
    FOR_EACH_INSN(REGISTER_EXRL_TEST)
};

/* Loop over all tests and run them. */
int main(void)
{
    const struct test *test;
    int ret = EXIT_SUCCESS;
    long reg, cc;
    size_t i;

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        test = &tests[i];
        mem[MEM_IDX] = MEM;
        cc = -1;
        reg = test->func(REG, &cc);
#define ASSERT_EQ(expected, actual) do {                                       \
    if (expected != actual) {                                                  \
        fprintf(stderr, "%s: " #expected " (0x%lx) != " #actual " (0x%lx)\n",  \
                test->name, expected, actual);                                 \
        ret = EXIT_FAILURE;                                                    \
    }                                                                          \
} while (0)
        ASSERT_EQ(test->exp_reg, reg);
        ASSERT_EQ(test->exp_mem, mem[MEM_IDX]);
        ASSERT_EQ(test->exp_cc, cc);
#undef ASSERT_EQ
    }

    return ret;
}
