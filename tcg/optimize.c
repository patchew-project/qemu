/*
 * Optimizations for Tiny Code Generator for QEMU
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/int128.h"
#include "tcg/tcg-op-common.h"
#include "tcg-internal.h"

#define CASE_OP_32_64(x)                        \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64)

#define CASE_OP_32_64_VEC(x)                    \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64):    \
        glue(glue(case INDEX_op_, x), _vec)

typedef struct TempOptInfo {
    bool is_const;
    TCGTemp *prev_copy;
    TCGTemp *next_copy;
    uint64_t val;
    uint64_t z_mask;  /* mask bit is 0 if and only if value bit is 0 */
    uint64_t s_mask;  /* a left-aligned mask of clrsb(value) bits. */
} TempOptInfo;

typedef struct OptContext {
    TCGContext *tcg;
    TCGOp *prev_mb;
    TCGTempSet temps_used;

    /* In flight values from optimization. */
    uint64_t a_mask;  /* mask bit is 0 iff value identical to first input */
    uint64_t z_mask;  /* mask bit is 0 iff value bit is 0 */
    uint64_t s_mask;  /* mask of clrsb(value) bits */
    TCGType type;
} OptContext;

/* Calculate the smask for a specific value. */
static uint64_t smask_from_value(uint64_t value)
{
    int rep = clrsb64(value);
    return ~(~0ull >> rep);
}

/*
 * Calculate the smask for a given set of known-zeros.
 * If there are lots of zeros on the left, we can consider the remainder
 * an unsigned field, and thus the corresponding signed field is one bit
 * larger.
 */
static uint64_t smask_from_zmask(uint64_t zmask)
{
    /*
     * Only the 0 bits are significant for zmask, thus the msb itself
     * must be zero, else we have no sign information.
     */
    int rep = clz64(zmask);
    if (rep == 0) {
        return 0;
    }
    rep -= 1;
    return ~(~0ull >> rep);
}

/*
 * Recreate a properly left-aligned smask after manipulation.
 * Some bit-shuffling, particularly shifts and rotates, may
 * retain sign bits on the left, but may scatter disconnected
 * sign bits on the right.  Retain only what remains to the left.
 */
static uint64_t smask_from_smask(int64_t smask)
{
    /* Only the 1 bits are significant for smask */
    return smask_from_zmask(~smask);
}

static inline TempOptInfo *ts_info(TCGTemp *ts)
{
    return ts->state_ptr;
}

static inline TempOptInfo *arg_info(TCGArg arg)
{
    return ts_info(arg_temp(arg));
}

static inline bool ts_is_const(TCGTemp *ts)
{
    return ts_info(ts)->is_const;
}

static inline bool ts_is_const_val(TCGTemp *ts, uint64_t val)
{
    TempOptInfo *ti = ts_info(ts);
    return ti->is_const && ti->val == val;
}

static inline bool arg_is_const(TCGArg arg)
{
    return ts_is_const(arg_temp(arg));
}

static inline bool arg_is_const_val(TCGArg arg, uint64_t val)
{
    return ts_is_const_val(arg_temp(arg), val);
}

static inline bool ts_is_copy(TCGTemp *ts)
{
    return ts_info(ts)->next_copy != ts;
}

/* Reset TEMP's state, possibly removing the temp for the list of copies.  */
static void reset_ts(TCGTemp *ts)
{
    TempOptInfo *ti = ts_info(ts);
    TempOptInfo *pi = ts_info(ti->prev_copy);
    TempOptInfo *ni = ts_info(ti->next_copy);

    ni->prev_copy = ti->prev_copy;
    pi->next_copy = ti->next_copy;
    ti->next_copy = ts;
    ti->prev_copy = ts;
    ti->is_const = false;
    ti->z_mask = -1;
    ti->s_mask = 0;
}

static void reset_temp(TCGArg arg)
{
    reset_ts(arg_temp(arg));
}

/* Initialize and activate a temporary.  */
static void init_ts_info(OptContext *ctx, TCGTemp *ts)
{
    size_t idx = temp_idx(ts);
    TempOptInfo *ti;

    if (test_bit(idx, ctx->temps_used.l)) {
        return;
    }
    set_bit(idx, ctx->temps_used.l);

    ti = ts->state_ptr;
    if (ti == NULL) {
        ti = tcg_malloc(sizeof(TempOptInfo));
        ts->state_ptr = ti;
    }

    ti->next_copy = ts;
    ti->prev_copy = ts;
    if (ts->kind == TEMP_CONST) {
        ti->is_const = true;
        ti->val = ts->val;
        ti->z_mask = ts->val;
        ti->s_mask = smask_from_value(ts->val);
    } else {
        ti->is_const = false;
        ti->z_mask = -1;
        ti->s_mask = 0;
    }
}

static TCGTemp *find_better_copy(TCGContext *s, TCGTemp *ts)
{
    TCGTemp *i, *g, *l;

    /* If this is already readonly, we can't do better. */
    if (temp_readonly(ts)) {
        return ts;
    }

    g = l = NULL;
    for (i = ts_info(ts)->next_copy; i != ts; i = ts_info(i)->next_copy) {
        if (temp_readonly(i)) {
            return i;
        } else if (i->kind > ts->kind) {
            if (i->kind == TEMP_GLOBAL) {
                g = i;
            } else if (i->kind == TEMP_TB) {
                l = i;
            }
        }
    }

    /* If we didn't find a better representation, return the same temp. */
    return g ? g : l ? l : ts;
}

static bool ts_are_copies(TCGTemp *ts1, TCGTemp *ts2)
{
    TCGTemp *i;

    if (ts1 == ts2) {
        return true;
    }

    if (!ts_is_copy(ts1) || !ts_is_copy(ts2)) {
        return false;
    }

    for (i = ts_info(ts1)->next_copy; i != ts1; i = ts_info(i)->next_copy) {
        if (i == ts2) {
            return true;
        }
    }

    return false;
}

static bool args_are_copies(TCGArg arg1, TCGArg arg2)
{
    return ts_are_copies(arg_temp(arg1), arg_temp(arg2));
}

static TCGArg arg_new_constant(OptContext *ctx, uint64_t val)
{
    TCGType type = ctx->type;
    TCGTemp *ts;

    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    ts = tcg_constant_internal(type, val);
    init_ts_info(ctx, ts);

    return temp_arg(ts);
}

static bool tcg_opt_gen_mov(OptContext *ctx, TCGOp *op, TCGArg dst, TCGArg src)
{
    TCGTemp *dst_ts = arg_temp(dst);
    TCGTemp *src_ts = arg_temp(src);
    TempOptInfo *di;
    TempOptInfo *si;
    TCGOpcode new_op;

    if (ts_are_copies(dst_ts, src_ts)) {
        tcg_op_remove(ctx->tcg, op);
        return true;
    }

    reset_ts(dst_ts);
    di = ts_info(dst_ts);
    si = ts_info(src_ts);

    switch (ctx->type) {
    case TCG_TYPE_I32:
        new_op = INDEX_op_mov_i32;
        break;
    case TCG_TYPE_I64:
        new_op = INDEX_op_mov_i64;
        break;
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        /* TCGOP_VECL and TCGOP_VECE remain unchanged.  */
        new_op = INDEX_op_mov_vec;
        break;
    default:
        g_assert_not_reached();
    }
    op->opc = new_op;
    op->args[0] = dst;
    op->args[1] = src;

    di->z_mask = si->z_mask;
    di->s_mask = si->s_mask;

    if (src_ts->type == dst_ts->type) {
        TempOptInfo *ni = ts_info(si->next_copy);

        di->next_copy = si->next_copy;
        di->prev_copy = src_ts;
        ni->prev_copy = dst_ts;
        si->next_copy = dst_ts;
        di->is_const = si->is_const;
        di->val = si->val;
    }
    return true;
}

static bool tcg_opt_gen_movi(OptContext *ctx, TCGOp *op,
                             TCGArg dst, uint64_t val)
{
    /* Convert movi to mov with constant temp. */
    return tcg_opt_gen_mov(ctx, op, dst, arg_new_constant(ctx, val));
}

static uint64_t do_constant_folding_2(TCGOpcode op, uint64_t x, uint64_t y)
{
    uint64_t l64, h64;

    switch (op) {
    CASE_OP_32_64(add):
        return x + y;

    CASE_OP_32_64(sub):
        return x - y;

    CASE_OP_32_64(mul):
        return x * y;

    CASE_OP_32_64_VEC(and):
        return x & y;

    CASE_OP_32_64_VEC(or):
        return x | y;

    CASE_OP_32_64_VEC(xor):
        return x ^ y;

    case INDEX_op_shl_i32:
        return (uint32_t)x << (y & 31);

    case INDEX_op_shl_i64:
        return (uint64_t)x << (y & 63);

    case INDEX_op_shr_i32:
        return (uint32_t)x >> (y & 31);

    case INDEX_op_shr_i64:
        return (uint64_t)x >> (y & 63);

    case INDEX_op_sar_i32:
        return (int32_t)x >> (y & 31);

    case INDEX_op_sar_i64:
        return (int64_t)x >> (y & 63);

    case INDEX_op_rotr_i32:
        return ror32(x, y & 31);

    case INDEX_op_rotr_i64:
        return ror64(x, y & 63);

    case INDEX_op_rotl_i32:
        return rol32(x, y & 31);

    case INDEX_op_rotl_i64:
        return rol64(x, y & 63);

    CASE_OP_32_64_VEC(not):
        return ~x;

    CASE_OP_32_64(neg):
        return -x;

    CASE_OP_32_64_VEC(andc):
        return x & ~y;

    CASE_OP_32_64_VEC(orc):
        return x | ~y;

    CASE_OP_32_64_VEC(eqv):
        return ~(x ^ y);

    CASE_OP_32_64_VEC(nand):
        return ~(x & y);

    CASE_OP_32_64_VEC(nor):
        return ~(x | y);

    case INDEX_op_clz_i32:
        return (uint32_t)x ? clz32(x) : y;

    case INDEX_op_clz_i64:
        return x ? clz64(x) : y;

    case INDEX_op_ctz_i32:
        return (uint32_t)x ? ctz32(x) : y;

    case INDEX_op_ctz_i64:
        return x ? ctz64(x) : y;

    case INDEX_op_ctpop_i32:
        return ctpop32(x);

    case INDEX_op_ctpop_i64:
        return ctpop64(x);

    CASE_OP_32_64(ext8s):
        return (int8_t)x;

    CASE_OP_32_64(ext16s):
        return (int16_t)x;

    CASE_OP_32_64(ext8u):
        return (uint8_t)x;

    CASE_OP_32_64(ext16u):
        return (uint16_t)x;

    CASE_OP_32_64(bswap16):
        x = bswap16(x);
        return y & TCG_BSWAP_OS ? (int16_t)x : x;

    CASE_OP_32_64(bswap32):
        x = bswap32(x);
        return y & TCG_BSWAP_OS ? (int32_t)x : x;

    case INDEX_op_bswap64_i64:
        return bswap64(x);

    case INDEX_op_ext_i32_i64:
    case INDEX_op_ext32s_i64:
        return (int32_t)x;

    case INDEX_op_extu_i32_i64:
    case INDEX_op_extrl_i64_i32:
    case INDEX_op_ext32u_i64:
        return (uint32_t)x;

    case INDEX_op_extrh_i64_i32:
        return (uint64_t)x >> 32;

    case INDEX_op_muluh_i32:
        return ((uint64_t)(uint32_t)x * (uint32_t)y) >> 32;
    case INDEX_op_mulsh_i32:
        return ((int64_t)(int32_t)x * (int32_t)y) >> 32;

    case INDEX_op_muluh_i64:
        mulu64(&l64, &h64, x, y);
        return h64;
    case INDEX_op_mulsh_i64:
        muls64(&l64, &h64, x, y);
        return h64;

    case INDEX_op_div_i32:
        /* Avoid crashing on divide by zero, otherwise undefined.  */
        return (int32_t)x / ((int32_t)y ? : 1);
    case INDEX_op_divu_i32:
        return (uint32_t)x / ((uint32_t)y ? : 1);
    case INDEX_op_div_i64:
        return (int64_t)x / ((int64_t)y ? : 1);
    case INDEX_op_divu_i64:
        return (uint64_t)x / ((uint64_t)y ? : 1);

    case INDEX_op_rem_i32:
        return (int32_t)x % ((int32_t)y ? : 1);
    case INDEX_op_remu_i32:
        return (uint32_t)x % ((uint32_t)y ? : 1);
    case INDEX_op_rem_i64:
        return (int64_t)x % ((int64_t)y ? : 1);
    case INDEX_op_remu_i64:
        return (uint64_t)x % ((uint64_t)y ? : 1);

    default:
        g_assert_not_reached();
    }
}

static uint64_t do_constant_folding(TCGOpcode op, TCGType type,
                                    uint64_t x, uint64_t y)
{
    uint64_t res = do_constant_folding_2(op, x, y);
    if (type == TCG_TYPE_I32) {
        res = (int32_t)res;
    }
    return res;
}

static bool do_constant_folding_cond_32(uint32_t x, uint32_t y, TCGCond c)
{
    switch (c) {
    case TCG_COND_EQ:
        return x == y;
    case TCG_COND_NE:
        return x != y;
    case TCG_COND_LT:
        return (int32_t)x < (int32_t)y;
    case TCG_COND_GE:
        return (int32_t)x >= (int32_t)y;
    case TCG_COND_LE:
        return (int32_t)x <= (int32_t)y;
    case TCG_COND_GT:
        return (int32_t)x > (int32_t)y;
    case TCG_COND_LTU:
        return x < y;
    case TCG_COND_GEU:
        return x >= y;
    case TCG_COND_LEU:
        return x <= y;
    case TCG_COND_GTU:
        return x > y;
    case TCG_COND_TSTEQ:
        return (x & y) == 0;
    case TCG_COND_TSTNE:
        return (x & y) != 0;
    case TCG_COND_ALWAYS:
    case TCG_COND_NEVER:
        break;
    }
    g_assert_not_reached();
}

static bool do_constant_folding_cond_64(uint64_t x, uint64_t y, TCGCond c)
{
    switch (c) {
    case TCG_COND_EQ:
        return x == y;
    case TCG_COND_NE:
        return x != y;
    case TCG_COND_LT:
        return (int64_t)x < (int64_t)y;
    case TCG_COND_GE:
        return (int64_t)x >= (int64_t)y;
    case TCG_COND_LE:
        return (int64_t)x <= (int64_t)y;
    case TCG_COND_GT:
        return (int64_t)x > (int64_t)y;
    case TCG_COND_LTU:
        return x < y;
    case TCG_COND_GEU:
        return x >= y;
    case TCG_COND_LEU:
        return x <= y;
    case TCG_COND_GTU:
        return x > y;
    case TCG_COND_TSTEQ:
        return (x & y) == 0;
    case TCG_COND_TSTNE:
        return (x & y) != 0;
    case TCG_COND_ALWAYS:
    case TCG_COND_NEVER:
        break;
    }
    g_assert_not_reached();
}

static int do_constant_folding_cond_eq(TCGCond c)
{
    switch (c) {
    case TCG_COND_GT:
    case TCG_COND_LTU:
    case TCG_COND_LT:
    case TCG_COND_GTU:
    case TCG_COND_NE:
        return 0;
    case TCG_COND_GE:
    case TCG_COND_GEU:
    case TCG_COND_LE:
    case TCG_COND_LEU:
    case TCG_COND_EQ:
        return 1;
    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        return -1;
    case TCG_COND_ALWAYS:
    case TCG_COND_NEVER:
        break;
    }
    g_assert_not_reached();
}

/*
 * Return -1 if the condition can't be simplified,
 * and the result of the condition (0 or 1) if it can.
 */
static int do_constant_folding_cond(TCGType type, TCGArg x,
                                    TCGArg y, TCGCond c)
{
    if (arg_is_const(x) && arg_is_const(y)) {
        uint64_t xv = arg_info(x)->val;
        uint64_t yv = arg_info(y)->val;

        switch (type) {
        case TCG_TYPE_I32:
            return do_constant_folding_cond_32(xv, yv, c);
        case TCG_TYPE_I64:
            return do_constant_folding_cond_64(xv, yv, c);
        default:
            /* Only scalar comparisons are optimizable */
            return -1;
        }
    } else if (args_are_copies(x, y)) {
        return do_constant_folding_cond_eq(c);
    } else if (arg_is_const_val(y, 0)) {
        switch (c) {
        case TCG_COND_LTU:
        case TCG_COND_TSTNE:
            return 0;
        case TCG_COND_GEU:
        case TCG_COND_TSTEQ:
            return 1;
        default:
            return -1;
        }
    }
    return -1;
}

/**
 * swap_commutative:
 * @dest: TCGArg of the destination argument, or NO_DEST.
 * @p1: first paired argument
 * @p2: second paired argument
 *
 * If *@p1 is a constant and *@p2 is not, swap.
 * If *@p2 matches @dest, swap.
 * Return true if a swap was performed.
 */

#define NO_DEST  temp_arg(NULL)

static bool swap_commutative(TCGArg dest, TCGArg *p1, TCGArg *p2)
{
    TCGArg a1 = *p1, a2 = *p2;
    int sum = 0;
    sum += arg_is_const(a1);
    sum -= arg_is_const(a2);

    /* Prefer the constant in second argument, and then the form
       op a, a, b, which is better handled on non-RISC hosts. */
    if (sum > 0 || (sum == 0 && dest == a2)) {
        *p1 = a2;
        *p2 = a1;
        return true;
    }
    return false;
}

static bool swap_commutative2(TCGArg *p1, TCGArg *p2)
{
    int sum = 0;
    sum += arg_is_const(p1[0]);
    sum += arg_is_const(p1[1]);
    sum -= arg_is_const(p2[0]);
    sum -= arg_is_const(p2[1]);
    if (sum > 0) {
        TCGArg t;
        t = p1[0], p1[0] = p2[0], p2[0] = t;
        t = p1[1], p1[1] = p2[1], p2[1] = t;
        return true;
    }
    return false;
}

/*
 * Return -1 if the condition can't be simplified,
 * and the result of the condition (0 or 1) if it can.
 */
static int do_constant_folding_cond1(OptContext *ctx, TCGArg dest,
                                     TCGArg *p1, TCGArg *p2, TCGArg *pcond)
{
    TCGCond cond;
    bool swap;
    int r;

    swap = swap_commutative(dest, p1, p2);
    cond = *pcond;
    if (swap) {
        *pcond = cond = tcg_swap_cond(cond);
    }

    r = do_constant_folding_cond(ctx->type, *p1, *p2, cond);
    if (r >= 0) {
        return r;
    }
    if (!is_tst_cond(cond)) {
        return -1;
    }

    /*
     * TSTNE x,x -> NE x,0
     * TSTNE x,-1 -> NE x,0
     */
    if (args_are_copies(*p1, *p2) || arg_is_const_val(*p2, -1)) {
        *p2 = arg_new_constant(ctx, 0);
        *pcond = tcg_tst_eqne_cond(cond);
        return -1;
    }

    /* TSTNE x,sign -> LT x,0 */
    if (arg_is_const_val(*p2, (ctx->type == TCG_TYPE_I32
                               ? INT32_MIN : INT64_MIN))) {
        *p2 = arg_new_constant(ctx, 0);
        *pcond = tcg_tst_ltge_cond(cond);
    }
    return -1;
}

static int do_constant_folding_cond2(OptContext *ctx, TCGArg *args)
{
    TCGArg al, ah, bl, bh;
    TCGCond c;
    bool swap;
    int r;

    swap = swap_commutative2(args, args + 2);
    c = args[4];
    if (swap) {
        args[4] = c = tcg_swap_cond(c);
    }

    al = args[0];
    ah = args[1];
    bl = args[2];
    bh = args[3];

    if (arg_is_const(bl) && arg_is_const(bh)) {
        tcg_target_ulong blv = arg_info(bl)->val;
        tcg_target_ulong bhv = arg_info(bh)->val;
        uint64_t b = deposit64(blv, 32, 32, bhv);

        if (arg_is_const(al) && arg_is_const(ah)) {
            tcg_target_ulong alv = arg_info(al)->val;
            tcg_target_ulong ahv = arg_info(ah)->val;
            uint64_t a = deposit64(alv, 32, 32, ahv);

            r = do_constant_folding_cond_64(a, b, c);
            if (r >= 0) {
                return r;
            }
        }

        if (b == 0) {
            switch (c) {
            case TCG_COND_LTU:
            case TCG_COND_TSTNE:
                return 0;
            case TCG_COND_GEU:
            case TCG_COND_TSTEQ:
                return 1;
            default:
                break;
            }
        }

        /* TSTNE x,-1 -> NE x,0 */
        if (b == -1 && is_tst_cond(c)) {
            args[3] = args[2] = arg_new_constant(ctx, 0);
            args[4] = tcg_tst_eqne_cond(c);
            return -1;
        }

        /* TSTNE x,sign -> LT x,0 */
        if (b == INT64_MIN && is_tst_cond(c)) {
            /* bl must be 0, so copy that to bh */
            args[3] = bl;
            args[4] = tcg_tst_ltge_cond(c);
            return -1;
        }
    }

    if (args_are_copies(al, bl) && args_are_copies(ah, bh)) {
        r = do_constant_folding_cond_eq(c);
        if (r >= 0) {
            return r;
        }

        /* TSTNE x,x -> NE x,0 */
        if (is_tst_cond(c)) {
            args[3] = args[2] = arg_new_constant(ctx, 0);
            args[4] = tcg_tst_eqne_cond(c);
            return -1;
        }
    }
    return -1;
}

static void init_arguments(OptContext *ctx, TCGOp *op, int nb_args)
{
    for (int i = 0; i < nb_args; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        init_ts_info(ctx, ts);
    }
}

static void copy_propagate(OptContext *ctx, TCGOp *op,
                           int nb_oargs, int nb_iargs)
{
    TCGContext *s = ctx->tcg;

    for (int i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        if (ts_is_copy(ts)) {
            op->args[i] = temp_arg(find_better_copy(s, ts));
        }
    }
}

static void finish_folding(OptContext *ctx, TCGOp *op)
{
    const TCGOpDef *def = &tcg_op_defs[op->opc];
    int i, nb_oargs;

    /*
     * We only optimize extended basic blocks.  If the opcode ends a BB
     * and is not a conditional branch, reset all temp data.
     */
    if (def->flags & TCG_OPF_BB_END) {
        ctx->prev_mb = NULL;
        if (!(def->flags & TCG_OPF_COND_BRANCH)) {
            memset(&ctx->temps_used, 0, sizeof(ctx->temps_used));
        }
        return;
    }

    nb_oargs = def->nb_oargs;
    for (i = 0; i < nb_oargs; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        reset_ts(ts);
        /*
         * Save the corresponding known-zero/sign bits mask for the
         * first output argument (only one supported so far).
         */
        if (i == 0) {
            ts_info(ts)->z_mask = ctx->z_mask;
            ts_info(ts)->s_mask = ctx->s_mask;
        }
    }
}

/*
 * The fold_* functions return true when processing is complete,
 * usually by folding the operation to a constant or to a copy,
 * and calling tcg_opt_gen_{mov,movi}.  They may do other things,
 * like collect information about the value produced, for use in
 * optimizing a subsequent operation.
 *
 * These first fold_* functions are all helpers, used by other
 * folders for more specific operations.
 */

static bool fold_const1(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1])) {
        uint64_t t;

        t = arg_info(op->args[1])->val;
        t = do_constant_folding(op->opc, ctx->type, t, 0);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }
    return false;
}

static bool fold_const2(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1]) && arg_is_const(op->args[2])) {
        uint64_t t1 = arg_info(op->args[1])->val;
        uint64_t t2 = arg_info(op->args[2])->val;

        t1 = do_constant_folding(op->opc, ctx->type, t1, t2);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t1);
    }
    return false;
}

static bool fold_commutative(OptContext *ctx, TCGOp *op)
{
    swap_commutative(op->args[0], &op->args[1], &op->args[2]);
    return false;
}

static bool fold_const2_commutative(OptContext *ctx, TCGOp *op)
{
    swap_commutative(op->args[0], &op->args[1], &op->args[2]);
    return fold_const2(ctx, op);
}

static bool fold_masks(OptContext *ctx, TCGOp *op)
{
    uint64_t a_mask = ctx->a_mask;
    uint64_t z_mask = ctx->z_mask;
    uint64_t s_mask = ctx->s_mask;

    /*
     * 32-bit ops generate 32-bit results, which for the purpose of
     * simplifying tcg are sign-extended.  Certainly that's how we
     * represent our constants elsewhere.  Note that the bits will
     * be reset properly for a 64-bit value when encountering the
     * type changing opcodes.
     */
    if (ctx->type == TCG_TYPE_I32) {
        a_mask = (int32_t)a_mask;
        z_mask = (int32_t)z_mask;
        s_mask |= MAKE_64BIT_MASK(32, 32);
        ctx->z_mask = z_mask;
        ctx->s_mask = s_mask;
    }

    if (z_mask == 0) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], 0);
    }
    if (a_mask == 0) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
    }
    return false;
}

/*
 * Convert @op to NOT, if NOT is supported by the host.
 * Return true f the conversion is successful, which will still
 * indicate that the processing is complete.
 */
static bool fold_not(OptContext *ctx, TCGOp *op);
static bool fold_to_not(OptContext *ctx, TCGOp *op, int idx)
{
    TCGOpcode not_op;
    bool have_not;

    switch (ctx->type) {
    case TCG_TYPE_I32:
        not_op = INDEX_op_not_i32;
        have_not = TCG_TARGET_HAS_not_i32;
        break;
    case TCG_TYPE_I64:
        not_op = INDEX_op_not_i64;
        have_not = TCG_TARGET_HAS_not_i64;
        break;
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        not_op = INDEX_op_not_vec;
        have_not = TCG_TARGET_HAS_not_vec;
        break;
    default:
        g_assert_not_reached();
    }
    if (have_not) {
        op->opc = not_op;
        op->args[1] = op->args[idx];
        return fold_not(ctx, op);
    }
    return false;
}

/* If the binary operation has first argument @i, fold to @i. */
static bool fold_ix_to_i(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[1], i)) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], i);
    }
    return false;
}

/* If the binary operation has first argument @i, fold to NOT. */
static bool fold_ix_to_not(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[1], i)) {
        return fold_to_not(ctx, op, 2);
    }
    return false;
}

/* If the binary operation has second argument @i, fold to @i. */
static bool fold_xi_to_i(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[2], i)) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], i);
    }
    return false;
}

/* If the binary operation has second argument @i, fold to identity. */
static bool fold_xi_to_x(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[2], i)) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
    }
    return false;
}

/* If the binary operation has second argument @i, fold to NOT. */
static bool fold_xi_to_not(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (arg_is_const_val(op->args[2], i)) {
        return fold_to_not(ctx, op, 1);
    }
    return false;
}

/* If the binary operation has both arguments equal, fold to @i. */
static bool fold_xx_to_i(OptContext *ctx, TCGOp *op, uint64_t i)
{
    if (args_are_copies(op->args[1], op->args[2])) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], i);
    }
    return false;
}

/* If the binary operation has both arguments equal, fold to identity. */
static bool fold_xx_to_x(OptContext *ctx, TCGOp *op)
{
    if (args_are_copies(op->args[1], op->args[2])) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
    }
    return false;
}

/*
 * These outermost fold_<op> functions are sorted alphabetically.
 *
 * The ordering of the transformations should be:
 *   1) those that produce a constant
 *   2) those that produce a copy
 *   3) those that produce information about the result value.
 */

static bool fold_add(OptContext *ctx, TCGOp *op)
{
    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_x(ctx, op, 0)) {
        return true;
    }
    return false;
}

/* We cannot as yet do_constant_folding with vectors. */
static bool fold_add_vec(OptContext *ctx, TCGOp *op)
{
    if (fold_commutative(ctx, op) ||
        fold_xi_to_x(ctx, op, 0)) {
        return true;
    }
    return false;
}

static bool fold_addsub2(OptContext *ctx, TCGOp *op, bool add)
{
    if (arg_is_const(op->args[2]) && arg_is_const(op->args[3]) &&
        arg_is_const(op->args[4]) && arg_is_const(op->args[5])) {
        uint64_t al = arg_info(op->args[2])->val;
        uint64_t ah = arg_info(op->args[3])->val;
        uint64_t bl = arg_info(op->args[4])->val;
        uint64_t bh = arg_info(op->args[5])->val;
        TCGArg rl, rh;
        TCGOp *op2;

        if (ctx->type == TCG_TYPE_I32) {
            uint64_t a = deposit64(al, 32, 32, ah);
            uint64_t b = deposit64(bl, 32, 32, bh);

            if (add) {
                a += b;
            } else {
                a -= b;
            }

            al = sextract64(a, 0, 32);
            ah = sextract64(a, 32, 32);
        } else {
            Int128 a = int128_make128(al, ah);
            Int128 b = int128_make128(bl, bh);

            if (add) {
                a = int128_add(a, b);
            } else {
                a = int128_sub(a, b);
            }

            al = int128_getlo(a);
            ah = int128_gethi(a);
        }

        rl = op->args[0];
        rh = op->args[1];

        /* The proper opcode is supplied by tcg_opt_gen_mov. */
        op2 = tcg_op_insert_before(ctx->tcg, op, 0, 2);

        tcg_opt_gen_movi(ctx, op, rl, al);
        tcg_opt_gen_movi(ctx, op2, rh, ah);
        return true;
    }
    return false;
}

static bool fold_add2(OptContext *ctx, TCGOp *op)
{
    /* Note that the high and low parts may be independently swapped. */
    swap_commutative(op->args[0], &op->args[2], &op->args[4]);
    swap_commutative(op->args[1], &op->args[3], &op->args[5]);

    return fold_addsub2(ctx, op, true);
}

static bool fold_and(OptContext *ctx, TCGOp *op)
{
    uint64_t z1, z2;

    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, -1) ||
        fold_xx_to_x(ctx, op)) {
        return true;
    }

    z1 = arg_info(op->args[1])->z_mask;
    z2 = arg_info(op->args[2])->z_mask;
    ctx->z_mask = z1 & z2;

    /*
     * Sign repetitions are perforce all identical, whether they are 1 or 0.
     * Bitwise operations preserve the relative quantity of the repetitions.
     */
    ctx->s_mask = arg_info(op->args[1])->s_mask
                & arg_info(op->args[2])->s_mask;

    /*
     * Known-zeros does not imply known-ones.  Therefore unless
     * arg2 is constant, we can't infer affected bits from it.
     */
    if (arg_is_const(op->args[2])) {
        ctx->a_mask = z1 & ~z2;
    }

    return fold_masks(ctx, op);
}

static bool fold_andc(OptContext *ctx, TCGOp *op)
{
    uint64_t z1;

    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 0) ||
        fold_ix_to_not(ctx, op, -1)) {
        return true;
    }

    z1 = arg_info(op->args[1])->z_mask;

    /*
     * Known-zeros does not imply known-ones.  Therefore unless
     * arg2 is constant, we can't infer anything from it.
     */
    if (arg_is_const(op->args[2])) {
        uint64_t z2 = ~arg_info(op->args[2])->z_mask;
        ctx->a_mask = z1 & ~z2;
        z1 &= z2;
    }
    ctx->z_mask = z1;

    ctx->s_mask = arg_info(op->args[1])->s_mask
                & arg_info(op->args[2])->s_mask;
    return fold_masks(ctx, op);
}

static bool fold_brcond(OptContext *ctx, TCGOp *op)
{
    int i = do_constant_folding_cond1(ctx, NO_DEST, &op->args[0],
                                      &op->args[1], &op->args[2]);
    if (i == 0) {
        tcg_op_remove(ctx->tcg, op);
        return true;
    }
    if (i > 0) {
        op->opc = INDEX_op_br;
        op->args[0] = op->args[3];
    }
    return false;
}

static bool fold_brcond2(OptContext *ctx, TCGOp *op)
{
    TCGCond cond;
    TCGArg label;
    int i, inv = 0;

    i = do_constant_folding_cond2(ctx, &op->args[0]);
    cond = op->args[4];
    label = op->args[5];
    if (i >= 0) {
        goto do_brcond_const;
    }

    switch (cond) {
    case TCG_COND_LT:
    case TCG_COND_GE:
        /*
         * Simplify LT/GE comparisons vs zero to a single compare
         * vs the high word of the input.
         */
        if (arg_is_const_val(op->args[2], 0) &&
            arg_is_const_val(op->args[3], 0)) {
            goto do_brcond_high;
        }
        break;

    case TCG_COND_NE:
        inv = 1;
        QEMU_FALLTHROUGH;
    case TCG_COND_EQ:
        /*
         * Simplify EQ/NE comparisons where one of the pairs
         * can be simplified.
         */
        i = do_constant_folding_cond(TCG_TYPE_I32, op->args[0],
                                     op->args[2], cond);
        switch (i ^ inv) {
        case 0:
            goto do_brcond_const;
        case 1:
            goto do_brcond_high;
        }

        i = do_constant_folding_cond(TCG_TYPE_I32, op->args[1],
                                     op->args[3], cond);
        switch (i ^ inv) {
        case 0:
            goto do_brcond_const;
        case 1:
            goto do_brcond_low;
        }
        break;

    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        if (arg_is_const_val(op->args[2], 0)) {
            goto do_brcond_high;
        }
        if (arg_is_const_val(op->args[3], 0)) {
            goto do_brcond_low;
        }
        break;

    default:
        break;

    do_brcond_low:
        op->opc = INDEX_op_brcond_i32;
        op->args[1] = op->args[2];
        op->args[2] = cond;
        op->args[3] = label;
        return fold_brcond(ctx, op);

    do_brcond_high:
        op->opc = INDEX_op_brcond_i32;
        op->args[0] = op->args[1];
        op->args[1] = op->args[3];
        op->args[2] = cond;
        op->args[3] = label;
        return fold_brcond(ctx, op);

    do_brcond_const:
        if (i == 0) {
            tcg_op_remove(ctx->tcg, op);
            return true;
        }
        op->opc = INDEX_op_br;
        op->args[0] = label;
        break;
    }
    return false;
}

static bool fold_bswap(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask, s_mask, sign;

    if (arg_is_const(op->args[1])) {
        uint64_t t = arg_info(op->args[1])->val;

        t = do_constant_folding(op->opc, ctx->type, t, op->args[2]);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }

    z_mask = arg_info(op->args[1])->z_mask;

    switch (op->opc) {
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
        z_mask = bswap16(z_mask);
        sign = INT16_MIN;
        break;
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap32_i64:
        z_mask = bswap32(z_mask);
        sign = INT32_MIN;
        break;
    case INDEX_op_bswap64_i64:
        z_mask = bswap64(z_mask);
        sign = INT64_MIN;
        break;
    default:
        g_assert_not_reached();
    }
    s_mask = smask_from_zmask(z_mask);

    switch (op->args[2] & (TCG_BSWAP_OZ | TCG_BSWAP_OS)) {
    case TCG_BSWAP_OZ:
        break;
    case TCG_BSWAP_OS:
        /* If the sign bit may be 1, force all the bits above to 1. */
        if (z_mask & sign) {
            z_mask |= sign;
            s_mask = sign << 1;
        }
        break;
    default:
        /* The high bits are undefined: force all bits above the sign to 1. */
        z_mask |= sign << 1;
        s_mask = 0;
        break;
    }
    ctx->z_mask = z_mask;
    ctx->s_mask = s_mask;

    return fold_masks(ctx, op);
}

static bool fold_call(OptContext *ctx, TCGOp *op)
{
    TCGContext *s = ctx->tcg;
    int nb_oargs = TCGOP_CALLO(op);
    int nb_iargs = TCGOP_CALLI(op);
    int flags, i;

    init_arguments(ctx, op, nb_oargs + nb_iargs);
    copy_propagate(ctx, op, nb_oargs, nb_iargs);

    /* If the function reads or writes globals, reset temp data. */
    flags = tcg_call_flags(op);
    if (!(flags & (TCG_CALL_NO_READ_GLOBALS | TCG_CALL_NO_WRITE_GLOBALS))) {
        int nb_globals = s->nb_globals;

        for (i = 0; i < nb_globals; i++) {
            if (test_bit(i, ctx->temps_used.l)) {
                reset_ts(&ctx->tcg->temps[i]);
            }
        }
    }

    /* Reset temp data for outputs. */
    for (i = 0; i < nb_oargs; i++) {
        reset_temp(op->args[i]);
    }

    /* Stop optimizing MB across calls. */
    ctx->prev_mb = NULL;
    return true;
}

static bool fold_count_zeros(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask;

    if (arg_is_const(op->args[1])) {
        uint64_t t = arg_info(op->args[1])->val;

        if (t != 0) {
            t = do_constant_folding(op->opc, ctx->type, t, 0);
            return tcg_opt_gen_movi(ctx, op, op->args[0], t);
        }
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[2]);
    }

    switch (ctx->type) {
    case TCG_TYPE_I32:
        z_mask = 31;
        break;
    case TCG_TYPE_I64:
        z_mask = 63;
        break;
    default:
        g_assert_not_reached();
    }
    ctx->z_mask = arg_info(op->args[2])->z_mask | z_mask;
    ctx->s_mask = smask_from_zmask(ctx->z_mask);
    return false;
}

static bool fold_ctpop(OptContext *ctx, TCGOp *op)
{
    if (fold_const1(ctx, op)) {
        return true;
    }

    switch (ctx->type) {
    case TCG_TYPE_I32:
        ctx->z_mask = 32 | 31;
        break;
    case TCG_TYPE_I64:
        ctx->z_mask = 64 | 63;
        break;
    default:
        g_assert_not_reached();
    }
    ctx->s_mask = smask_from_zmask(ctx->z_mask);
    return false;
}

static bool fold_deposit(OptContext *ctx, TCGOp *op)
{
    TCGOpcode and_opc;

    if (arg_is_const(op->args[1]) && arg_is_const(op->args[2])) {
        uint64_t t1 = arg_info(op->args[1])->val;
        uint64_t t2 = arg_info(op->args[2])->val;

        t1 = deposit64(t1, op->args[3], op->args[4], t2);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t1);
    }

    switch (ctx->type) {
    case TCG_TYPE_I32:
        and_opc = INDEX_op_and_i32;
        break;
    case TCG_TYPE_I64:
        and_opc = INDEX_op_and_i64;
        break;
    default:
        g_assert_not_reached();
    }

    /* Inserting a value into zero at offset 0. */
    if (arg_is_const_val(op->args[1], 0) && op->args[3] == 0) {
        uint64_t mask = MAKE_64BIT_MASK(0, op->args[4]);

        op->opc = and_opc;
        op->args[1] = op->args[2];
        op->args[2] = arg_new_constant(ctx, mask);
        ctx->z_mask = mask & arg_info(op->args[1])->z_mask;
        return false;
    }

    /* Inserting zero into a value. */
    if (arg_is_const_val(op->args[2], 0)) {
        uint64_t mask = deposit64(-1, op->args[3], op->args[4], 0);

        op->opc = and_opc;
        op->args[2] = arg_new_constant(ctx, mask);
        ctx->z_mask = mask & arg_info(op->args[1])->z_mask;
        return false;
    }

    ctx->z_mask = deposit64(arg_info(op->args[1])->z_mask,
                            op->args[3], op->args[4],
                            arg_info(op->args[2])->z_mask);
    return false;
}

static bool fold_divide(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xi_to_x(ctx, op, 1)) {
        return true;
    }
    return false;
}

static bool fold_dup(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1])) {
        uint64_t t = arg_info(op->args[1])->val;
        t = dup_const(TCGOP_VECE(op), t);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }
    return false;
}

static bool fold_dup2(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1]) && arg_is_const(op->args[2])) {
        uint64_t t = deposit64(arg_info(op->args[1])->val, 32, 32,
                               arg_info(op->args[2])->val);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }

    if (args_are_copies(op->args[1], op->args[2])) {
        op->opc = INDEX_op_dup_vec;
        TCGOP_VECE(op) = MO_32;
    }
    return false;
}

static bool fold_eqv(OptContext *ctx, TCGOp *op)
{
    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_x(ctx, op, -1) ||
        fold_xi_to_not(ctx, op, 0)) {
        return true;
    }

    ctx->s_mask = arg_info(op->args[1])->s_mask
                & arg_info(op->args[2])->s_mask;
    return false;
}

static bool fold_extract(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask_old, z_mask;
    int pos = op->args[2];
    int len = op->args[3];

    if (arg_is_const(op->args[1])) {
        uint64_t t;

        t = arg_info(op->args[1])->val;
        t = extract64(t, pos, len);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }

    z_mask_old = arg_info(op->args[1])->z_mask;
    z_mask = extract64(z_mask_old, pos, len);
    if (pos == 0) {
        ctx->a_mask = z_mask_old ^ z_mask;
    }
    ctx->z_mask = z_mask;
    ctx->s_mask = smask_from_zmask(z_mask);

    return fold_masks(ctx, op);
}

static bool fold_extract2(OptContext *ctx, TCGOp *op)
{
    if (arg_is_const(op->args[1]) && arg_is_const(op->args[2])) {
        uint64_t v1 = arg_info(op->args[1])->val;
        uint64_t v2 = arg_info(op->args[2])->val;
        int shr = op->args[3];

        if (op->opc == INDEX_op_extract2_i64) {
            v1 >>= shr;
            v2 <<= 64 - shr;
        } else {
            v1 = (uint32_t)v1 >> shr;
            v2 = (uint64_t)((int32_t)v2 << (32 - shr));
        }
        return tcg_opt_gen_movi(ctx, op, op->args[0], v1 | v2);
    }
    return false;
}

static bool fold_exts(OptContext *ctx, TCGOp *op)
{
    uint64_t s_mask_old, s_mask, z_mask, sign;
    bool type_change = false;

    if (fold_const1(ctx, op)) {
        return true;
    }

    z_mask = arg_info(op->args[1])->z_mask;
    s_mask = arg_info(op->args[1])->s_mask;
    s_mask_old = s_mask;

    switch (op->opc) {
    CASE_OP_32_64(ext8s):
        sign = INT8_MIN;
        z_mask = (uint8_t)z_mask;
        break;
    CASE_OP_32_64(ext16s):
        sign = INT16_MIN;
        z_mask = (uint16_t)z_mask;
        break;
    case INDEX_op_ext_i32_i64:
        type_change = true;
        QEMU_FALLTHROUGH;
    case INDEX_op_ext32s_i64:
        sign = INT32_MIN;
        z_mask = (uint32_t)z_mask;
        break;
    default:
        g_assert_not_reached();
    }

    if (z_mask & sign) {
        z_mask |= sign;
    }
    s_mask |= sign << 1;

    ctx->z_mask = z_mask;
    ctx->s_mask = s_mask;
    if (!type_change) {
        ctx->a_mask = s_mask & ~s_mask_old;
    }

    return fold_masks(ctx, op);
}

static bool fold_extu(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask_old, z_mask;
    bool type_change = false;

    if (fold_const1(ctx, op)) {
        return true;
    }

    z_mask_old = z_mask = arg_info(op->args[1])->z_mask;

    switch (op->opc) {
    CASE_OP_32_64(ext8u):
        z_mask = (uint8_t)z_mask;
        break;
    CASE_OP_32_64(ext16u):
        z_mask = (uint16_t)z_mask;
        break;
    case INDEX_op_extrl_i64_i32:
    case INDEX_op_extu_i32_i64:
        type_change = true;
        QEMU_FALLTHROUGH;
    case INDEX_op_ext32u_i64:
        z_mask = (uint32_t)z_mask;
        break;
    case INDEX_op_extrh_i64_i32:
        type_change = true;
        z_mask >>= 32;
        break;
    default:
        g_assert_not_reached();
    }

    ctx->z_mask = z_mask;
    ctx->s_mask = smask_from_zmask(z_mask);
    if (!type_change) {
        ctx->a_mask = z_mask_old ^ z_mask;
    }
    return fold_masks(ctx, op);
}

static bool fold_mb(OptContext *ctx, TCGOp *op)
{
    /* Eliminate duplicate and redundant fence instructions.  */
    if (ctx->prev_mb) {
        /*
         * Merge two barriers of the same type into one,
         * or a weaker barrier into a stronger one,
         * or two weaker barriers into a stronger one.
         *   mb X; mb Y => mb X|Y
         *   mb; strl => mb; st
         *   ldaq; mb => ld; mb
         *   ldaq; strl => ld; mb; st
         * Other combinations are also merged into a strong
         * barrier.  This is stricter than specified but for
         * the purposes of TCG is better than not optimizing.
         */
        ctx->prev_mb->args[0] |= op->args[0];
        tcg_op_remove(ctx->tcg, op);
    } else {
        ctx->prev_mb = op;
    }
    return true;
}

static bool fold_mov(OptContext *ctx, TCGOp *op)
{
    return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[1]);
}

static bool fold_movcond(OptContext *ctx, TCGOp *op)
{
    int i;

    /*
     * Canonicalize the "false" input reg to match the destination reg so
     * that the tcg backend can implement a "move if true" operation.
     */
    if (swap_commutative(op->args[0], &op->args[4], &op->args[3])) {
        op->args[5] = tcg_invert_cond(op->args[5]);
    }

    i = do_constant_folding_cond1(ctx, NO_DEST, &op->args[1],
                                  &op->args[2], &op->args[5]);
    if (i >= 0) {
        return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[4 - i]);
    }

    ctx->z_mask = arg_info(op->args[3])->z_mask
                | arg_info(op->args[4])->z_mask;
    ctx->s_mask = arg_info(op->args[3])->s_mask
                & arg_info(op->args[4])->s_mask;

    if (arg_is_const(op->args[3]) && arg_is_const(op->args[4])) {
        uint64_t tv = arg_info(op->args[3])->val;
        uint64_t fv = arg_info(op->args[4])->val;
        TCGOpcode opc, negopc = 0;
        TCGCond cond = op->args[5];

        switch (ctx->type) {
        case TCG_TYPE_I32:
            opc = INDEX_op_setcond_i32;
            if (TCG_TARGET_HAS_negsetcond_i32) {
                negopc = INDEX_op_negsetcond_i32;
            }
            tv = (int32_t)tv;
            fv = (int32_t)fv;
            break;
        case TCG_TYPE_I64:
            opc = INDEX_op_setcond_i64;
            if (TCG_TARGET_HAS_negsetcond_i64) {
                negopc = INDEX_op_negsetcond_i64;
            }
            break;
        default:
            g_assert_not_reached();
        }

        if (tv == 1 && fv == 0) {
            op->opc = opc;
            op->args[3] = cond;
        } else if (fv == 1 && tv == 0) {
            op->opc = opc;
            op->args[3] = tcg_invert_cond(cond);
        } else if (negopc) {
            if (tv == -1 && fv == 0) {
                op->opc = negopc;
                op->args[3] = cond;
            } else if (fv == -1 && tv == 0) {
                op->opc = negopc;
                op->args[3] = tcg_invert_cond(cond);
            }
        }
    }
    return false;
}

static bool fold_mul(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xi_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 1)) {
        return true;
    }
    return false;
}

static bool fold_mul_highpart(OptContext *ctx, TCGOp *op)
{
    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_i(ctx, op, 0)) {
        return true;
    }
    return false;
}

static bool fold_multiply2(OptContext *ctx, TCGOp *op)
{
    swap_commutative(op->args[0], &op->args[2], &op->args[3]);

    if (arg_is_const(op->args[2]) && arg_is_const(op->args[3])) {
        uint64_t a = arg_info(op->args[2])->val;
        uint64_t b = arg_info(op->args[3])->val;
        uint64_t h, l;
        TCGArg rl, rh;
        TCGOp *op2;

        switch (op->opc) {
        case INDEX_op_mulu2_i32:
            l = (uint64_t)(uint32_t)a * (uint32_t)b;
            h = (int32_t)(l >> 32);
            l = (int32_t)l;
            break;
        case INDEX_op_muls2_i32:
            l = (int64_t)(int32_t)a * (int32_t)b;
            h = l >> 32;
            l = (int32_t)l;
            break;
        case INDEX_op_mulu2_i64:
            mulu64(&l, &h, a, b);
            break;
        case INDEX_op_muls2_i64:
            muls64(&l, &h, a, b);
            break;
        default:
            g_assert_not_reached();
        }

        rl = op->args[0];
        rh = op->args[1];

        /* The proper opcode is supplied by tcg_opt_gen_mov. */
        op2 = tcg_op_insert_before(ctx->tcg, op, 0, 2);

        tcg_opt_gen_movi(ctx, op, rl, l);
        tcg_opt_gen_movi(ctx, op2, rh, h);
        return true;
    }
    return false;
}

static bool fold_nand(OptContext *ctx, TCGOp *op)
{
    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_not(ctx, op, -1)) {
        return true;
    }

    ctx->s_mask = arg_info(op->args[1])->s_mask
                & arg_info(op->args[2])->s_mask;
    return false;
}

static bool fold_neg(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask;

    if (fold_const1(ctx, op)) {
        return true;
    }

    /* Set to 1 all bits to the left of the rightmost.  */
    z_mask = arg_info(op->args[1])->z_mask;
    ctx->z_mask = -(z_mask & -z_mask);

    /*
     * Because of fold_sub_to_neg, we want to always return true,
     * via finish_folding.
     */
    finish_folding(ctx, op);
    return true;
}

static bool fold_nor(OptContext *ctx, TCGOp *op)
{
    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_not(ctx, op, 0)) {
        return true;
    }

    ctx->s_mask = arg_info(op->args[1])->s_mask
                & arg_info(op->args[2])->s_mask;
    return false;
}

static bool fold_not(OptContext *ctx, TCGOp *op)
{
    if (fold_const1(ctx, op)) {
        return true;
    }

    ctx->s_mask = arg_info(op->args[1])->s_mask;

    /* Because of fold_to_not, we want to always return true, via finish. */
    finish_folding(ctx, op);
    return true;
}

static bool fold_or(OptContext *ctx, TCGOp *op)
{
    if (fold_const2_commutative(ctx, op) ||
        fold_xi_to_x(ctx, op, 0) ||
        fold_xx_to_x(ctx, op)) {
        return true;
    }

    ctx->z_mask = arg_info(op->args[1])->z_mask
                | arg_info(op->args[2])->z_mask;
    ctx->s_mask = arg_info(op->args[1])->s_mask
                & arg_info(op->args[2])->s_mask;
    return fold_masks(ctx, op);
}

static bool fold_orc(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, -1) ||
        fold_xi_to_x(ctx, op, -1) ||
        fold_ix_to_not(ctx, op, 0)) {
        return true;
    }

    ctx->s_mask = arg_info(op->args[1])->s_mask
                & arg_info(op->args[2])->s_mask;
    return false;
}

static bool fold_qemu_ld(OptContext *ctx, TCGOp *op)
{
    const TCGOpDef *def = &tcg_op_defs[op->opc];
    MemOpIdx oi = op->args[def->nb_oargs + def->nb_iargs];
    MemOp mop = get_memop(oi);
    int width = 8 * memop_size(mop);

    if (width < 64) {
        ctx->s_mask = MAKE_64BIT_MASK(width, 64 - width);
        if (!(mop & MO_SIGN)) {
            ctx->z_mask = MAKE_64BIT_MASK(0, width);
            ctx->s_mask <<= 1;
        }
    }

    /* Opcodes that touch guest memory stop the mb optimization.  */
    ctx->prev_mb = NULL;
    return false;
}

static bool fold_qemu_st(OptContext *ctx, TCGOp *op)
{
    /* Opcodes that touch guest memory stop the mb optimization.  */
    ctx->prev_mb = NULL;
    return false;
}

static bool fold_remainder(OptContext *ctx, TCGOp *op)
{
    if (fold_const2(ctx, op) ||
        fold_xx_to_i(ctx, op, 0)) {
        return true;
    }
    return false;
}

static void fold_setcond_tst_pow2(OptContext *ctx, TCGOp *op, bool neg)
{
    TCGOpcode and_opc, sub_opc, xor_opc, neg_opc, shr_opc, uext_opc, sext_opc;
    TCGCond cond = op->args[3];
    TCGArg ret, src1, src2;
    TCGOp *op2;
    uint64_t val;
    int sh;
    bool inv;

    if (!is_tst_cond(cond) || !arg_is_const(op->args[2])) {
        return;
    }

    src2 = op->args[2];
    val = arg_info(src2)->val;
    if (!is_power_of_2(val)) {
        return;
    }
    sh = ctz64(val);

    switch (ctx->type) {
    case TCG_TYPE_I32:
        and_opc = INDEX_op_and_i32;
        sub_opc = INDEX_op_sub_i32;
        xor_opc = INDEX_op_xor_i32;
        shr_opc = INDEX_op_shr_i32;
        neg_opc = TCG_TARGET_HAS_neg_i32 ? INDEX_op_neg_i32 : 0;
        if (TCG_TARGET_extract_i32_valid(sh, 1)) {
            uext_opc = TCG_TARGET_HAS_extract_i32 ? INDEX_op_extract_i32 : 0;
            sext_opc = TCG_TARGET_HAS_sextract_i32 ? INDEX_op_sextract_i32 : 0;
        }
        break;
    case TCG_TYPE_I64:
        and_opc = INDEX_op_and_i64;
        sub_opc = INDEX_op_sub_i64;
        xor_opc = INDEX_op_xor_i64;
        shr_opc = INDEX_op_shr_i64;
        neg_opc = TCG_TARGET_HAS_neg_i64 ? INDEX_op_neg_i64 : 0;
        if (TCG_TARGET_extract_i64_valid(sh, 1)) {
            uext_opc = TCG_TARGET_HAS_extract_i64 ? INDEX_op_extract_i64 : 0;
            sext_opc = TCG_TARGET_HAS_sextract_i64 ? INDEX_op_sextract_i64 : 0;
        }
        break;
    default:
        g_assert_not_reached();
    }

    ret = op->args[0];
    src1 = op->args[1];
    inv = cond == TCG_COND_TSTEQ;

    if (sh && sext_opc && neg && !inv) {
        op->opc = sext_opc;
        op->args[1] = src1;
        op->args[2] = sh;
        op->args[3] = 1;
        return;
    } else if (sh && uext_opc) {
        op->opc = uext_opc;
        op->args[1] = src1;
        op->args[2] = sh;
        op->args[3] = 1;
    } else {
        if (sh) {
            op2 = tcg_op_insert_before(ctx->tcg, op, shr_opc, 3);
            op2->args[0] = ret;
            op2->args[1] = src1;
            op2->args[2] = arg_new_constant(ctx, sh);
            src1 = ret;
        }
        op->opc = and_opc;
        op->args[1] = src1;
        op->args[2] = arg_new_constant(ctx, 1);
    }

    if (neg && inv) {
        op2 = tcg_op_insert_after(ctx->tcg, op, sub_opc, 3);
        op2->args[0] = ret;
        op2->args[1] = ret;
        op2->args[2] = arg_new_constant(ctx, 1);
    } else if (inv) {
        op2 = tcg_op_insert_after(ctx->tcg, op, xor_opc, 3);
        op2->args[0] = ret;
        op2->args[1] = ret;
        op2->args[2] = arg_new_constant(ctx, 1);
    } else if (neg && neg_opc) {
        op2 = tcg_op_insert_after(ctx->tcg, op, neg_opc, 2);
        op2->args[0] = ret;
        op2->args[1] = ret;
    } else if (neg) {
        op2 = tcg_op_insert_after(ctx->tcg, op, sub_opc, 3);
        op2->args[0] = ret;
        op2->args[1] = arg_new_constant(ctx, 0);
        op2->args[2] = ret;
    }
}

static bool fold_setcond(OptContext *ctx, TCGOp *op)
{
    int i = do_constant_folding_cond1(ctx, op->args[0], &op->args[1],
                                      &op->args[2], &op->args[3]);
    if (i >= 0) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], i);
    }
    fold_setcond_tst_pow2(ctx, op, false);

    ctx->z_mask = 1;
    ctx->s_mask = smask_from_zmask(1);
    return false;
}

static bool fold_negsetcond(OptContext *ctx, TCGOp *op)
{
    int i = do_constant_folding_cond1(ctx, op->args[0], &op->args[1],
                                      &op->args[2], &op->args[3]);
    if (i >= 0) {
        return tcg_opt_gen_movi(ctx, op, op->args[0], -i);
    }
    fold_setcond_tst_pow2(ctx, op, true);

    /* Value is {0,-1} so all bits are repetitions of the sign. */
    ctx->s_mask = -1;
    return false;
}

static bool fold_setcond2(OptContext *ctx, TCGOp *op)
{
    TCGCond cond;
    int i, inv = 0;

    i = do_constant_folding_cond2(ctx, &op->args[1]);
    cond = op->args[5];
    if (i >= 0) {
        goto do_setcond_const;
    }

    switch (cond) {
    case TCG_COND_LT:
    case TCG_COND_GE:
        /*
         * Simplify LT/GE comparisons vs zero to a single compare
         * vs the high word of the input.
         */
        if (arg_is_const_val(op->args[3], 0) &&
            arg_is_const_val(op->args[4], 0)) {
            goto do_setcond_high;
        }
        break;

    case TCG_COND_NE:
        inv = 1;
        QEMU_FALLTHROUGH;
    case TCG_COND_EQ:
        /*
         * Simplify EQ/NE comparisons where one of the pairs
         * can be simplified.
         */
        i = do_constant_folding_cond(TCG_TYPE_I32, op->args[1],
                                     op->args[3], cond);
        switch (i ^ inv) {
        case 0:
            goto do_setcond_const;
        case 1:
            goto do_setcond_high;
        }

        i = do_constant_folding_cond(TCG_TYPE_I32, op->args[2],
                                     op->args[4], cond);
        switch (i ^ inv) {
        case 0:
            goto do_setcond_const;
        case 1:
            goto do_setcond_low;
        }
        break;

    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        if (arg_is_const_val(op->args[2], 0)) {
            goto do_setcond_high;
        }
        if (arg_is_const_val(op->args[4], 0)) {
            goto do_setcond_low;
        }
        break;

    default:
        break;

    do_setcond_low:
        op->args[2] = op->args[3];
        op->args[3] = cond;
        op->opc = INDEX_op_setcond_i32;
        return fold_setcond(ctx, op);

    do_setcond_high:
        op->args[1] = op->args[2];
        op->args[2] = op->args[4];
        op->args[3] = cond;
        op->opc = INDEX_op_setcond_i32;
        return fold_setcond(ctx, op);
    }

    ctx->z_mask = 1;
    ctx->s_mask = smask_from_zmask(1);
    return false;

 do_setcond_const:
    return tcg_opt_gen_movi(ctx, op, op->args[0], i);
}

static bool fold_sextract(OptContext *ctx, TCGOp *op)
{
    uint64_t z_mask, s_mask, s_mask_old;
    int pos = op->args[2];
    int len = op->args[3];

    if (arg_is_const(op->args[1])) {
        uint64_t t;

        t = arg_info(op->args[1])->val;
        t = sextract64(t, pos, len);
        return tcg_opt_gen_movi(ctx, op, op->args[0], t);
    }

    z_mask = arg_info(op->args[1])->z_mask;
    z_mask = sextract64(z_mask, pos, len);
    ctx->z_mask = z_mask;

    s_mask_old = arg_info(op->args[1])->s_mask;
    s_mask = sextract64(s_mask_old, pos, len);
    s_mask |= MAKE_64BIT_MASK(len, 64 - len);
    ctx->s_mask = s_mask;

    if (pos == 0) {
        ctx->a_mask = s_mask & ~s_mask_old;
    }

    return fold_masks(ctx, op);
}

static bool fold_shift(OptContext *ctx, TCGOp *op)
{
    uint64_t s_mask, z_mask, sign;

    if (fold_const2(ctx, op) ||
        fold_ix_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 0)) {
        return true;
    }

    s_mask = arg_info(op->args[1])->s_mask;
    z_mask = arg_info(op->args[1])->z_mask;

    if (arg_is_const(op->args[2])) {
        int sh = arg_info(op->args[2])->val;

        ctx->z_mask = do_constant_folding(op->opc, ctx->type, z_mask, sh);

        s_mask = do_constant_folding(op->opc, ctx->type, s_mask, sh);
        ctx->s_mask = smask_from_smask(s_mask);

        return fold_masks(ctx, op);
    }

    switch (op->opc) {
    CASE_OP_32_64(sar):
        /*
         * Arithmetic right shift will not reduce the number of
         * input sign repetitions.
         */
        ctx->s_mask = s_mask;
        break;
    CASE_OP_32_64(shr):
        /*
         * If the sign bit is known zero, then logical right shift
         * will not reduced the number of input sign repetitions.
         */
        sign = (s_mask & -s_mask) >> 1;
        if (!(z_mask & sign)) {
            ctx->s_mask = s_mask;
        }
        break;
    default:
        break;
    }

    return false;
}

static bool fold_sub_to_neg(OptContext *ctx, TCGOp *op)
{
    TCGOpcode neg_op;
    bool have_neg;

    if (!arg_is_const(op->args[1]) || arg_info(op->args[1])->val != 0) {
        return false;
    }

    switch (ctx->type) {
    case TCG_TYPE_I32:
        neg_op = INDEX_op_neg_i32;
        have_neg = TCG_TARGET_HAS_neg_i32;
        break;
    case TCG_TYPE_I64:
        neg_op = INDEX_op_neg_i64;
        have_neg = TCG_TARGET_HAS_neg_i64;
        break;
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        neg_op = INDEX_op_neg_vec;
        have_neg = (TCG_TARGET_HAS_neg_vec &&
                    tcg_can_emit_vec_op(neg_op, ctx->type, TCGOP_VECE(op)) > 0);
        break;
    default:
        g_assert_not_reached();
    }
    if (have_neg) {
        op->opc = neg_op;
        op->args[1] = op->args[2];
        return fold_neg(ctx, op);
    }
    return false;
}

/* We cannot as yet do_constant_folding with vectors. */
static bool fold_sub_vec(OptContext *ctx, TCGOp *op)
{
    if (fold_xx_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 0) ||
        fold_sub_to_neg(ctx, op)) {
        return true;
    }
    return false;
}

static bool fold_sub(OptContext *ctx, TCGOp *op)
{
    return fold_const2(ctx, op) || fold_sub_vec(ctx, op);
}

static bool fold_sub2(OptContext *ctx, TCGOp *op)
{
    return fold_addsub2(ctx, op, false);
}

static bool fold_tcg_ld(OptContext *ctx, TCGOp *op)
{
    /* We can't do any folding with a load, but we can record bits. */
    switch (op->opc) {
    CASE_OP_32_64(ld8s):
        ctx->s_mask = MAKE_64BIT_MASK(8, 56);
        break;
    CASE_OP_32_64(ld8u):
        ctx->z_mask = MAKE_64BIT_MASK(0, 8);
        ctx->s_mask = MAKE_64BIT_MASK(9, 55);
        break;
    CASE_OP_32_64(ld16s):
        ctx->s_mask = MAKE_64BIT_MASK(16, 48);
        break;
    CASE_OP_32_64(ld16u):
        ctx->z_mask = MAKE_64BIT_MASK(0, 16);
        ctx->s_mask = MAKE_64BIT_MASK(17, 47);
        break;
    case INDEX_op_ld32s_i64:
        ctx->s_mask = MAKE_64BIT_MASK(32, 32);
        break;
    case INDEX_op_ld32u_i64:
        ctx->z_mask = MAKE_64BIT_MASK(0, 32);
        ctx->s_mask = MAKE_64BIT_MASK(33, 31);
        break;
    default:
        g_assert_not_reached();
    }
    return false;
}

static bool fold_xor(OptContext *ctx, TCGOp *op)
{
    if (fold_const2_commutative(ctx, op) ||
        fold_xx_to_i(ctx, op, 0) ||
        fold_xi_to_x(ctx, op, 0) ||
        fold_xi_to_not(ctx, op, -1)) {
        return true;
    }

    ctx->z_mask = arg_info(op->args[1])->z_mask
                | arg_info(op->args[2])->z_mask;
    ctx->s_mask = arg_info(op->args[1])->s_mask
                & arg_info(op->args[2])->s_mask;
    return fold_masks(ctx, op);
}

/* Propagate constants and copies, fold constant expressions. */
void tcg_optimize(TCGContext *s)
{
    int nb_temps, i;
    TCGOp *op, *op_next;
    OptContext ctx = { .tcg = s };

    /* Array VALS has an element for each temp.
       If this temp holds a constant then its value is kept in VALS' element.
       If this temp is a copy of other ones then the other copies are
       available through the doubly linked circular list. */

    nb_temps = s->nb_temps;
    for (i = 0; i < nb_temps; ++i) {
        s->temps[i].state_ptr = NULL;
    }

    QTAILQ_FOREACH_SAFE(op, &s->ops, link, op_next) {
        TCGOpcode opc = op->opc;
        const TCGOpDef *def;
        bool done = false;

        /* Calls are special. */
        if (opc == INDEX_op_call) {
            fold_call(&ctx, op);
            continue;
        }

        def = &tcg_op_defs[opc];
        init_arguments(&ctx, op, def->nb_oargs + def->nb_iargs);
        copy_propagate(&ctx, op, def->nb_oargs, def->nb_iargs);

        /* Pre-compute the type of the operation. */
        if (def->flags & TCG_OPF_VECTOR) {
            ctx.type = TCG_TYPE_V64 + TCGOP_VECL(op);
        } else if (def->flags & TCG_OPF_64BIT) {
            ctx.type = TCG_TYPE_I64;
        } else {
            ctx.type = TCG_TYPE_I32;
        }

        /* Assume all bits affected, no bits known zero, no sign reps. */
        ctx.a_mask = -1;
        ctx.z_mask = -1;
        ctx.s_mask = 0;

        /*
         * Process each opcode.
         * Sorted alphabetically by opcode as much as possible.
         */
        switch (opc) {
        CASE_OP_32_64(add):
            done = fold_add(&ctx, op);
            break;
        case INDEX_op_add_vec:
            done = fold_add_vec(&ctx, op);
            break;
        CASE_OP_32_64(add2):
            done = fold_add2(&ctx, op);
            break;
        CASE_OP_32_64_VEC(and):
            done = fold_and(&ctx, op);
            break;
        CASE_OP_32_64_VEC(andc):
            done = fold_andc(&ctx, op);
            break;
        CASE_OP_32_64(brcond):
            done = fold_brcond(&ctx, op);
            break;
        case INDEX_op_brcond2_i32:
            done = fold_brcond2(&ctx, op);
            break;
        CASE_OP_32_64(bswap16):
        CASE_OP_32_64(bswap32):
        case INDEX_op_bswap64_i64:
            done = fold_bswap(&ctx, op);
            break;
        CASE_OP_32_64(clz):
        CASE_OP_32_64(ctz):
            done = fold_count_zeros(&ctx, op);
            break;
        CASE_OP_32_64(ctpop):
            done = fold_ctpop(&ctx, op);
            break;
        CASE_OP_32_64(deposit):
            done = fold_deposit(&ctx, op);
            break;
        CASE_OP_32_64(div):
        CASE_OP_32_64(divu):
            done = fold_divide(&ctx, op);
            break;
        case INDEX_op_dup_vec:
            done = fold_dup(&ctx, op);
            break;
        case INDEX_op_dup2_vec:
            done = fold_dup2(&ctx, op);
            break;
        CASE_OP_32_64_VEC(eqv):
            done = fold_eqv(&ctx, op);
            break;
        CASE_OP_32_64(extract):
            done = fold_extract(&ctx, op);
            break;
        CASE_OP_32_64(extract2):
            done = fold_extract2(&ctx, op);
            break;
        CASE_OP_32_64(ext8s):
        CASE_OP_32_64(ext16s):
        case INDEX_op_ext32s_i64:
        case INDEX_op_ext_i32_i64:
            done = fold_exts(&ctx, op);
            break;
        CASE_OP_32_64(ext8u):
        CASE_OP_32_64(ext16u):
        case INDEX_op_ext32u_i64:
        case INDEX_op_extu_i32_i64:
        case INDEX_op_extrl_i64_i32:
        case INDEX_op_extrh_i64_i32:
            done = fold_extu(&ctx, op);
            break;
        CASE_OP_32_64(ld8s):
        CASE_OP_32_64(ld8u):
        CASE_OP_32_64(ld16s):
        CASE_OP_32_64(ld16u):
        case INDEX_op_ld32s_i64:
        case INDEX_op_ld32u_i64:
            done = fold_tcg_ld(&ctx, op);
            break;
        case INDEX_op_mb:
            done = fold_mb(&ctx, op);
            break;
        CASE_OP_32_64_VEC(mov):
            done = fold_mov(&ctx, op);
            break;
        CASE_OP_32_64(movcond):
            done = fold_movcond(&ctx, op);
            break;
        CASE_OP_32_64(mul):
            done = fold_mul(&ctx, op);
            break;
        CASE_OP_32_64(mulsh):
        CASE_OP_32_64(muluh):
            done = fold_mul_highpart(&ctx, op);
            break;
        CASE_OP_32_64(muls2):
        CASE_OP_32_64(mulu2):
            done = fold_multiply2(&ctx, op);
            break;
        CASE_OP_32_64_VEC(nand):
            done = fold_nand(&ctx, op);
            break;
        CASE_OP_32_64(neg):
            done = fold_neg(&ctx, op);
            break;
        CASE_OP_32_64_VEC(nor):
            done = fold_nor(&ctx, op);
            break;
        CASE_OP_32_64_VEC(not):
            done = fold_not(&ctx, op);
            break;
        CASE_OP_32_64_VEC(or):
            done = fold_or(&ctx, op);
            break;
        CASE_OP_32_64_VEC(orc):
            done = fold_orc(&ctx, op);
            break;
        case INDEX_op_qemu_ld_a32_i32:
        case INDEX_op_qemu_ld_a64_i32:
        case INDEX_op_qemu_ld_a32_i64:
        case INDEX_op_qemu_ld_a64_i64:
        case INDEX_op_qemu_ld_a32_i128:
        case INDEX_op_qemu_ld_a64_i128:
            done = fold_qemu_ld(&ctx, op);
            break;
        case INDEX_op_qemu_st8_a32_i32:
        case INDEX_op_qemu_st8_a64_i32:
        case INDEX_op_qemu_st_a32_i32:
        case INDEX_op_qemu_st_a64_i32:
        case INDEX_op_qemu_st_a32_i64:
        case INDEX_op_qemu_st_a64_i64:
        case INDEX_op_qemu_st_a32_i128:
        case INDEX_op_qemu_st_a64_i128:
            done = fold_qemu_st(&ctx, op);
            break;
        CASE_OP_32_64(rem):
        CASE_OP_32_64(remu):
            done = fold_remainder(&ctx, op);
            break;
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
            done = fold_shift(&ctx, op);
            break;
        CASE_OP_32_64(setcond):
            done = fold_setcond(&ctx, op);
            break;
        CASE_OP_32_64(negsetcond):
            done = fold_negsetcond(&ctx, op);
            break;
        case INDEX_op_setcond2_i32:
            done = fold_setcond2(&ctx, op);
            break;
        CASE_OP_32_64(sextract):
            done = fold_sextract(&ctx, op);
            break;
        CASE_OP_32_64(sub):
            done = fold_sub(&ctx, op);
            break;
        case INDEX_op_sub_vec:
            done = fold_sub_vec(&ctx, op);
            break;
        CASE_OP_32_64(sub2):
            done = fold_sub2(&ctx, op);
            break;
        CASE_OP_32_64_VEC(xor):
            done = fold_xor(&ctx, op);
            break;
        default:
            break;
        }

        if (!done) {
            finish_folding(&ctx, op);
        }
    }
}
