/*
 * fp-test.c - Floating point test suite.
 *
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#ifndef HW_POISON_H
#error Must define HW_POISON_H to work around TARGET_* poisoning
#endif

#include "qemu/osdep.h"
#include "fpu/softfloat.h"

#include <fenv.h>
#include <math.h>

enum error {
    ERROR_NONE,
    ERROR_NOT_HANDLED,
    ERROR_WHITELISTED,
    ERROR_COMMENT,
    ERROR_INPUT,
    ERROR_RESULT,
    ERROR_EXCEPTIONS,
    ERROR_MAX,
};

enum input_fmt {
    INPUT_FMT_IBM,
};

struct input {
    const char * const name;
    enum error (*test_line)(const char *line);
};

enum precision {
    PREC_FLOAT,
    PREC_DOUBLE,
    PREC_QUAD,
    PREC_FLOAT_TO_DOUBLE,
};

struct op_desc {
    const char * const name;
    int n_operands;
};

enum op {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_MULADD,
    OP_MULADD_NEG_ADDEND,
    OP_MULADD_NEG_PRODUCT,
    OP_MULADD_NEG_RESULT,
    OP_DIV,
    OP_SQRT,
    OP_MINNUM,
    OP_MAXNUM,
    OP_MAXNUMMAG,
    OP_ABS,
    OP_IS_NAN,
    OP_IS_INF,
    OP_FLOAT_TO_DOUBLE,
};

static const struct op_desc ops[] = {
    [OP_ADD] =       { "+", 2 },
    [OP_SUB] =       { "-", 2 },
    [OP_MUL] =       { "*", 2 },
    [OP_MULADD] =    { "*+", 3 },
    [OP_MULADD_NEG_ADDEND] =  { "*+nc", 3 },
    [OP_MULADD_NEG_PRODUCT] = { "*+np", 3 },
    [OP_MULADD_NEG_RESULT] =  { "*+nr", 3 },
    [OP_DIV] =       { "/", 2 },
    [OP_SQRT] =      { "V", 1 },
    [OP_MINNUM] =    { "<C", 2 },
    [OP_MAXNUM] =    { ">C", 2 },
    [OP_MAXNUMMAG] = { ">A", 2 },
    [OP_ABS] =       { "A", 1 },
    [OP_IS_NAN] =    { "?N", 1 },
    [OP_IS_INF] =    { "?i", 1 },
    [OP_FLOAT_TO_DOUBLE] = { "cff", 1 },
};

/*
 * We could enumerate all the types here. But really we only care about
 * QNaN and SNaN since only those can vary across ISAs.
 */
enum op_type {
    OP_TYPE_NUMBER,
    OP_TYPE_QNAN,
    OP_TYPE_SNAN,
};

struct operand {
    uint64_t val;
    enum op_type type;
};

struct test_op {
    struct operand operands[3];
    struct operand expected_result;
    enum precision prec;
    enum op op;
    signed char round;
    uint8_t trapped_exceptions;
    uint8_t exceptions;
    bool expected_result_is_valid;
};

typedef enum error (*tester_func_t)(struct test_op *);

struct tester {
    tester_func_t func;
    const char *name;
};

struct whitelist {
    char **lines;
    size_t n;
    GHashTable *ht;
};

static uint64_t test_stats[ERROR_MAX];
static struct whitelist whitelist;
static uint8_t default_exceptions;
static bool die_on_error = true;
static struct float_status soft_status = {
    .float_detect_tininess = float_tininess_before_rounding,
};

static inline float u64_to_float(uint64_t v)
{
    uint32_t v32 = v;
    uint32_t *v32p = &v32;

    return *(float *)v32p;
}

static inline double u64_to_double(uint64_t v)
{
    uint64_t *vp = &v;

    return *(double *)vp;
}

static inline uint64_t float_to_u64(float f)
{
    float *fp = &f;

    return *(uint32_t *)fp;
}

static inline uint64_t double_to_u64(double d)
{
    double *dp = &d;

    return *(uint64_t *)dp;
}

static inline bool is_err(enum error err)
{
    return err != ERROR_NONE &&
        err != ERROR_NOT_HANDLED &&
        err != ERROR_WHITELISTED &&
        err != ERROR_COMMENT;
}

static int host_exceptions_translate(int host_flags)
{
    int flags = 0;

    if (host_flags & FE_INEXACT) {
        flags |= float_flag_inexact;
    }
    if (host_flags & FE_UNDERFLOW) {
        flags |= float_flag_underflow;
    }
    if (host_flags & FE_OVERFLOW) {
        flags |= float_flag_overflow;
    }
    if (host_flags & FE_DIVBYZERO) {
        flags |= float_flag_divbyzero;
    }
    if (host_flags & FE_INVALID) {
        flags |= float_flag_invalid;
    }
    return flags;
}

static inline uint8_t host_get_exceptions(void)
{
    return host_exceptions_translate(fetestexcept(FE_ALL_EXCEPT));
}

static void host_set_exceptions(uint8_t flags)
{
    int host_flags = 0;

    if (flags & float_flag_inexact) {
        host_flags |= FE_INEXACT;
    }
    if (flags & float_flag_underflow) {
        host_flags |= FE_UNDERFLOW;
    }
    if (flags & float_flag_overflow) {
        host_flags |= FE_OVERFLOW;
    }
    if (flags & float_flag_divbyzero) {
        host_flags |= FE_DIVBYZERO;
    }
    if (flags & float_flag_invalid) {
        host_flags |= FE_INVALID;
    }
    feraiseexcept(host_flags);
}

#define STANDARD_EXCEPTIONS \
    (float_flag_inexact | float_flag_underflow | \
     float_flag_overflow | float_flag_divbyzero | float_flag_invalid)
#define FMT_EXCEPTIONS "%s%s%s%s%s%s"
#define PR_EXCEPTIONS(x)                                \
        ((x) & STANDARD_EXCEPTIONS ? "" : "none"),      \
        (((x) & float_flag_inexact)   ? "x" : ""),      \
        (((x) & float_flag_underflow) ? "u" : ""),      \
        (((x) & float_flag_overflow)  ? "o" : ""),      \
        (((x) & float_flag_divbyzero) ? "z" : ""),      \
        (((x) & float_flag_invalid)   ? "i" : "")

static enum error tester_check(const struct test_op *t, uint64_t res64,
                               bool res_is_nan, uint8_t flags)
{
    enum error err = ERROR_NONE;

    if (t->expected_result_is_valid) {
        if (t->expected_result.type == OP_TYPE_QNAN ||
            t->expected_result.type == OP_TYPE_SNAN) {
            if (!res_is_nan) {
                err = ERROR_RESULT;
                goto out;
            }
        } else if (res64 != t->expected_result.val) {
            err = ERROR_RESULT;
            goto out;
        }
    }
    if (t->exceptions && flags != (t->exceptions | default_exceptions)) {
        err = ERROR_EXCEPTIONS;
        goto out;
    }

 out:
    if (is_err(err)) {
        int i;

        fprintf(stderr, "%s ", ops[t->op].name);
        for (i = 0; i < ops[t->op].n_operands; i++) {
            fprintf(stderr, "0x%" PRIx64 "%s", t->operands[i].val,
                    i < ops[t->op].n_operands - 1 ? " " : "");
        }
        fprintf(stderr, ", expected: 0x%" PRIx64 ", returned: 0x%" PRIx64,
                t->expected_result.val, res64);
        if (err == ERROR_EXCEPTIONS) {
            fprintf(stderr, ", expected exceptions: " FMT_EXCEPTIONS
                    ", returned: " FMT_EXCEPTIONS,
                    PR_EXCEPTIONS(t->exceptions), PR_EXCEPTIONS(flags));
        }
        fprintf(stderr, "\n");
    }
    return err;
}

static enum error host_tester(struct test_op *t)
{
    uint64_t res64;
    bool result_is_nan;
    uint8_t flags = 0;

    feclearexcept(FE_ALL_EXCEPT);
    if (default_exceptions) {
        host_set_exceptions(default_exceptions);
    }

    if (t->prec == PREC_FLOAT) {
        float a, b, c;
        float *in[] = { &a, &b, &c };
        float res;
        int i;

        g_assert(ops[t->op].n_operands <= ARRAY_SIZE(in));
        for (i = 0; i < ops[t->op].n_operands; i++) {
            /* use the host's QNaN/SNaN patterns */
            if (t->operands[i].type == OP_TYPE_QNAN) {
                *in[i] = __builtin_nanf("");
            } else if (t->operands[i].type == OP_TYPE_SNAN) {
                *in[i] = __builtin_nansf("");
            } else {
                *in[i] = u64_to_float(t->operands[i].val);
            }
        }

        if (t->expected_result.type == OP_TYPE_QNAN) {
            t->expected_result.val = float_to_u64(__builtin_nanf(""));
        } else if (t->expected_result.type == OP_TYPE_SNAN) {
            t->expected_result.val = float_to_u64(__builtin_nansf(""));
        }

        switch (t->op) {
        case OP_ADD:
            res = a + b;
            break;
        case OP_SUB:
            res = a - b;
            break;
        case OP_MUL:
            res = a * b;
            break;
        case OP_MULADD:
            res = fmaf(a, b, c);
            break;
        case OP_DIV:
            res = a / b;
            break;
        case OP_SQRT:
            res = sqrtf(a);
            break;
        case OP_ABS:
            res = fabsf(a);
            break;
        case OP_IS_NAN:
            res = !!isnan(a);
            break;
        case OP_IS_INF:
            res = !!isinf(a);
            break;
        default:
            return ERROR_NOT_HANDLED;
        }
        flags = host_get_exceptions();
        res64 = float_to_u64(res);
        result_is_nan = isnan(res);
    } else if (t->prec == PREC_DOUBLE) {
        double a, b, c;
        double *in[] = { &a, &b, &c };
        double res;
        int i;

        g_assert(ops[t->op].n_operands <= ARRAY_SIZE(in));
        for (i = 0; i < ops[t->op].n_operands; i++) {
            /* use the host's QNaN/SNaN patterns */
            if (t->operands[i].type == OP_TYPE_QNAN) {
                *in[i] = __builtin_nan("");
            } else if (t->operands[i].type == OP_TYPE_SNAN) {
                *in[i] = __builtin_nans("");
            } else {
                *in[i] = u64_to_double(t->operands[i].val);
            }
        }

        if (t->expected_result.type == OP_TYPE_QNAN) {
            t->expected_result.val = double_to_u64(__builtin_nan(""));
        } else if (t->expected_result.type == OP_TYPE_SNAN) {
            t->expected_result.val = double_to_u64(__builtin_nans(""));
        }

        switch (t->op) {
        case OP_ADD:
            res = a + b;
            break;
        case OP_SUB:
            res = a - b;
            break;
        case OP_MUL:
            res = a * b;
            break;
        case OP_MULADD:
            res = fma(a, b, c);
            break;
        case OP_DIV:
            res = a / b;
            break;
        case OP_SQRT:
            res = sqrt(a);
            break;
        case OP_ABS:
            res = fabs(a);
            break;
        case OP_IS_NAN:
            res = !!isnan(a);
            break;
        case OP_IS_INF:
            res = !!isinf(a);
            break;
        default:
            return ERROR_NOT_HANDLED;
        }
        flags = host_get_exceptions();
        res64 = double_to_u64(res);
        result_is_nan = isnan(res);
    } else if (t->prec == PREC_FLOAT_TO_DOUBLE) {
        float a;
        double res;

        if (t->operands[0].type == OP_TYPE_QNAN) {
            a = __builtin_nanf("");
        } else if (t->operands[0].type == OP_TYPE_SNAN) {
            a = __builtin_nansf("");
        } else {
            a = u64_to_float(t->operands[0].val);
        }

        if (t->expected_result.type == OP_TYPE_QNAN) {
            t->expected_result.val = double_to_u64(__builtin_nan(""));
        } else if (t->expected_result.type == OP_TYPE_SNAN) {
            t->expected_result.val = double_to_u64(__builtin_nans(""));
        }

        switch (t->op) {
        case OP_FLOAT_TO_DOUBLE:
            res = a;
            break;
        default:
            return ERROR_NOT_HANDLED;
        }
        flags = host_get_exceptions();
        res64 = double_to_u64(res);
        result_is_nan = isnan(res);
    } else {
        return ERROR_NOT_HANDLED; /* XXX */
    }
    return tester_check(t, res64, result_is_nan, flags);
}

static enum error soft_tester(struct test_op *t)
{
    float_status *s = &soft_status;
    uint64_t res64;
    enum error err = ERROR_NONE;
    bool result_is_nan;

    s->float_rounding_mode = t->round;
    s->float_exception_flags = default_exceptions;

    if (t->prec == PREC_FLOAT) {
        float32 a, b, c;
        float32 *in[] = { &a, &b, &c };
        float32 res;
        int i;

        g_assert(ops[t->op].n_operands <= ARRAY_SIZE(in));
        for (i = 0; i < ops[t->op].n_operands; i++) {
            *in[i] = t->operands[i].val;
        }

        switch (t->op) {
        case OP_ADD:
            res = float32_add(a, b, s);
            break;
        case OP_SUB:
            res = float32_sub(a, b, s);
            break;
        case OP_MUL:
            res = float32_mul(a, b, s);
            break;
        case OP_MULADD:
            res = float32_muladd(a, b, c, 0, s);
            break;
        case OP_MULADD_NEG_ADDEND:
            res = float32_muladd(a, b, c, float_muladd_negate_c, s);
            break;
        case OP_MULADD_NEG_PRODUCT:
            res = float32_muladd(a, b, c, float_muladd_negate_product, s);
            break;
        case OP_MULADD_NEG_RESULT:
            res = float32_muladd(a, b, c, float_muladd_negate_result, s);
            break;
        case OP_DIV:
            res = float32_div(a, b, s);
            break;
        case OP_SQRT:
            res = float32_sqrt(a, s);
            break;
        case OP_MINNUM:
            res = float32_minnum(a, b, s);
            break;
        case OP_MAXNUM:
            res = float32_maxnum(a, b, s);
            break;
        case OP_MAXNUMMAG:
            res = float32_maxnummag(a, b, s);
            break;
        case OP_IS_NAN:
        {
            float f = !!float32_is_any_nan(a);

            res = float_to_u64(f);
            break;
        }
        case OP_IS_INF:
        {
            float f = !!float32_is_infinity(a);

            res = float_to_u64(f);
            break;
        }
        case OP_ABS:
            /* Fall-through: float32_abs does not handle NaN's */
        default:
            return ERROR_NOT_HANDLED;
        }
        res64 = res;
        result_is_nan = isnan(*(float *)&res);
    } else if (t->prec == PREC_DOUBLE) {
        float64 a, b, c;
        float64 *in[] = { &a, &b, &c };
        int i;

        g_assert(ops[t->op].n_operands <= ARRAY_SIZE(in));
        for (i = 0; i < ops[t->op].n_operands; i++) {
            *in[i] = t->operands[i].val;
        }

        switch (t->op) {
        case OP_ADD:
            res64 = float64_add(a, b, s);
            break;
        case OP_SUB:
            res64 = float64_sub(a, b, s);
            break;
        case OP_MUL:
            res64 = float64_mul(a, b, s);
            break;
        case OP_MULADD:
            res64 = float64_muladd(a, b, c, 0, s);
            break;
        case OP_MULADD_NEG_ADDEND:
            res64 = float64_muladd(a, b, c, float_muladd_negate_c, s);
            break;
        case OP_MULADD_NEG_PRODUCT:
            res64 = float64_muladd(a, b, c, float_muladd_negate_product, s);
            break;
        case OP_MULADD_NEG_RESULT:
            res64 = float64_muladd(a, b, c, float_muladd_negate_result, s);
            break;
        case OP_DIV:
            res64 = float64_div(a, b, s);
            break;
        case OP_SQRT:
            res64 = float64_sqrt(a, s);
            break;
        case OP_MINNUM:
            res64 = float64_minnum(a, b, s);
            break;
        case OP_MAXNUM:
            res64 = float64_maxnum(a, b, s);
            break;
        case OP_MAXNUMMAG:
            res64 = float64_maxnummag(a, b, s);
            break;
        case OP_IS_NAN:
        {
            double d = !!float64_is_any_nan(a);

            res64 = double_to_u64(d);
            break;
        }
        case OP_IS_INF:
        {
            double d = !!float64_is_infinity(a);

            res64 = double_to_u64(d);
            break;
        }
        case OP_ABS:
            /* Fall-through: float64_abs does not handle NaN's */
        default:
            return ERROR_NOT_HANDLED;
        }
        result_is_nan = isnan(*(double *)&res64);
    } else if (t->prec == PREC_FLOAT_TO_DOUBLE) {
        float32 a = t->operands[0].val;

        switch (t->op) {
        case OP_FLOAT_TO_DOUBLE:
            res64 = float32_to_float64(a, s);
            break;
        default:
            return ERROR_NOT_HANDLED;
        }
        result_is_nan = isnan(*(double *)&res64);
    } else {
        return ERROR_NOT_HANDLED; /* XXX */
    }
    return tester_check(t, res64, result_is_nan, s->float_exception_flags);
    return err;
}

static const struct tester valid_testers[] = {
    [0] = {
        .name = "soft",
        .func = soft_tester,
    },
    [1] = {
        .name = "host",
        .func = host_tester,
    },
};
static const struct tester *tester = &valid_testers[0];

static int ibm_get_exceptions(const char *p, uint8_t *excp)
{
    while (*p) {
        switch (*p) {
        case 'x':
            *excp |= float_flag_inexact;
            break;
        case 'u':
            *excp |= float_flag_underflow;
            break;
        case 'o':
            *excp |= float_flag_overflow;
            break;
        case 'z':
            *excp |= float_flag_divbyzero;
            break;
        case 'i':
            *excp |= float_flag_invalid;
            break;
        default:
            return 1;
        }
        p++;
    }
    return 0;
}

static uint64_t fp_choose(enum precision prec, uint64_t f, uint64_t d)
{
    switch (prec) {
    case PREC_FLOAT:
        return f;
    case PREC_DOUBLE:
        return d;
    default:
        g_assert_not_reached();
    }
}

static int
ibm_fp_hex(const char *p, enum precision prec, struct operand *ret)
{
    int len;

    ret->type = OP_TYPE_NUMBER;

    /* QNaN */
    if (unlikely(!strcmp("Q", p))) {
        ret->val = fp_choose(prec, 0xffc00000, 0xfff8000000000000);
        ret->type = OP_TYPE_QNAN;
        return 0;
    }
    /* SNaN */
    if (unlikely(!strcmp("S", p))) {
        ret->val = fp_choose(prec, 0xffb00000, 0xfff7000000000000);
        ret->type = OP_TYPE_SNAN;
        return 0;
    }
    if (unlikely(!strcmp("+Zero", p))) {
        ret->val = fp_choose(prec, 0x00000000, 0x0000000000000000);
        return 0;
    }
    if (unlikely(!strcmp("-Zero", p))) {
        ret->val = fp_choose(prec, 0x80000000, 0x8000000000000000);
        return 0;
    }
    if (unlikely(!strcmp("+inf", p) || !strcmp("+Inf", p))) {
        ret->val = fp_choose(prec, 0x7f800000, 0x7ff0000000000000);
        return 0;
    }
    if (unlikely(!strcmp("-inf", p) || !strcmp("-Inf", p))) {
        ret->val = fp_choose(prec, 0xff800000, 0xfff0000000000000);
        return 0;
    }

    len = strlen(p);

    if (strchr(p, 'P')) {
        bool negative = p[0] == '-';
        char *pos;
        bool denormal;

        if (len <= 4) {
            return 1;
        }
        denormal = p[1] == '0';
        if (prec == PREC_FLOAT) {
            uint32_t exponent;
            uint32_t significand;
            uint32_t h;

            significand = strtoul(&p[3], &pos, 16);
            if (*pos != 'P') {
                return 1;
            }
            pos++;
            exponent = strtol(pos, &pos, 10) + 127;
            if (pos != p + len) {
                return 1;
            }
            /*
             * When there's a leading zero, we have a denormal number. We'd
             * expect the input (unbiased) exponent to be -127, but for some
             * reason -126 is used. Correct that here.
             */
            if (denormal) {
                if (exponent != 1) {
                    return 1;
                }
                exponent = 0;
            }
            h = negative ? (1 << 31) : 0;
            h |= exponent << 23;
            h |= significand;
            ret->val = h;
            return 0;
        } else if (prec == PREC_DOUBLE) {
            uint64_t exponent;
            uint64_t significand;
            uint64_t h;

            significand = strtoul(&p[3], &pos, 16);
            if (*pos != 'P') {
                return 1;
            }
            pos++;
            exponent = strtol(pos, &pos, 10) + 1023;
            if (pos != p + len) {
                return 1;
            }
            if (denormal) {
                return 1; /* XXX */
            }
            h = negative ? (1ULL << 63) : 0;
            h |= exponent << 52;
            h |= significand;
            ret->val = h;
            return 0;
        } else { /* XXX */
            return 1;
        }
    } else if (strchr(p, 'e')) {
        char *pos;

        if (prec == PREC_FLOAT) {
            float f = strtof(p, &pos);

            if (*pos) {
                return 1;
            }
            ret->val = float_to_u64(f);
            return 0;
        }
        if (prec == PREC_DOUBLE) {
            double d = strtod(p, &pos);

            if (*pos) {
                return 1;
            }
            ret->val = double_to_u64(d);
            return 0;
        }
        return 0;
    } else if (!strcmp(p, "0x0")) {
        if (prec == PREC_FLOAT) {
            ret->val = float_to_u64(0.0);
        } else if (prec == PREC_DOUBLE) {
            ret->val = double_to_u64(0.0);
        } else {
            g_assert_not_reached();
        }
        return 0;
    } else if (!strcmp(p, "0x1")) {
        if (prec == PREC_FLOAT) {
            ret->val = float_to_u64(1.0);
        } else if (prec == PREC_DOUBLE) {
            ret->val = double_to_u64(1.0);
        } else {
            g_assert_not_reached();
        }
        return 0;
    }
    return 1;
}

static int find_op(const char *name, enum op *op)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ops); i++) {
        if (strcmp(ops[i].name, name) == 0) {
            *op = i;
            return 0;
        }
    }
    return 1;
}

/* Syntax of IBM FP test cases:
 * https://www.research.ibm.com/haifa/projects/verification/fpgen/syntax.txt
 */
static enum error ibm_test_line(const char *line)
{
    struct test_op t;
    /* at most nine fields; this should be more than enough for each field */
    char s[9][64];
    char *p;
    int n, field;
    int i;

    /* data lines start with either b32 or d(64|128) */
    if (unlikely(line[0] != 'b' && line[0] != 'd')) {
        return ERROR_COMMENT;
    }
    n = sscanf(line, "%63s %63s %63s %63s %63s %63s %63s %63s %63s",
               s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8]);
    if (unlikely(n < 5 || n > 9)) {
        return ERROR_INPUT;
    }

    field = 0;
    p = s[field];
    if (unlikely(strlen(p) < 4)) {
        return ERROR_INPUT;
    }
    if (strcmp("b32b64cff", p) == 0) {
        t.prec = PREC_FLOAT_TO_DOUBLE;
        if (find_op(&p[6], &t.op)) {
            return ERROR_NOT_HANDLED;
        }
    } else {
        if (strncmp("b32", p, 3) == 0) {
            t.prec = PREC_FLOAT;
        } else if (strncmp("d64", p, 3) == 0) {
            t.prec = PREC_DOUBLE;
        } else if (strncmp("d128", p, 4) == 0) {
            return ERROR_NOT_HANDLED; /* XXX */
        } else {
            return ERROR_INPUT;
        }
        if (find_op(&p[3], &t.op)) {
            return ERROR_NOT_HANDLED;
        }
    }

    field = 1;
    p = s[field];
    if (!strncmp("=0", p, 2)) {
        t.round = float_round_nearest_even;
    } else {
        return ERROR_NOT_HANDLED; /* XXX */
    }

    /* The trapped exceptions field is optional */
    t.trapped_exceptions = 0;
    field = 2;
    p = s[field];
    if (ibm_get_exceptions(p, &t.trapped_exceptions)) {
        if (unlikely(n == 9)) {
            return ERROR_INPUT;
        }
    } else {
        field++;
    }

    for (i = 0; i < ops[t.op].n_operands; i++) {
        enum precision prec = t.prec == PREC_FLOAT_TO_DOUBLE ?
            PREC_FLOAT : t.prec;

        p = s[field++];
        if (ibm_fp_hex(p, prec, &t.operands[i])) {
            return ERROR_INPUT;
        }
    }

    p = s[field++];
    if (strcmp("->", p)) {
        return ERROR_INPUT;
    }

    p = s[field++];
    if (unlikely(strcmp("#", p) == 0)) {
        t.expected_result_is_valid = false;
    } else {
        enum precision prec = t.prec == PREC_FLOAT_TO_DOUBLE ?
            PREC_DOUBLE : t.prec;

        if (ibm_fp_hex(p, prec, &t.expected_result)) {
            return ERROR_INPUT;
        }
        t.expected_result_is_valid = true;
    }

    /*
     * A 0 here means "do not check the exceptions", i.e. it does NOT mean
     * "there should be no exceptions raised".
     */
    t.exceptions = 0;
    /* the expected exceptions field is optional */
    if (field == n - 1) {
        p = s[field++];
        if (ibm_get_exceptions(p, &t.exceptions)) {
            return ERROR_INPUT;
        }
    }

    /*
     * We ignore "trapped exceptions" because we're not testing the trapping
     * mechanism of the host CPU.
     * We test though that the exception bits are correctly set.
     */
    if (t.trapped_exceptions) {
        return ERROR_NOT_HANDLED;
    }
    return tester->func(&t);
}

static const struct input valid_input_types[] = {
    [INPUT_FMT_IBM] = {
        .name = "ibm",
        .test_line = ibm_test_line,
    },
};

static const struct input *input_type = &valid_input_types[INPUT_FMT_IBM];

static bool line_is_whitelisted(const char *line)
{
    if (whitelist.ht == NULL) {
        return false;
    }
    return !!g_hash_table_lookup(whitelist.ht, line);
}

static void test_file(const char *filename)
{
    static char line[256];
    unsigned int i;
    FILE *fp;

    fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "cannot open file '%s': %s\n",
                filename, strerror(errno));
        exit(EXIT_FAILURE);
    }
    i = 0;
    while (fgets(line, sizeof(line), fp)) {
        enum error err;

        i++;
        if (unlikely(line_is_whitelisted(line))) {
            test_stats[ERROR_WHITELISTED]++;
            continue;
        }
        err = input_type->test_line(line);
        if (unlikely(is_err(err))) {
            switch (err) {
            case ERROR_INPUT:
                fprintf(stderr, "error: malformed input @ %s:%d:\n",
                        filename, i);
                break;
            case ERROR_RESULT:
                fprintf(stderr, "error: result mismatch for input @ %s:%d:\n",
                        filename, i);
                break;
            case ERROR_EXCEPTIONS:
                fprintf(stderr, "error: flags mismatch for input @ %s:%d:\n",
                        filename, i);
                break;
            default:
                g_assert_not_reached();
            }
            fprintf(stderr, "%s", line);
            if (die_on_error) {
                exit(EXIT_FAILURE);
            }
        }
        test_stats[err]++;
    }
    if (fclose(fp)) {
        fprintf(stderr, "warning: cannot close file '%s': %s\n",
                filename, strerror(errno));
    }
}

static void set_input_fmt(const char *optarg)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(valid_input_types); i++) {
        const struct input *type = &valid_input_types[i];

        if (strcmp(optarg, type->name) == 0) {
            input_type = type;
            return;
        }
    }
    fprintf(stderr, "Unknown input format '%s'", optarg);
    exit(EXIT_FAILURE);
}

static void set_tester(const char *optarg)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(valid_testers); i++) {
        const struct tester *t = &valid_testers[i];

        if (strcmp(optarg, t->name) == 0) {
            tester = t;
            return;
        }
    }
    fprintf(stderr, "Unknown tester '%s'", optarg);
    exit(EXIT_FAILURE);
}

static void whitelist_add_line(const char *orig_line)
{
    char *line;
    bool inserted;

    if (whitelist.ht == NULL) {
        whitelist.ht = g_hash_table_new(g_str_hash, g_str_equal);
    }
    line = g_hash_table_lookup(whitelist.ht, orig_line);
    if (unlikely(line != NULL)) {
        return;
    }
    whitelist.n++;
    whitelist.lines = g_realloc_n(whitelist.lines, whitelist.n, sizeof(line));
    line = strdup(orig_line);
    whitelist.lines[whitelist.n - 1] = line;
    /* if we pass key == val GLib will not reserve space for the value */
    inserted = g_hash_table_insert(whitelist.ht, line, line);
    g_assert(inserted);
}

static void set_whitelist(const char *filename)
{
    FILE *fp;
    static char line[256];

    fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "warning: cannot open white list file '%s': %s\n",
                filename, strerror(errno));
        return;
    }
    while (fgets(line, sizeof(line), fp)) {
        if (isspace(line[0]) || line[0] == '#') {
            continue;
        }
        whitelist_add_line(line);
    }
    if (fclose(fp)) {
        fprintf(stderr, "warning: cannot close file '%s': %s\n",
                filename, strerror(errno));
    }
}

static void set_default_exceptions(const char *str)
{
    if (ibm_get_exceptions(str, &default_exceptions)) {
        fprintf(stderr, "Invalid exception '%s'\n", str);
        exit(EXIT_FAILURE);
    }
}

static void usage_complete(int argc, char *argv[])
{
    fprintf(stderr, "Usage: %s [options] file1 [file2 ...]\n", argv[0]);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -a = Perform tininess detection after rounding "
            "(soft tester only). Default: before\n");
    fprintf(stderr, "  -n = do not die on error. Default: dies on error\n");
    fprintf(stderr, "  -e = default exception flags (xiozu). Default: none\n");
    fprintf(stderr, "  -f = format of the input file(s). Default: %s\n",
            valid_input_types[0].name);
    fprintf(stderr, "  -t = tester. Default: %s\n", valid_testers[0].name);
    fprintf(stderr, "  -w = path to file with test cases to be whitelisted\n");
    fprintf(stderr, "  -z = flush inputs to zero (soft tester only). "
            "Default: disabled\n");
    fprintf(stderr, "  -Z = flush output to zero (soft tester only). "
            "Default: disabled\n");
}

static void parse_opts(int argc, char *argv[])
{
    int c;

    for (;;) {
        c = getopt(argc, argv, "ae:f:hnt:w:zZ");
        if (c < 0) {
            return;
        }
        switch (c) {
        case 'a':
            soft_status.float_detect_tininess = float_tininess_after_rounding;
            break;
        case 'e':
            set_default_exceptions(optarg);
            break;
        case 'f':
            set_input_fmt(optarg);
            break;
        case 'h':
            usage_complete(argc, argv);
            exit(EXIT_SUCCESS);
        case 'n':
            die_on_error = false;
            break;
        case 't':
            set_tester(optarg);
            break;
        case 'w':
            set_whitelist(optarg);
            break;
        case 'z':
            soft_status.flush_inputs_to_zero = 1;
            break;
        case 'Z':
            soft_status.flush_to_zero = 1;
            break;
        }
    }
    g_assert_not_reached();
}

static uint64_t count_errors(void)
{
    uint64_t ret = 0;
    int i;

    for (i = ERROR_INPUT; i < ERROR_MAX; i++) {
        ret += test_stats[i];
    }
    return ret;
}

int main(int argc, char *argv[])
{
    uint64_t n_errors;
    int i;

    if (argc == 1) {
        usage_complete(argc, argv);
        exit(EXIT_FAILURE);
    }
    parse_opts(argc, argv);
    for (i = optind; i < argc; i++) {
        test_file(argv[i]);
    }

    n_errors = count_errors();
    if (n_errors) {
        printf("Tests failed: %"PRIu64". Parsing: %"PRIu64
               ", result:%"PRIu64", flags:%"PRIu64"\n",
               n_errors, test_stats[ERROR_INPUT], test_stats[ERROR_RESULT],
               test_stats[ERROR_EXCEPTIONS]);
    } else {
        printf("All tests OK.\n");
    }
    printf("Tests passed: %" PRIu64 ". Not handled: %" PRIu64
           ", whitelisted: %"PRIu64 "\n",
           test_stats[ERROR_NONE], test_stats[ERROR_NOT_HANDLED],
           test_stats[ERROR_WHITELISTED]);
    return !!n_errors;
}
