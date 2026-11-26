/*
 * HPPA floating-point instruction and context switching tests.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define BOOT 0x1000
#define CODE 0x1100
#define FP_CODE 0x1300
#define ASSIST 0x1400
#define UNEXPECTED 0x1500
#define HALT 0x1600
#define DATA 0x1800
#define RESULT 0x1900
#define LOG 0x1a00

/* Enable interruption-state collection through IPSW before entering CODE. */
static const uint32_t bootstrap[] = {
    0x34010010, /* ldi 8,r1 */
    0x02c11840, /* mtctl r1,ipsw */
    0x02201840, /* mtctl r0,iiasq */
    0x02201840, /* mtctl r0,iiasq */
    0x34012200, /* ldi 0x1100,r1 */
    0x02411840, /* mtctl r1,iiaoq */
    0x34012208, /* ldi 0x1104,r1 */
    0x02411840, /* mtctl r1,iiaoq */
    0x00000c00, /* rfi */
};

/*
 * r20 counts assists. After the first call, the shared CCR writer is entered
 * with FP enabled. Alternating its input must reselect the next translation.
 */
static const uint32_t fp_guest[] = {
    0x34011000, /* ldi 0x800,r1 */
    0x01c11840, /* mtctl r1,iva */
    0x08000240, /* nop */
    0x34103000, /* ldi 0x1800,r16 */
    0x34113200, /* ldi 0x1900,r17 */
    0x341a3400, /* ldi 0x1a00,r26 */
    0x34140000, /* ldi 0,r20 */
    0x34010180, /* ldi 0xc0,r1 */
    0x01411840, /* mtctl r1,ccr */
    0x2e00100c, /* fldd 0(r16),fr12 */
    0x34010000, /* ldi 0,r1 */
    0x01411840, /* mtctl r1,ccr */
    0x08142880, /* cmpclr,= r20,r0,r0: nullify the next instruction */
    0x08000240, /* replaced by the FP instruction under test */
    0x6a340008, /* stw r20,4(r17) */
    0x34010180, /* ldi 0xc0,r1 */
    0xe8400370, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x6a340010, /* stw r20,8(r17) */
    0x34010000, /* ldi 0,r1 */
    0xe8400350, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x6a340018, /* stw r20,12(r17) */
    0x34010180, /* ldi 0xc0,r1 */
    0xe8400330, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x6a340020, /* stw r20,16(r17) */
    0x34010000, /* ldi 0,r1 */
    0xe8400310, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x6a340028, /* stw r20,20(r17) */
    0x34010180, /* ldi 0xc0,r1 */
    0xe84002f0, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x6a340030, /* stw r20,24(r17) */
    0x34010000, /* ldi 0,r1 */
    0xe84002d0, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x6a340038, /* stw r20,28(r17) */
    0x34010180, /* ldi 0xc0,r1 */
    0x01411840, /* mtctl r1,ccr */
    0x2e10120c, /* fstd fr12,8(r16) */
    0x36100010, /* ldo 8(r16),r16 */
    0x2e10120d, /* fstd fr13,8(r16) */
    0x34010002, /* ldi 1,r1 */
    0x6a210000, /* stw r1,0(r17): completion */
    0xe8000882, /* b,n 0x1600 */
};

static const uint32_t fp_site[] = {
    0x01411840, /* mtctl r1,ccr */
    0x08000240, /* replaced by the FP instruction under test */
    0xe840c002, /* bv,n 0(rp) */
};

/* r20 and r26 are outside the shadow register set restored by rfir. */
static const uint32_t assist_handler[] = {
    0x026008a1, /* mfctl iir,r1 */
    0x6b410000, /* stw r1,0(r26) */
    0x024008a1, /* mfctl iiaoq,r1 */
    0x6b410008, /* stw r1,4(r26) */
    0x028008a1, /* mfctl isr,r1 */
    0x6b410010, /* stw r1,8(r26) */
    0x02a008a1, /* mfctl ior,r1 */
    0x6b410018, /* stw r1,12(r26) */
    0x375a0020, /* ldo 16(r26),r26 */
    0x36940002, /* ldo 1(r20),r20 */
    0x34010180, /* ldi 0xc0,r1 */
    0x01411840, /* mtctl r1,ccr */
    0x00000ca0, /* rfir: retry the faulting instruction */
};

static const uint32_t unexpected_handler[] = {
    0x026008a1, /* mfctl iir,r1 */
    0x6a210008, /* stw r1,4(r17) */
    0x024008a1, /* mfctl iiaoq,r1 */
    0x6a210010, /* stw r1,8(r17) */
    0x028008a1, /* mfctl isr,r1 */
    0x6a210018, /* stw r1,12(r17) */
    0x02a008a1, /* mfctl ior,r1 */
    0x6a210020, /* stw r1,16(r17) */
    0x6a380000, /* stw r24,0(r17): 0x100 | exception number */
    0xe80001aa, /* b,n 0x1600 */
};

/* r18 owns the live FP state; r19 requests the next logical context. */
static const uint32_t lazy_guest[] = {
    0x34011000, /* ldi 0x800,r1 */
    0x01c11840, /* mtctl r1,iva */
    0x08000240, /* nop */
    0x34123000, /* ldi 0x1800,r18: owner A */
    0x34113200, /* ldi 0x1900,r17 */
    0x34140000, /* ldi 0,r20 */
    0x34010180, /* ldi 0xc0,r1 */
    0x01411840, /* mtctl r1,ccr */
    0x2e40100c, /* fldd 0(r18),fr12 */
    0x318c0e0c, /* fadd,d fr12,fr12,fr12: A = 2 */
    0x34173240, /* ldi 0x1920,r23 */
    0x34133020, /* ldi 0x1810,r19: request B */
    0xe8400390, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x318c0e0c, /* fadd,d fr12,fr12,fr12: B = 32 */
    0x36f70010, /* ldo 8(r23),r23 */
    0x34133000, /* ldi 0x1800,r19: request A */
    0xe8400368, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x318c0e0c, /* fadd,d fr12,fr12,fr12: A = 4 */
    0x36f70010, /* ldo 8(r23),r23 */
    0x34133020, /* ldi 0x1810,r19: request B */
    0xe8400340, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x318c0e0c, /* fadd,d fr12,fr12,fr12: B = 64 */
    0x36f70010, /* ldo 8(r23),r23 */
    0x34133000, /* ldi 0x1800,r19: request A */
    0xe8400318, /* bl 0x1300,rp */
    0x08000240, /* nop */
    0x6a340008, /* stw r20,4(r17) */
    0x34010002, /* ldi 1,r1 */
    0x6a210000, /* stw r1,0(r17) */
    0xe80008f2, /* b,n 0x1600 */
};

static const uint32_t lazy_site[] = {
    0x01401840, /* mtctl r0,ccr */
    0x2ee0120c, /* fstd fr12,0(r23) */
    0xe840c002, /* bv,n 0(rp) */
};

static const uint32_t lazy_handler[] = {
    0x34010180, /* ldi 0xc0,r1 */
    0x01411840, /* mtctl r1,ccr */
    0x2e40120c, /* fstd fr12,0(r18): save owner */
    0x2e60100c, /* fldd 0(r19),fr12: restore requested context */
    0x08130252, /* copy r19,r18 */
    0x36940002, /* ldo 1(r20),r20 */
    0x00000ca0, /* rfir */
};

static void write_words(QTestState *qts, uint32_t addr,
                        const uint32_t *words, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        qtest_writel(qts, addr + 4 * i, words[i]);
    }
}

static uint32_t branch(uint32_t pc, uint32_t target)
{
    uint32_t d = (target - pc - 8) / 4;

    return 0xe8000002 | ((d & 0x10000) >> 16) | ((d & 0xf800) << 5) |
           ((d & 0x400) >> 8) | ((d & 0x3ff) << 3);
}

static QTestState *start_guest(bool retry_assist)
{
    QTestState *qts = qtest_init("-machine none -cpu pa-7300lc -m 1M "
                                "-S -accel tcg -no-shutdown "
                                "-device loader,addr=0x1000,cpu-num=0");

    write_words(qts, BOOT, bootstrap, G_N_ELEMENTS(bootstrap));
    /* Shutdown may re-enter this TB before the main loop stops the CPU. */
    qtest_writel(qts, HALT, 0xfffdead0);
    write_words(qts, UNEXPECTED, unexpected_handler,
                G_N_ELEMENTS(unexpected_handler));
    for (unsigned int i = 0; i < 32; i++) {
        uint32_t pc = 0x800 + i * 32;
        uint32_t target = retry_assist && i == 22 ? ASSIST : UNEXPECTED;

        qtest_writel(qts, pc, 0x34180000 | ((0x100 | i) << 1));
        qtest_writel(qts, pc + 4, branch(pc + 4, target));
    }
    return qts;
}

static void run_guest(QTestState *qts, uint32_t expected)
{
    uint32_t result;

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    qtest_qmp_eventwait(qts, "SHUTDOWN");
    result = qtest_readl(qts, RESULT);
    g_test_message("completion=0x%x, result[1]=0x%x, result[2]=0x%x",
                   result, qtest_readl(qts, RESULT + 4),
                   qtest_readl(qts, RESULT + 8));
    g_assert_cmphex(result, ==, expected);
}

typedef struct FPTest {
    const char *name;
    uint32_t instruction;
    bool memory;
    uint64_t fr13;
} FPTest;

static const FPTest fp_tests[] = {
    { "fldw", 0x2600100c, true },
    { "fstw", 0x2600120c, true },
    { "fldd", 0x2e00100c, true },
    { "fstd", 0x2e00120c, true },
    { "fid", 0x30000000 },
    { "fadd-double", 0x318c0e0d, false, 0x4010000000000000ULL },
    { "fadd-double-extended", 0x398c0e0d, false, 0x4010000000000000ULL },
    { "fmpyadd-single", 0x188541e6 }, /* fr20L,fr21L,fr22L,fr23L,fr24L */
    { "fmpysub-double", 0x988541c6 }, /* fr4,fr5,fr6,fr7,fr8 */
};

static void test_fp(const void *opaque)
{
    const FPTest *test = opaque;
    static const uint32_t counts[] = { 0, 0, 1, 1, 2, 2, 3 };
    QTestState *qts = start_guest(true);

    write_words(qts, CODE, fp_guest, G_N_ELEMENTS(fp_guest));
    write_words(qts, FP_CODE, fp_site, G_N_ELEMENTS(fp_site));
    write_words(qts, ASSIST, assist_handler, G_N_ELEMENTS(assist_handler));
    qtest_writel(qts, CODE + 13 * 4, test->instruction);
    qtest_writel(qts, FP_CODE + 4, test->instruction);
    qtest_writeq(qts, DATA, 0x4000000000000000ULL); /* double 2 */
    run_guest(qts, 1);
    for (size_t i = 0; i < G_N_ELEMENTS(counts); i++) {
        g_assert_cmpuint(qtest_readl(qts, RESULT + 4 + 4 * i), ==, counts[i]);
    }
    for (unsigned int i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(qts, LOG + 16 * i), ==, test->instruction);
        g_assert_cmphex(qtest_readl(qts, LOG + 16 * i + 4), ==, FP_CODE + 4);
        if (test->memory) {
            g_assert_cmphex(qtest_readl(qts, LOG + 16 * i + 8), ==, 0);
            g_assert_cmphex(qtest_readl(qts, LOG + 16 * i + 12), ==, DATA);
        }
    }
    g_assert_cmphex(qtest_readq(qts, DATA + 8), ==, 0x4000000000000000ULL);
    g_assert_cmphex(qtest_readq(qts, DATA + 16), ==, test->fr13);
    qtest_quit(qts);
}

static void test_lazy(void)
{
    static const uint64_t markers[] = {
        0x4030000000000000ULL, /* B = 16 */
        0x4000000000000000ULL, /* A = 2 */
        0x4040000000000000ULL, /* B = 32 */
        0x4010000000000000ULL, /* A = 4 */
    };
    QTestState *qts = start_guest(true);

    write_words(qts, CODE, lazy_guest, G_N_ELEMENTS(lazy_guest));
    write_words(qts, FP_CODE, lazy_site, G_N_ELEMENTS(lazy_site));
    write_words(qts, ASSIST, lazy_handler, G_N_ELEMENTS(lazy_handler));
    qtest_writeq(qts, DATA, 0x3ff0000000000000ULL); /* A = 1 */
    qtest_writeq(qts, DATA + 16, markers[0]);
    run_guest(qts, 1);
    g_assert_cmpuint(qtest_readl(qts, RESULT + 4), ==, 4);
    for (size_t i = 0; i < G_N_ELEMENTS(markers); i++) {
        g_assert_cmphex(qtest_readq(qts, RESULT + 32 + 8 * i), ==, markers[i]);
    }
    g_assert_cmphex(qtest_readq(qts, DATA), ==, 0x4010000000000000ULL);
    g_assert_cmphex(qtest_readq(qts, DATA + 16), ==, 0x4050000000000000ULL);
    qtest_quit(qts);
}

typedef struct FremTest {
    const char *name;
    uint32_t instruction;
    uint64_t dividend;
    uint64_t divisor;
    uint64_t result;
    uint32_t fpsr;
    uint32_t flags;
} FremTest;

static const FremTest frem_tests[] = {
    {
        .name = "single-tie",
        .instruction = 0x318d860e, /* frem,sgl fr12,fr13,fr14 */
        .dividend = 0x40e0000000000000ULL, /* 7.0f */
        .divisor = 0x4000000000000000ULL, /* 2.0f */
        .result = 0xbf80000089abcdefULL, /* -1.0f */
        .fpsr = 0x200, /* round toward zero */
    },
    {
        .name = "double-tie",
        .instruction = 0x318d8e0e, /* frem,dbl fr12,fr13,fr14 */
        .dividend = 0x4014000000000000ULL, /* 5.0 */
        .divisor = 0x4000000000000000ULL, /* 2.0 */
        .result = 0x3ff0000000000000ULL, /* 1.0 */
        .fpsr = 0x400, /* round upward */
    },
    {
        .name = "single-extended-subnormal",
        .instruction = 0x398d96ce, /* frem,sgl fr12R,fr13R,fr14R */
        .dividend = 0x0000000000000001ULL, /* smallest subnormal */
        .divisor = 0x000000003f800000ULL, /* 1.0f */
        .result = 0x0123456700000001ULL,
        .fpsr = 0x600, /* round downward */
    },
    {
        .name = "double-extended-negative-zero",
        .instruction = 0x398d8e0e, /* frem,dbl fr12,fr13,fr14 */
        .dividend = 0xc010000000000000ULL, /* -4.0 */
        .divisor = 0x4000000000000000ULL, /* 2.0 */
        .result = 0x8000000000000000ULL, /* -0.0 */
    },
    {
        .name = "single-zero-divisor",
        .instruction = 0x318d860e,
        .dividend = 0x3f80000000000000ULL, /* 1.0f */
        .divisor = 0,
        .result = 0x7fa0000089abcdefULL, /* quiet NaN */
        .flags = 0x80000000, /* invalid operation */
    },
    {
        .name = "double-infinite-dividend",
        .instruction = 0x398d8e0e,
        .dividend = 0x7ff0000000000000ULL, /* infinity */
        .divisor = 0x3ff0000000000000ULL, /* 1.0 */
        .result = 0x7ff4000000000000ULL, /* quiet NaN */
        .flags = 0x80000000, /* invalid operation */
    },
};

static void test_frem(const void *opaque)
{
    const FremTest *test = opaque;
    const uint32_t guest[] = {
        0x34011000, /* ldi 0x800,r1 */
        0x01c11840, /* mtctl r1,iva */
        0x34103000, /* ldi 0x1800,r16 */
        0x34113200, /* ldi 0x1900,r17 */
        0x34010180, /* ldi 0xc0,r1 */
        0x01411840, /* mtctl r1,ccr */
        0x2e00100c, /* fldd 0(r16),fr12 */
        0x2e10100d, /* fldd 8(r16),fr13 */
        0x36100020, /* ldo 16(r16),r16 */
        0x2e001000, /* fldd 0(r16),fr0 */
        0x2e10100e, /* fldd 8(r16),fr14 */
        test->instruction,
        0x2e10120e, /* fstd fr14,8(r16) */
        0x2e001200, /* fstd fr0,0(r16) */
        0x34010002, /* ldi 1,r1 */
        0x6a210000, /* stw r1,0(r17): completion */
    };
    QTestState *qts = start_guest(false);
    uint32_t end = CODE + sizeof(guest);

    write_words(qts, CODE, guest, G_N_ELEMENTS(guest));
    qtest_writel(qts, end, branch(end, HALT));
    qtest_writeq(qts, DATA, test->dividend);
    qtest_writeq(qts, DATA + 8, test->divisor);
    qtest_writeq(qts, DATA + 16, (uint64_t)test->fpsr << 32);
    /* A single-precision write must preserve the other half of FR14. */
    qtest_writeq(qts, DATA + 24, 0x0123456789abcdefULL);

    run_guest(qts, 1);
    g_assert_cmphex(qtest_readq(qts, DATA + 24), ==, test->result);
    g_assert_cmphex(qtest_readl(qts, DATA + 16), ==, test->fpsr | test->flags);
    qtest_quit(qts);
}

typedef struct FcnvTest {
    const char *name;
    uint32_t instruction;
    uint64_t operand;
    uint64_t result;
    uint64_t result_mask;
    uint32_t er2;
    bool result_first;
} FcnvTest;

static const FcnvTest fcnv_tests[] = {
    {
        .name = "status-first",
        .instruction = 0x3180220c, /* fcnvff,sgl,dbl fr12,fr12 */
        .operand = 0x7fa0000012345678ULL,
        .result = 0x7fa0000000000000ULL,
        .result_mask = 0xffffffff00000000ULL,
        .er2 = 0x2580220c,
    },
    {
        .name = "result-first",
        .instruction = 0x3180220c,
        .operand = 0x7fa0000012345678ULL,
        .result = 0x7fa0000000000000ULL,
        .result_mask = 0xffffffff00000000ULL,
        .er2 = 0x2580220c,
        .result_first = true,
    },
    {
        .name = "finite",
        .instruction = 0x3180220c,
        .operand = 0x4000000012345678ULL, /* 2.0f in the left half */
        .result = 0x4000000000000000ULL, /* 2.0 */
        .result_mask = UINT64_MAX,
    },
    {
        .name = "extended-status-first",
        .instruction = 0x39800a4c, /* fcnvff,dbl,sgl fr12,fr12R */
        .operand = 0x7ff4000001234567ULL,
        .result = 0x7ff4000001234567ULL,
        .result_mask = UINT64_MAX,
        .er2 = 0x2d800a4c,
    },
    {
        .name = "extended-finite",
        .instruction = 0x39800a4c,
        .operand = 0x4004000000000000ULL, /* 2.5 */
        .result = 0x4004000040200000ULL, /* 2.5f in the right half */
        .result_mask = UINT64_MAX,
    },
};

static const uint32_t fcnv_save[] = {
    0x2e101200, /* fstd fr0,8(r16) */
    0x36100020, /* ldo 16(r16),r16 */
    0x2e001201, /* fstd fr1,0(r16) */
    0x2e101200, /* fstd fr0,8(r16) */
    0x36100020, /* ldo 16(r16),r16 */
    0x2e00120c, /* fstd fr12,0(r16) */
};

static void test_fcnv(const void *opaque)
{
    const FcnvTest *test = opaque;
    const uint32_t guest[] = {
        0x34011000, /* ldi 0x800,r1 */
        0x01c11840, /* mtctl r1,iva */
        0x34103000, /* ldi 0x1800,r16 */
        0x34113200, /* ldi 0x1900,r17 */
        0x34123050, /* ldi 0x1828,r18 */
        0x34010180, /* ldi 0xc0,r1 */
        0x01411840, /* mtctl r1,ccr */
        0x2e00100c, /* fldd 0(r16),fr12 */
        test->instruction,
        0x08002880, /* cmpclr,= r0,r0,r0 */
        0x2e40120c, /* fstd fr12,0(r18): nullified */
    };
    static const uint32_t complete[] = {
        0x34010002, /* ldi 1,r1 */
        0x6a210000, /* stw r1,0(r17): completion */
    };
    QTestState *qts = start_guest(false);
    uint32_t pc = CODE + sizeof(guest);
    uint64_t fr0 = test->er2 ? 0x0000004000000000ULL : 0;

    write_words(qts, CODE, guest, G_N_ELEMENTS(guest));
    if (test->result_first) {
        uint32_t vector_branch = 0x800 + 14 * 32 + 4;
        uint32_t end = ASSIST + sizeof(fcnv_save);

        qtest_writel(qts, pc, 0x2e40120c); /* fstd fr12,0(r18) */
        pc += 4;
        qtest_writel(qts, vector_branch, branch(vector_branch, ASSIST));
        write_words(qts, ASSIST, fcnv_save, G_N_ELEMENTS(fcnv_save));
        qtest_writel(qts, end, branch(end, UNEXPECTED));
    }
    write_words(qts, pc, fcnv_save, G_N_ELEMENTS(fcnv_save));
    pc += sizeof(fcnv_save);
    write_words(qts, pc, complete, G_N_ELEMENTS(complete));
    pc += sizeof(complete);
    qtest_writel(qts, pc, branch(pc, HALT));
    qtest_writeq(qts, DATA, test->operand);

    run_guest(qts, test->result_first ? 0x10e : 1);
    if (test->result_first) {
        g_assert_cmphex(qtest_readl(qts, RESULT + 8), ==, CODE + sizeof(guest));
    }
    g_assert_cmphex(qtest_readq(qts, DATA + 8), ==, fr0);
    g_assert_cmphex(qtest_readl(qts, DATA + 16), ==, test->er2);
    g_assert_cmphex(qtest_readq(qts, DATA + 24), ==, 0);
    g_assert_cmphex(qtest_readq(qts, DATA + 32) & test->result_mask, ==,
                    test->result);
    g_assert_cmphex(qtest_readq(qts, DATA + 40), ==, 0);
    qtest_quit(qts);
}

typedef struct FmpyAssistTest {
    const char *name;
    uint32_t instruction;
    uint32_t er2;
    uint64_t fr22;
    uint64_t fr23;
} FmpyAssistTest;

static const FmpyAssistTest fmpy_assist_tests[] = {
    {
        .name = "scalar-tiny",
        .instruction = 0x33374617, /* fmpy,sgl fr25,fr23,fr23 */
        .er2 = 0x27374617,
        .fr22 = 0xb79bae5f8cad057cULL,
        .fr23 = 0x37d95384fd6b8987ULL,
    },
    {
        .name = "compound-trace",
        .instruction = 0x19263a26, /* fmpyadd,sgl fr25,fr22,fr22,fr24,fr23 */
        .er2 = 0x0d263a26,
        .fr22 = 0xb79bae5f8cad057cULL,
        .fr23 = 0x004a1020fd6b8987ULL,
    },
    {
        .name = "compound-tiny",
        .instruction = 0x19263a26,
        .er2 = 0x0d263a26,
        .fr22 = 0x380000008cad057cULL, /* Exact subnormal product. */
        .fr23 = 0x37d95384fd6b8987ULL,
    },
    {
        .name = "double-scalar-tiny",
        .instruction = 0x33374e17, /* fmpy,dbl fr25,fr23,fr23 */
        .er2 = 0x27374e17,
        .fr22 = 0xb79bae5f8cad057cULL,
        .fr23 = 0x3850000000000000ULL, /* 2^-122. */
    },
    {
        .name = "compound-subtract-tiny",
        .instruction = 0x99263a26, /* fmpysub,sgl fr25,fr22,fr22,fr24,fr23 */
        .er2 = 0x8d263a26,
        .fr22 = 0x380000008cad057cULL,
        .fr23 = 0x37d95384fd6b8987ULL,
    },
    {
        .name = "compound-denormal",
        .instruction = 0x19263a26,
        .er2 = 0x0d263a26,
        .fr22 = 0x3f8000008cad057cULL, /* Multiply by 1.0f. */
        .fr23 = 0x004a1020fd6b8987ULL,
    },
};

static void test_fmpy_assist(const void *opaque)
{
    const FmpyAssistTest *test = opaque;
    const uint32_t guest[] = {
        0x34011000, /* ldi 0x800,r1 */
        0x01c11840, /* mtctl r1,iva */
        0x34103000, /* ldi 0x1800,r16 */
        0x34113200, /* ldi 0x1900,r17 */
        0x34010180, /* ldi 0xc0,r1 */
        0x01411840, /* mtctl r1,ccr */
        0x2e001016, /* fldd 0(r16),fr22 */
        0x2e101017, /* fldd 8(r16),fr23 */
        0x36100020, /* ldo 16(r16),r16 */
        0x2e001018, /* fldd 0(r16),fr24 */
        0x2e101019, /* fldd 8(r16),fr25 */
        test->instruction,
        0x2e001200, /* fstd fr0,0(r16) */
        0x2e101201, /* fstd fr1,8(r16) */
        0x36100020, /* ldo 16(r16),r16 */
        0x2e001200, /* fstd fr0,0(r16) */
        0x2e101216, /* fstd fr22,8(r16) */
        0x36100020, /* ldo 16(r16),r16 */
        0x2e001217, /* fstd fr23,0(r16) */
        0x34010002, /* ldi 1,r1 */
        0x6a210000, /* stw r1,0(r17): completion */
    };
    QTestState *qts = start_guest(false);
    uint32_t end = CODE + sizeof(guest);

    write_words(qts, CODE, guest, G_N_ELEMENTS(guest));
    qtest_writel(qts, end, branch(end, HALT));
    qtest_writeq(qts, DATA, test->fr22);
    qtest_writeq(qts, DATA + 8, test->fr23);
    qtest_writeq(qts, DATA + 16, 0x3895c07a1110fe7aULL);
    qtest_writeq(qts, DATA + 24, 0x07ae7c429facdab0ULL);

    run_guest(qts, 1);
    g_assert_cmphex(qtest_readq(qts, DATA + 16), ==, 0x0000004000000000ULL);
    g_assert_cmphex(qtest_readl(qts, DATA + 24), ==, test->er2);
    g_assert_cmphex(qtest_readq(qts, DATA + 32), ==, 0);
    g_assert_cmphex(qtest_readq(qts, DATA + 40), ==, test->fr22);
    g_assert_cmphex(qtest_readq(qts, DATA + 48), ==, test->fr23);
    qtest_quit(qts);
}

static const uint32_t fault_guest[] = {
    0x34011000, /* ldi 0x800,r1 */
    0x01c11840, /* mtctl r1,iva */
    0x34103000, /* ldi 0x1800,r16: replaced for unaligned cases */
    0x34113200, /* ldi 0x1900,r17 */
    0x01401840, /* mtctl r0,ccr */
    0x08000240, /* replaced by ssm 2,r0 for data translation */
    0x08000240, /* replaced by the FP instruction under test */
    0x34010002, /* ldi 1,r1 */
    0x6a210000, /* stw r1,0(r17): unexpectedly completed */
    0xe80009aa, /* b,n 0x1600 */
};

typedef struct FaultTest {
    const char *name;
    uint32_t instruction;
    unsigned int exception;
    uint32_t address;
    bool dtlb;
    bool memory;
} FaultTest;

static const FaultTest fault_tests[] = {
    { "unaligned-load", 0x2e00100c, 28, DATA + 2, false, true },
    { "unaligned-store", 0x2e00120c, 28, DATA + 2, false, true },
    { "dtlb-load", 0x2e00100c, 15, DATA, true, true },
    { "dtlb-store", 0x2e00120c, 15, DATA, true, true },
    { "disabled-uid1-arithmetic", 0x30000040, 22, DATA, false, false },
    { "disabled-uid1-memory", 0x2e00104c, 22, DATA, false, true },
};

static void test_fault(const void *opaque)
{
    const FaultTest *test = opaque;
    uint32_t addr = test->address;
    QTestState *qts = start_guest(false);

    write_words(qts, CODE, fault_guest, G_N_ELEMENTS(fault_guest));
    qtest_writel(qts, CODE + 8, 0x34100000 | (addr << 1));
    if (test->dtlb) {
        qtest_writel(qts, CODE + 20, 0x00020d60); /* ssm 2,r0 */
    }
    qtest_writel(qts, CODE + 24, test->instruction);
    run_guest(qts, 0x100 | test->exception);
    g_assert_cmphex(qtest_readl(qts, RESULT + 4), ==, test->instruction);
    g_assert_cmphex(qtest_readl(qts, RESULT + 8), ==, CODE + 24);
    if (test->memory) {
        g_assert_cmphex(qtest_readl(qts, RESULT + 12), ==, 0);
        g_assert_cmphex(qtest_readl(qts, RESULT + 16), ==, addr);
    }
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!qtest_has_accel("tcg")) {
        g_test_skip("TCG accelerator required");
        return 0;
    }
    for (size_t i = 0; i < G_N_ELEMENTS(fp_tests); i++) {
        g_autofree char *name = g_strdup_printf("fpu/%s", fp_tests[i].name);

        qtest_add_data_func(name, &fp_tests[i], test_fp);
    }
    qtest_add_func("fpu/lazy-contexts", test_lazy);
    for (size_t i = 0; i < G_N_ELEMENTS(frem_tests); i++) {
        g_autofree char *name = g_strdup_printf("fpu/frem/%s", frem_tests[i].name);

        qtest_add_data_func(name, &frem_tests[i], test_frem);
    }
    for (size_t i = 0; i < G_N_ELEMENTS(fcnv_tests); i++) {
        g_autofree char *name = g_strdup_printf("fpu/fcnv/%s", fcnv_tests[i].name);

        qtest_add_data_func(name, &fcnv_tests[i], test_fcnv);
    }
    for (size_t i = 0; i < G_N_ELEMENTS(fmpy_assist_tests); i++) {
        g_autofree char *name = g_strdup_printf("fpu/fmpy-assist/%s",
                                                fmpy_assist_tests[i].name);

        qtest_add_data_func(name, &fmpy_assist_tests[i], test_fmpy_assist);
    }
    for (size_t i = 0; i < G_N_ELEMENTS(fault_tests); i++) {
        g_autofree char *name = g_strdup_printf("fpu/%s", fault_tests[i].name);

        qtest_add_data_func(name, &fault_tests[i], test_fault);
    }
    return g_test_run();
}
