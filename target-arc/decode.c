#include "translate.h"
#include "translate-inst.h"

typedef int (*decode_f)(DisasCtxt *ctx, uint32_t opcode);

static void arc_decode_src(DisasCtxt *ctx, TCGv src)
{
    if (TCGV_EQUAL(src, cpu_limm)   /* register refers to limm  */
        && ctx->opt.limm == 0) {    /* limm is not yet decoded  */
        uint32_t limm;

        limm    = cpu_ldl_code(ctx->env, ctx->npc);
        limm    = (limm & 0xffff0000) >> 16
                | (limm & 0x0000ffff) << 16;

        tcg_gen_movi_tl(cpu_limm, limm);

        ctx->npc += sizeof(limm);
        ctx->opt.limm = 1;
        ctx->opt.d = 0;
    }

    if (TCGV_EQUAL(src, cpu_pcl)) {
        tcg_gen_movi_tl(cpu_pcl, ctx->pcl);
    }
}

static int arc_decode_invalid(DisasCtxt *ctx, uint32_t opcode)
{
    return BS_EXCP;
}


/*
    Branch Conditionally
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------|-------------------|-|-------------------|-|----------+
    | major   |S[10:1]            |0|S[20:11]           |N|Q[4:0]    |
    +---------|-------------------|-|-------------------|-|----------+
    |0 0 0 0 0|s s s s s s s s s s|0|S S S S S S S S S S|N|Q Q Q Q Q |
    +---------+-------------------+-+-------------------+-+----------+

    Branch Unconditional Far
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------|-------------------|-|-------------------|-|-|--------+
    | major   |S[10:1]            |1|S[20:11]           |N|R|S[24:21]|
    +---------|-------------------|-|-------------------|-|-|--------+
    |0 0 0 0 0|s s s s s s s s s s|1|S S S S S S S S S S|N|0|T T T T |
    +---------+-------------------+-+-------------------+-+-+--------+
*/
static int arc_decode_major00(DisasCtxt *ctx, uint32_t opcode)
{
    int ret;
    TCGv s;

    if (extract32(opcode, 16, 1) == 0) {
        unsigned _Q = extract32(opcode, 0, 5);
        unsigned _N = extract32(opcode, 5, 1);
        unsigned _S = extract32(opcode, 6, 10);
        unsigned _s = extract32(opcode, 17, 10);

        s = tcg_const_local_i32(sextract32((_S << 10) | _s, 0, 20));
        ctx->opt.d = _N;

        ret = arc_gen_B(ctx, s, _Q);

    } else {
        unsigned _T = extract32(opcode, 0, 4);
        unsigned _N = extract32(opcode, 5, 1);
        unsigned _S = extract32(opcode, 6, 10);
        unsigned _s = extract32(opcode, 17, 10);

        s = tcg_const_local_i32(sextract32((((_T << 10) | _S) << 10) | _s, 0,
                                                                           24));
        ctx->opt.d = _N;

        ret = arc_gen_B(ctx, s, ARC_COND_AL);
    }

    tcg_temp_free_i32(s);

    return ret;
}

/*

    Branch and Link Conditionally
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------|-----------------|-|-|-------------------|-|----------+
    | major   |S[10:2]          |0|0|S[20:11]           |N|Q[4:0]    |
    +---------|-----------------|-|-|-------------------|-|----------+
    |0 0 0 0 1|s s s s s s s s s|0|0|S S S S S S S S S S|N|Q Q Q Q Q |
    +---------+-----------------+-+-+-------------------+-+----------+

    Branch and Link Unconditional Far
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------|-----------------|-|-|-------------------|-|----------+
    | major   |S[10:2]          |0|0|S[20:11]           |N|Q[4:0]    |
    +---------|-----------------|-|-|-------------------|-|----------+
    |0 0 0 0 1|s s s s s s s s s|0|0|S S S S S S S S S S|N|Q Q Q Q Q |
    +---------+-----------------+-+-+-------------------+-+----------+

    Branch on Compare Register-Register
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------|-----|-------------|-|-|-----|-----------|-|-|--------+
    | major   |b    |S[7:1]       |1|S|B    |C[5:0]     |N|0| minor  |
    +---------|-----|-------------|-|-|-----|-----------|-|-|--------+
    |0 0 0 0 1|b b b|s s s s s s s|1|S|B B B|C C C C C C|N|0|i i i i |
    +---------+-----+-------------+-+-+-----+-----------+-+-+--------+

    Branch on Compare/Bit Test Register-Immediate
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------|-----|-------------|-|-|-----|-----------|-|-|--------+
    | major   |b    |S[7:1]       |1|S|B    |U[5:0]     |N|1| minor  |
    +---------|-----|-------------|-|-|-----|-----------|-|-|--------+
    |0 0 0 0 1|b b b|s s s s s s s|1|S|B B B|U U U U U U|N|1|i i i i |
    +---------+-----+-------------+-+-+-----+-----------+-+-+--------+
*/
static int arc_decode_major01_body(DisasCtxt *ctx,
                                        unsigned minor, TCGv a, TCGv b, TCGv c)
{
    int ret;

    switch (minor) {
        case 0x00: {
            arc_decode_src(ctx, a);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BR(ctx, a, b, c, TCG_COND_EQ);
            break;
        }
        case 0x01: {
            arc_decode_src(ctx, a);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BR(ctx, a, b, c, TCG_COND_NE);
            break;
        }
        case 0x02: {
            arc_decode_src(ctx, a);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BR(ctx, a, b, c, TCG_COND_LT);
            break;
        }
        case 0x03: {
            arc_decode_src(ctx, a);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BR(ctx, a, b, c, TCG_COND_GE);
            break;
        }
        case 0x04: {
            arc_decode_src(ctx, a);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BR(ctx, a, b, c, TCG_COND_LTU);
            break;
        }
        case 0x05: {
            arc_decode_src(ctx, a);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BR(ctx, a, b, c, TCG_COND_GEU);
            break;
        }

        case 0x06 ... 0x0d: {
            ret = arc_gen_INVALID(ctx);
            break;
        }

        case 0x0E: {
            arc_decode_src(ctx, a);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BBIT0(ctx, a, b, c);
            break;
        }
        case 0x0F: {
            arc_decode_src(ctx, a);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BBIT1(ctx, a, b, c);
            break;
        }
        default: {
            ret = arc_gen_INVALID(ctx);
        }
    }

    return ret;
}

static int arc_decode_major01(DisasCtxt *ctx, uint32_t opcode)
{
    int ret;

    if (extract32(opcode, 16, 1) == 0) {

        if (extract32(opcode, 17, 1) == 0) {
            /*  Branch and Link Conditionally   */
            unsigned _Q = extract32(opcode, 0, 5);
            unsigned _N = extract32(opcode, 5, 1);
            unsigned _S = extract32(opcode, 6, 10);
            unsigned _s = extract32(opcode, 18, 9);
            TCGv s = tcg_const_local_i32(sextract32((_S << 9) | _s, 0, 19));

            ctx->opt.d = _N;

            ret = arc_gen_BL(ctx, s, _Q);

            tcg_temp_free_i32(s);
        } else {
            /*  Branch and Link Unconditional Far   */
            unsigned _T = extract32(opcode, 0, 4);
            unsigned _N = extract32(opcode, 5, 1);
            unsigned _S = extract32(opcode, 6, 10);
            unsigned _s = extract32(opcode, 18, 9);
            TCGv s = tcg_const_local_i32(sextract32((((_T << 10) | _S) << 9) |
                                                                    _s, 0, 23));

            ctx->opt.d = _N;

            ret = arc_gen_BL(ctx, s, ARC_COND_AL);

            tcg_temp_free_i32(s);
        }
    } else {
        unsigned minor;

        minor = extract32(opcode, 0, 4);

        if (extract32(opcode, 4, 1) == 0) {
            /*  Branch on Compare Register-Register */
            unsigned _N = extract32(opcode, 5, 1);
            unsigned _C = extract32(opcode, 6, 6);
            unsigned _B = extract32(opcode, 12, 3);
            unsigned _S = extract32(opcode, 15, 1);
            unsigned _s = extract32(opcode, 17, 7);
            unsigned _b = extract32(opcode, 24, 3);

            TCGv b = cpu_r[(_B << 3) | _b];
            TCGv c = cpu_r[_C];
            TCGv s = tcg_const_local_i32(sextract32((_S << 7) | _s, 0, 8));

            ctx->opt.d = _N;

            ret = arc_decode_major01_body(ctx, minor, b, c, s);

            tcg_temp_free_i32(s);
        } else {
            /*  Branch on Compare/Bit Test Register-Immediate   */
            unsigned _N = extract32(opcode, 5, 1);
            unsigned _u = extract32(opcode, 6, 6);
            unsigned _B = extract32(opcode, 12, 3);
            unsigned _S = extract32(opcode, 15, 1);
            unsigned _s = extract32(opcode, 17, 7);
            unsigned _b = extract32(opcode, 24, 3);

            TCGv b = cpu_r[(_B << 3) | _b];
            TCGv u = tcg_const_local_i32(_u);
            TCGv s = tcg_const_local_i32(sextract32((_S << 7) | _s, 0, 8));
            ctx->opt.d = _N;

            ret = arc_decode_major01_body(ctx, minor, b, u, s);

            tcg_temp_free_i32(s);
            tcg_temp_free_i32(u);
        }
    }

    return ret;
}


/*
    Load Register with Offset
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---------------+-+-----+-+---+---+-+------------+
    | major   | b   |S[7:0]         |S|B    |D|aa |zz |x| A[5:0]     |
    +---------+-----+---------------+-+-----+-+---+---+-+------------+
    |0 0 0 1 0|b b b|s s s s s s s s|S|B B B|D|a a|Z Z|X|A A A A A A |
    +---------+-----+---------------+-+-----+-+---+---+-+------------+
*/
static int arc_decode_major02(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;

    unsigned _A = extract32(opcode, 0, 6);
    unsigned _X = extract32(opcode, 6, 1);
    unsigned _ZZ = extract32(opcode, 7, 2);
    unsigned _AA = extract32(opcode, 9, 2);
    unsigned _DI = extract32(opcode, 11, 1);
    unsigned _B = extract32(opcode, 12, 3);
    unsigned _S = extract32(opcode, 15, 1);
    unsigned _s = extract32(opcode, 16, 8);
    unsigned _b = extract32(opcode, 24, 3);

    TCGv a = cpu_r[_A];
    TCGv b = cpu_r[(_B << 3) | _b];
    TCGv s = tcg_const_local_i32(sextract32((_S << 8) | _s, 0, 9));
    ctx->opt.zz = _ZZ;
    ctx->opt.x = _X;
    ctx->opt.aa = _AA;
    ctx->opt.di = _DI;

    arc_decode_src(ctx, b);
    ret = arc_gen_LD(ctx, a, b, s);

    tcg_temp_free_i32(s);

    return ret;
}

/*
    Store Register with Offset
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---------------+-+-----+-----------+-+---+---+--+
    | major   | b   | S[7:0]        |S| B   | C[5:0]    |D|aa |zz |R |
    +---------+-----+---------------+-+-----+-----------+-+---+---+--+
    |0 0 0 1 1|b b b|s s s s s s s s|S|B B B|C C C C C C|D|a a|Z Z|0 |
    +---------+-----+---------------+-+-----+-----------+-+---+---+--+
*/
static int arc_decode_major03(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _ZZ = extract32(opcode, 1, 2);
    unsigned _AA = extract32(opcode, 3, 2);
    unsigned _DI = extract32(opcode, 5, 1);
    unsigned _C = extract32(opcode, 6, 6);
    unsigned _B = extract32(opcode, 12, 3);
    unsigned _S = extract32(opcode, 15, 1);
    unsigned _s = extract32(opcode, 16, 8);
    unsigned _b = extract32(opcode, 24, 3);

    TCGv b = cpu_r[(_B << 3) | _b];
    TCGv c = cpu_r[_C];
    TCGv s = tcg_const_local_i32(sextract32((_S << 8) | _s, 0, 9));
    ctx->opt.zz = _ZZ;
    ctx->opt.aa = _AA;
    ctx->opt.di = _DI;

    arc_decode_src(ctx, b);
    arc_decode_src(ctx, c);
    ret = arc_gen_ST(ctx, c, b, s);

    tcg_temp_free_i32(s);
    return ret;
}

/*
    General Operations Register-Register
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    | major   | b   |P  | minor     |F| B   | C[5:0]    | A[5:0]     |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    |0 0 1 0 0|b b b|0 0|i i i i i i|F|B B B|C C C C C C|A A A A A A |
    +---------+-----+---+-----------+-+-----+-----------+------------+

    General Operations Register with Unsigned 6-bit Immediate
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    | major   | b   |P  | minor     |F| B   | U[5:0]    | A[5:0]     |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    |0 0 1 0 0|b b b|0 1|i i i i i i|F|B B B|U U U U U U|A A A A A A |
    +---------+-----+---+-----------+-+-----+-----------+------------+

    General Operations Register with Signed 12-bit Immediate
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    | major   | b   |P  | minor     |F| B   | S[5:0]    | S[11:6]    |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    |0 0 1 0 0|b b b|0 1|i i i i i i|F|B B B|s s s s s s|S S S S S S |
    +---------+-----+---+-----------+-+-----+-----------+------------+

    General Operations Conditional Register
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+
    | major   | b   |P  | minor     |F| B   | C[5:0]    |M|Q[4:0]    |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+
    |0 0 1 0 0|b b b|1 1|i i i i i i|F|B B B|C C C C C C|0|Q Q Q Q Q |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+

    General Operations Conditional Register with Unsigned 6-bit Immediate
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+
    | major   | b   |P  | minor     |F| B   | U[5:0]    |M|Q[4:0]    |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+
    |0 0 1 0 0|b b b|1 1|i i i i i i|F|B B B|U U U U U U|1|Q Q Q Q Q |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+

    Load Register-Register
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+---+-+---+-+-+-----+-----------+------------+
    | major   | b   |aa |1 1 0|zz |x|d|B    | C[5:0]    | A[5:0]     |
    +---------+-----+---+-----+---+-+-+-----+-----------+------------+
    |0 0 1 0 0|b b b|a a|1 1 0|Z Z|X|D|B B B|C C C C C C|A A A A A A |
    +---------+-----+---+---+-+---+-+-+-----+-----------+------------+

*/
static int arc_decode_major04_zop(DisasCtxt *ctx,
                                        unsigned _B, TCGv c)
{
    int ret = BS_NONE;

    switch (_B) {
        case 0x01: {
            ret = arc_gen_SLEEP(ctx, c);
            break;
        }
        case 0x02: {
            ret = arc_gen_SWI(ctx);
            break;
        }
        case 0x03: {
            ret = arc_gen_SYNC(ctx);
            break;
        }
        case 0x04: {
            ret = arc_gen_RTIE(ctx);
            break;
        }
        case 0x05: {
            ret = arc_gen_BRK(ctx);
            break;
        }
        default: {
            ret = arc_gen_INVALID(ctx);
        }
    }

    return ret;
}

static int arc_decode_major04_sop(DisasCtxt *ctx,
                                    unsigned _A, unsigned _B, TCGv b, TCGv c)
{
    int ret = BS_NONE;

    switch (_A) {
        case 0x00: {
            arc_decode_src(ctx, c);
            ret = arc_gen_ASL(ctx, b, c);
            break;
        }
        case 0x01: {
            arc_decode_src(ctx, c);
            ret = arc_gen_ASR(ctx, b, c);
            break;
        }
        case 0x02: {
            arc_decode_src(ctx, c);
            ret = arc_gen_LSR(ctx, b, c);
            break;
        }
        case 0x03: {
            arc_decode_src(ctx, c);
            ret = arc_gen_ROR(ctx, b, c);
            break;
        }
        case 0x04: {
            arc_decode_src(ctx, c);
            ret = arc_gen_RRC(ctx, b, c);
            break;
        }
        case 0x05: {
            arc_decode_src(ctx, c);
            ret = arc_gen_SEXB(ctx, b, c);
            break;
        }
        case 0x06: {
            arc_decode_src(ctx, c);
            ret = arc_gen_SEXW(ctx, b, c);
            break;
        }
        case 0x07: {
            arc_decode_src(ctx, c);
            ret = arc_gen_EXTB(ctx, b, c);
            break;
        }
        case 0x08: {
            arc_decode_src(ctx, c);
            ret = arc_gen_EXTW(ctx, b, c);
            break;
        }
        case 0x09: {
            arc_decode_src(ctx, c);
            ret = arc_gen_ABS(ctx, b, c);
            break;
        }
        case 0x0A: {
            arc_decode_src(ctx, c);
            ret = arc_gen_NOT(ctx, b, c);
            break;
        }
        case 0x0B: {
            arc_decode_src(ctx, c);
            ret = arc_gen_RLC(ctx, b, c);
            break;
        }
        case 0x0C: {
            arc_decode_src(ctx, c);
            ret = arc_gen_EX(ctx, b, c);
            break;
        }
        case 0x3F: {
            ret = arc_decode_major04_zop(ctx, _B, c);
            break;
        }
        default: {
            ret = arc_gen_INVALID(ctx);
        }
    }

    return ret;
}
static int arc_decode_major04_op(DisasCtxt *ctx,
                           uint32_t opcode, TCGv a, TCGv b, TCGv c, unsigned _Q)
{
    int ret = BS_NONE;
    unsigned _A = extract32(opcode, 0, 6);
    unsigned _B = extract32(opcode, 12, 3);
    unsigned _b = extract32(opcode, 24, 3);
    unsigned minor = extract32(opcode, 16, 6);
    TCGLabel *skip = gen_new_label();

    _B = (_B << 3) | _b;

    switch (minor) {
        case    0x00: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ADD(ctx, a, b, c);
            break;
        }
        case    0x01: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ADC(ctx, a, b, c);
            break;
        }
        case    0x02: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_SUB(ctx, a, b, c);
            break;
        }
        case    0x03: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_SBC(ctx, a, b, c);
            break;
        }
        case    0x04: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_AND(ctx, a, b, c);
            break;
        }
        case    0x05: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_OR(ctx, a, b, c);
            break;
        }
        case    0x06: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BIC(ctx, a, b, c);
            break;
        }
        case    0x07: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_XOR(ctx, a, b, c);
            break;
        }
        case    0x08: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_MAX(ctx, a, b, c);
            break;
        }
        case    0x09: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_MIN(ctx, a, b, c);
            break;
        }
        case    0x0A: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, c);
            ret = arc_gen_MOV(ctx, b, c);
            break;
        }
        case    0x0B: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_TST(ctx, b, c);
            break;
        }
        case    0x0C: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_CMP(ctx, b, c);
            break;
        }
        case    0x0D: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_CMP(ctx, c, b);
            break;
        }
        case    0x0E: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_RSUB(ctx, a, b, c);
            break;
        }
        case    0x0F: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BSET(ctx, a, b, c);
            break;
        }
        case    0x10: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BCLR(ctx, a, b, c);
            break;
        }
        case    0x11: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BTST(ctx, b, c);
            break;
        }
        case    0x12: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BXOR(ctx, a, b, c);
            break;
        }
        case    0x13: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_BMSK(ctx, a, b, c);
            break;
        }
        case    0x14: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ADD1(ctx, a, b, c);
            break;
        }
        case    0x15: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ADD2(ctx, a, b, c);
            break;
        }
        case    0x16: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ADD3(ctx, a, b, c);
            break;
        }
        case    0x17: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_SUB1(ctx, a, b, c);
            break;
        }
        case    0x18: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_SUB2(ctx, a, b, c);
            break;
        }
        case    0x19: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_SUB3(ctx, a, b, c);
            break;
        }
        case    0x1A: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_MPY(ctx, a, b, c);
            break;
        }
        case    0x1B: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_MPYH(ctx, a, b, c);
            break;
        }
        case    0x1C: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_MPYHU(ctx, a, b, c);
            break;
        }
        case    0x1D: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_MPYU(ctx, a, b, c);
            break;
        }
        case    0x20: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, c);
            ret = arc_gen_J(ctx, c, ARC_COND_AL);
            break;
        }
        case    0x21: {
            ctx->opt.d = 1;
            arc_decode_src(ctx, c);
            ret = arc_gen_J(ctx, c, ARC_COND_AL);
            break;
        }
        case    0x22: {
            arc_decode_src(ctx, c);
            ret = arc_gen_JL(ctx, c, _Q);
            break;
        }
        case    0x23: {
            ctx->opt.d = 1;
            arc_decode_src(ctx, c);
            ret = arc_gen_JL(ctx, c, _Q);
            break;
        }
        case    0x28: {
            ret = arc_gen_LPcc(ctx, c, _Q);
            break;
        }
        case    0x29: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, c);
            ret = arc_gen_FLAG(ctx, c);
            break;
        }
        case    0x2A: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, c);
            ret = arc_gen_LR(ctx, b, c);
            break;
        }
        case    0x2B: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_SR(ctx, b, c);
            break;
        }
        case    0x2F: {
            arc_gen_jump_ifnot(ctx, _Q, skip);
            ret = arc_decode_major04_sop(ctx, _A, _B, b, c);
            break;
        }
        case    0x30 ... 0x37: {
            unsigned _DI = extract32(opcode, 15, 1);
            unsigned _X = extract32(opcode, 16, 1);
            unsigned _ZZ = extract32(opcode, 17, 2);
            unsigned _AA = extract32(opcode, 22, 2);

            ctx->opt.zz = _ZZ;
            ctx->opt.x = _X;
            ctx->opt.aa = _AA;
            ctx->opt.di = _DI;

            arc_gen_jump_ifnot(ctx, _Q, skip);
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_LD(ctx, a, b, c);
            break;
        }
        default: {
            ret = arc_gen_INVALID(ctx);
        }
    }

    gen_set_label(skip);

    return ret;
}

static int arc_decode_major04(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _A = extract32(opcode, 0, 6);
    unsigned _B = extract32(opcode, 12, 3);
    unsigned _C = extract32(opcode, 6, 6);
    unsigned _F = extract32(opcode, 15, 1);
    unsigned _Q = extract32(opcode, 0, 5);
    unsigned _S = extract32(opcode, 0, 6);
    unsigned _b = extract32(opcode, 24, 3);
    unsigned _s = extract32(opcode, 6, 6);
    unsigned _u = extract32(opcode, 6, 6);

    _B = (_B << 3) | _b;

    switch (extract32(opcode, 22, 2)) {
        case 0x00: {
            /*  General Operations Register-Register    */

            TCGv a = cpu_r[_A];
            TCGv b = cpu_r[_B];
            TCGv c = cpu_r[_C];
            ctx->opt.f = _F;

            ret = arc_decode_major04_op(ctx, opcode, a, b, c, ARC_COND_AL);

            break;
        }
        case 0x01: {
            /*  General Operations Register with Unsigned 6-bit Immediate   */

            TCGv a = cpu_r[_A];
            TCGv b = cpu_r[_B];
            TCGv u = tcg_const_local_i32(_u);
            ctx->opt.f = _F;

            ret = arc_decode_major04_op(ctx, opcode, a, b, u, ARC_COND_AL);

            tcg_temp_free_i32(u);
            break;
        }
        case 0x02: {
            /*  General Operations Register with Signed 12-bit Immediate    */

            TCGv b = cpu_r[_B];
            TCGv s = tcg_const_local_i32(sextract32((_S << 6) | _s, 0, 12));
            ctx->opt.f = _F;

            ret = arc_decode_major04_op(ctx, opcode, b, b, s, ARC_COND_AL);

            tcg_temp_free_i32(s);
            break;
        }
        case 0x03: {
            if (extract32(opcode, 5, 1) == 0) {
                /*  General Operations Conditional Register */

                TCGv b = cpu_r[_B];
                TCGv c = cpu_r[_C];
                ctx->opt.f = _F;

                ret = arc_decode_major04_op(ctx, opcode, b, b, c, _Q);
            } else {
                TCGv b = cpu_r[_B];
                TCGv u = tcg_const_local_i32(_u);

                ctx->opt.f = _F;

                ret = arc_decode_major04_op(ctx, opcode, b, b, u, _Q);
            }

            break;
        }
    }
    return ret;
}

/*
    Extension ALU Operation, Register-Register
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    | major   |b    |P  | minor     |F|B    | C[5:0]    | A[5:0]     |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    |0 0 1 0 1|b b b|0 0|i i i i i i|F|B B B|C C C C C C|A A A A A A |
    +---------+-----+---+-----------+-+-----+-----------+------------+

    Extension ALU Operation, Register with Unsigned 6-bit Immediate
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    | major   |b    |P  | minor     |F|B    | U[5:0]    | A[5:0]     |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    |0 0 1 0 1|b b b|0 1|i i i i i i|F|B B B|U U U U U U|A A A A A A |
    +---------+-----+---+-----------+-+-----+-----------+------------+

    Extension ALU Operation, Register with Signed 12-bit Immediate
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    | major   |b    |P  | minor     |F|B    | S[5:0]    | S[11:6]    |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    |0 0 1 0 1|b b b|1 0|i i i i i i|F|B B B|s s s s s s|S S S S S S |
    +---------+-----+---+-----------+-+-----+-----------+------------+

    Extension ALU Operation, Conditional Register
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    | major   |b    |P  | minor     |F|B    | C[5:0]    |M| Q[5:0]   |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+
    |0 0 1 0 1|b b b|1 1|i i i i i i|F|B B B|C C C C C C|0|Q Q Q Q Q |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+

    Extension ALU Operation, Conditional Register with Unsigned 6-bit Immediate
    +----------------------------------------------------------------+
    |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+---+-----------+-+-----+-----------+------------+
    | major   |b    |P  | minor     |F|B    | U[5:0]    |M| Q[5:0]   |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+
    |0 0 1 0 1|b b b|1 1|i i i i i i|F|B B B|U U U U U U|1|Q Q Q Q Q |
    +---------+-----+---+-----------+-+-----+-----------+-+----------+

*/
static int arc_decode_major05_zop(DisasCtxt *ctx,
                                                            unsigned _B, TCGv c)
{
    int ret = BS_NONE;

    ret = arc_gen_INVALID(ctx);

    return ret;
}

static int arc_decode_major05_sop(DisasCtxt *ctx,
                                       unsigned _A, unsigned _B, TCGv b, TCGv c)
{
    int ret = BS_NONE;
    switch (_A) {
        case 0x00: {
            arc_decode_src(ctx, c);
            ret = arc_gen_SWAP(ctx, b, c);
            break;
        }
        case 0x01: {
            arc_decode_src(ctx, c);
            ret = arc_gen_NORM(ctx, b, c);
            break;
        }
        case 0x02: {
            arc_decode_src(ctx, c);
            ret = arc_gen_SAT16(ctx, b, c);
            break;
        }
        case 0x03: {
            arc_decode_src(ctx, c);
            ret = arc_gen_RND16(ctx, b, c);
            break;
        }
        case 0x04: {
            arc_decode_src(ctx, c);
            ret = arc_gen_ABSSW(ctx, b, c);
            break;
        }
        case 0x05: {
            arc_decode_src(ctx, c);
            ret = arc_gen_ABSS(ctx, b, c);
            break;
        }
        case 0x06: {
            arc_decode_src(ctx, c);
            ret = arc_gen_NEGSW(ctx, b, c);
            break;
        }
        case 0x07: {
            arc_decode_src(ctx, c);
            ret = arc_gen_NEGS(ctx, b, c);
            break;
        }
        case 0x08: {
            arc_decode_src(ctx, c);
            ret = arc_gen_NORMW(ctx, b, c);
            break;
        }
        case 0x3F: {
            ret = arc_decode_major05_zop(ctx, _B, c);
            break;
        }
        default: {
            ret = arc_gen_INVALID(ctx);
        }
    }
    return ret;
}
static int arc_decode_major05_op(DisasCtxt *ctx,
                                        uint32_t opcode, TCGv a, TCGv b, TCGv c)
{
    int ret = BS_NONE;
    unsigned _i = extract32(opcode, 16, 6);
    unsigned _A = extract32(opcode, 0, 6);
    unsigned _B = extract32(opcode, 12, 3);
    unsigned _b = extract32(opcode, 24, 3);

    _B = (_B << 3) | _b;

    switch (_i) {
        case 0x00: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ASLm(ctx, a, b, c);
            break;
        }
        case 0x01: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_LSRm(ctx, a, b, c);
            break;
        }
        case 0x02: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ASRm(ctx, a, b, c);
            break;
        }
        case 0x03: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_RORm(ctx, a, b, c);
            break;
        }
        case 0x04: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_MUL64(ctx, a, b, c);
            break;
        }
        case 0x05: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_MULU64(ctx, a, b, c);
            break;
        }
        case 0x06: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ADDS(ctx, a, b, c);
            break;
        }
        case 0x07: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_SUBS(ctx, a, b, c);
            break;
        }
        case 0x08: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_DIVAW(ctx, a, b, c);
            break;
        }
        case 0x0A: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ASLS(ctx, a, b, c);
            break;
        }
        case 0x0B: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ASRS(ctx, a, b, c);
            break;
        }
        case 0x28: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_ADDSDW(ctx, a, b, c);
            break;
        }
        case 0x29: {
            arc_decode_src(ctx, b);
            arc_decode_src(ctx, c);
            ret = arc_gen_SUBSDW(ctx, a, b, c);
            break;
        }
        case 0x2F: {
            ret = arc_decode_major05_sop(ctx, _A, _B, b, c);
            break;
        }
        default: {
            ret = arc_gen_INVALID(ctx);
        }
    }

    return ret;
}

static int arc_decode_major05(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _A = extract32(opcode, 0, 6);
    unsigned _B = extract32(opcode, 12, 3);
    unsigned _C = extract32(opcode, 6, 6);
    unsigned _F = extract32(opcode, 15, 1);
    unsigned _Q = extract32(opcode, 0, 5);
    unsigned _S = extract32(opcode, 0, 6);
    unsigned _b = extract32(opcode, 24, 3);
    unsigned _s = extract32(opcode, 6, 6);
    unsigned _u = extract32(opcode, 6, 6);

    _B = (_B << 3) | _b;

    switch (extract32(opcode, 22, 2)) {
        case 0x00: {
            /*
                Extension ALU Operation, Register-Register
            */
            TCGv a = cpu_r[_A];
            TCGv b = cpu_r[_B];
            TCGv c = cpu_r[_C];
            ctx->opt.f = _F;

            ret = arc_decode_major05_op(ctx, opcode, a, b, c);
            break;
        }
        case 0x01: {
            /*
                Extension ALU Operation, Register with Unsigned 6-bit Immediate
            */
            TCGv a = cpu_r[_A];
            TCGv b = cpu_r[_B];
            TCGv u = tcg_const_local_i32(_u);
            ctx->opt.f = _F;

            ret = arc_decode_major05_op(ctx, opcode, a, b, u);

            tcg_temp_free_i32(u);
            break;
        }
        case 0x02: {
            /*
                Extension ALU Operation, Register with Signed 12-bit Immediate
            */
            TCGv b = cpu_r[_B];
            TCGv s = tcg_const_local_i32(sextract32((_S << 6) | _s, 0, 12));
            ctx->opt.f = _F;

            ret = arc_decode_major05_op(ctx, opcode, b, b, s);

            tcg_temp_free_i32(s);
            break;
        }
        case 0x03: {
            ctx->opt.f = _F;
            TCGLabel *skip = gen_new_label();

            arc_gen_jump_ifnot(ctx, _Q, skip);

            if (extract32(opcode, 5, 1) == 0) {
                /*
                    Extension ALU Operation, Conditional Register
                */
                TCGv b = cpu_r[_B];
                TCGv c = cpu_r[_C];

                ret = arc_decode_major05_op(ctx, opcode, b, b, c);

            } else {
                /*
                    Extension ALU Operation, Conditional Register with
                    Unsigned 6-bit Immediate
                */
                TCGv b = cpu_r[_B];
                TCGv u = tcg_const_local_i32(_u);

                ret = arc_decode_major05_op(ctx, opcode, b, b, u);

                tcg_temp_free_i32(u);
            }

            gen_set_label(skip);
            break;
        }
    }
    return ret;
}

/*

    Load /Add Register-Register
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-----+---+------+
    | major   | b   | c   | i | a    |
    +---------+-----+-----+---+------+
    |0 1 1 0 0|b b b|c c c|i i|a a a |
    +---------+-----+-----+---+------+
*/
static int arc_decode_major0C(DisasCtxt *ctx, uint32_t opcode)
{
    unsigned _a = extract32(opcode, 0, 3);
    unsigned _c = extract32(opcode, 5, 3);
    unsigned _b = extract32(opcode, 8, 3);
    unsigned _i = extract32(opcode, 3, 2);
    int ret = BS_NONE;

    TCGv a = cpu_r[(_a / 4) * 12 + _a % 4];
    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv c = cpu_r[(_c / 4) * 12 + _c % 4];

    switch (_i) {
        case 0x00: {
            ctx->opt.zz = 0;
            ret = arc_gen_LD(ctx, a, b, c);
            break;
        }
        case 0x01: {
            ctx->opt.zz = 1;
            ret = arc_gen_LD(ctx, a, b, c);
            break;
        }
        case 0x02: {
            ctx->opt.zz = 2;
            ret = arc_gen_LD(ctx, a, b, c);
            break;
        }
        case 0x03: {
            ret = arc_gen_ADD(ctx, a, b, c);
            break;
        }
    }
    return ret;
}

/*

    Load /Add Register-Register
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-----+---+------+
    | major   | b   | c   | i | u    |
    +---------+-----+-----+---+------+
    |0 1 1 0 1|b b b|c c c|i i|u u u |
    +---------+-----+-----+---+------+
*/
static int arc_decode_major0D(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _u = extract32(opcode, 0, 3);
    unsigned _c = extract32(opcode, 5, 3);
    unsigned _b = extract32(opcode, 8, 3);
    unsigned _i = extract32(opcode, 3, 2);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv c = cpu_r[(_c / 4) * 12 + _c % 4];
    TCGv u = tcg_const_local_i32(_u);

    switch (_i) {
        case 0x00: {
            ret = arc_gen_ADD(ctx, c, b, u);
            break;
        }
        case 0x01: {
            ret = arc_gen_SUB(ctx, c, b, u);
            break;
        }
        case 0x02: {
            ret = arc_gen_ASLm(ctx, c, b, u);
            break;
        }
        case 0x03: {
            ret = arc_gen_ASRm(ctx, c, b, u);
            break;
        }
    }

    tcg_temp_free_i32(u);
    return ret;
}

/*

    Mov/Cmp/Add with High Register
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-----+---+------+
    | major   | b   | h   | i | H    |
    +---------+-----+-----+---+------+
    |0 1 1 1 0|b b b|h h h|i i|H H H |
    +---------+-----+-----+---+------+
*/
static int arc_decode_major0E(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _H = extract32(opcode, 0, 3);
    unsigned _h = extract32(opcode, 5, 3);
    unsigned _b = extract32(opcode, 8, 3);
    unsigned _i = extract32(opcode, 3, 2);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv h = cpu_r[(_H << 3) | _h];

    switch (_i) {
        case 0x00: {
            arc_decode_src(ctx, h);
            ret = arc_gen_ADD(ctx, b, b, h);
            break;
        }
        case 0x01: {
            arc_decode_src(ctx, h);
            ret = arc_gen_MOV(ctx, b, h);
            break;
        }
        case 0x02: {
            arc_decode_src(ctx, h);
            ret = arc_gen_CMP(ctx, b, h);
            break;
        }
        case 0x03: {
            ret = arc_gen_MOV(ctx, h, b);
            break;
        }
    }

    return ret;
}

/*

    General Operations, register-register
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-----+----------+
    | major   | b   | c   | i        |
    +---------+-----+-----+----------+
    |0 1 1 1 1|b b b|c c c|i i i i i |
    +---------+-----+-----+----------+

    General Operations, register
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-----+----------+
    | major   | b   | i   | 0        |
    +---------+-----+-----+----------+
    |0 1 1 1 1|b b b|i i i|0 0 0 0 0 |
    +---------+-----+-----+----------+

    General Operations, no register
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-----+----------+
    | major   | i   | 7   | 0        |
    +---------+-----+-----+----------+
    |0 1 1 1 1|i i i|1 1 1|0 0 0 0 0 |
    +---------+-----+-----+----------+
*/
static int arc_decode_major0F_zop(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _i = extract32(opcode, 8, 3);

    switch (_i) {
        case 0x00: {
            ret = arc_gen_NOP(ctx);
            break;
        }
        case 0x01: {
            ret = arc_gen_UNIMP(ctx);
            break;
        }
        case 0x04: {
            ret = arc_gen_J(ctx, cpu_blink, ARC_COND_EQ);
            break;
        }
        case 0x05: {
            ret = arc_gen_J(ctx, cpu_blink, ARC_COND_NE);
            break;
        }
        case 0x06: {
            ret = arc_gen_J(ctx, cpu_blink, ARC_COND_AL);
            break;
        }
        case 0x07: {
            ctx->opt.d = 1;
            ret = arc_gen_J(ctx, cpu_blink, ARC_COND_AL);
            break;
        }
        default: {
            ret = arc_gen_INVALID(ctx);
        }
    }

    return ret;
}

static int arc_decode_major0F_sop(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _i = extract32(opcode, 5, 3);
    unsigned _b = extract32(opcode, 8, 3);
    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    switch (_i) {
        case 0x00: {
            ret = arc_gen_J(ctx, b, ARC_COND_AL);
            break;
        }
        case 0x01: {
            ctx->opt.d = 1;
            ret = arc_gen_J(ctx, b, ARC_COND_AL);
            break;
        }
        case 0x02: {
            ret = arc_gen_JL(ctx, b, ARC_COND_AL);
            break;
        }
        case 0x03: {
            ctx->opt.d = 1;
            ret = arc_gen_JL(ctx, b, ARC_COND_AL);
            break;
        }
        case 0x06: {
            TCGLabel *skip = gen_new_label();

            arc_gen_jump_ifnot(ctx, ARC_COND_NE, skip);

            ret = arc_gen_SUB(ctx, b, b, b);

            gen_set_label(skip);

            break;
        }
        case 0x07: {
            ret = arc_decode_major0F_zop(ctx, opcode);
            break;
        }
        default: {
            ret = arc_gen_INVALID(ctx);
        }
    }

    return ret;
}

static int arc_decode_major0F(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _c = extract32(opcode, 5, 3);
    unsigned _b = extract32(opcode, 8, 3);
    unsigned _i = extract32(opcode, 0, 5);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv c = cpu_r[(_c / 4) * 12 + _c % 4];

    switch (_i) {
        case 0x00: {
            ret = arc_decode_major0F_sop(ctx, opcode);
            break;
        }
        case 0x02: {
            ret = arc_gen_SUB(ctx, b, b, c);
            break;
        }
        case 0x04: {
            ret = arc_gen_AND(ctx, b, b, c);
            break;
        }
        case 0x05: {
            ret = arc_gen_OR(ctx, b, b, c);
            break;
        }
        case 0x06: {
            ret = arc_gen_BIC(ctx, b, b, c);
            break;
        }
        case 0x07: {
            ret = arc_gen_XOR(ctx, b, b, c);
            break;
        }
        case 0x0B: {
            ret = arc_gen_TST(ctx, b, c);
            break;
        }
        case 0x0C: {
            ret = arc_gen_MUL64(ctx, b, b, c);
            break;
        }
        case 0x0D: {
            ret = arc_gen_SEXB(ctx, b, c);
            break;
        }
        case 0x0E: {
            ret = arc_gen_SEXW(ctx, b, c);
            break;
        }
        case 0x0F: {
            ret = arc_gen_EXTB(ctx, b, c);
            break;
        }
        case 0x10: {
            ret = arc_gen_EXTW(ctx, b, c);
            break;
        }
        case 0x11: {
            ret = arc_gen_ABS(ctx, b, c);
            break;
        }
        case 0x12: {
            ret = arc_gen_NOT(ctx, b, c);
            break;
        }
        case 0x13: {
            ret = arc_gen_NEG(ctx, b, c);
            break;
        }
        case 0x14: {
            ret = arc_gen_ADD1(ctx, b, b, c);
            break;
        }
        case 0x15: {
            ret = arc_gen_ADD2(ctx, b, b, c);
            break;
        }
        case 0x16: {
            ret = arc_gen_ADD3(ctx, b, b, c);
            break;
        }
        case 0x18: {
            ret = arc_gen_ASLm(ctx, b, b, c);
            break;
        }
        case 0x19: {
            ret = arc_gen_LSRm(ctx, b, b, c);
            break;
        }
        case 0x1A: {
            ret = arc_gen_ASRm(ctx, b, b, c);
            break;
        }
        case 0x1B: {
            ret = arc_gen_ASL(ctx, b, c);
            break;
        }
        case 0x1C: {
            ret = arc_gen_ASR(ctx, b, c);
            break;
        }
        case 0x1D: {
            ret = arc_gen_LSR(ctx, b, c);
            break;
        }
        case 0x1E: {
            unsigned _u = extract32(opcode, 5, 6);
            TCGv u = tcg_const_local_i32(_u);
            ret = arc_gen_TRAP(ctx, u);
            tcg_temp_free_i32(u);
            break;
        }
        case 0x1F: {
            ret = arc_gen_BRK(ctx);
            break;
        }
        default: {
            ret = arc_gen_INVALID(ctx);
        }
    }

    return ret;
}

/*
    Load/Store with Offset
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-----+----------+
    | major   | b   | c   | u        |
    +---------+-----+-----+----------+
    |I I I I I|b b b|c c c|u u u u u |
    +---------+-----+-----+----------+

*/
static int arc_decode_major10_16(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _i = extract32(opcode, 11, 5);
    unsigned _u = extract32(opcode, 0, 5);
    unsigned _c = extract32(opcode, 5, 3);
    unsigned _b = extract32(opcode, 8, 3);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv c = cpu_r[(_c / 4) * 12 + _c % 4];
    TCGv u;

    switch (_i) {
        case 0x10: {
            ctx->opt.zz = 0;
            u = tcg_const_local_i32(_u << 2);
            ret = arc_gen_LD(ctx, c, b, u);
            tcg_temp_free_i32(u);
            break;
        }
        case 0x11: {
            ctx->opt.zz = 1;
            u = tcg_const_local_i32(_u);
            ret = arc_gen_LD(ctx, c, b, u);
            tcg_temp_free_i32(u);
            break;
        }
        case 0x12: {
            ctx->opt.zz = 2;
            u = tcg_const_local_i32(_u << 1);
            ret = arc_gen_LD(ctx, c, b, u);
            tcg_temp_free_i32(u);
            break;
        }
        case 0x13: {
            ctx->opt.zz = 2;
            ctx->opt.x = 1;
            u = tcg_const_local_i32(_u << 1);
            ret = arc_gen_LD(ctx, c, b, u);
            tcg_temp_free_i32(u);
            break;
        }
        case 0x14: {
            ctx->opt.zz = 0;
            u = tcg_const_local_i32(_u << 2);
            ret = arc_gen_ST(ctx, c, b, u);
            tcg_temp_free_i32(u);
            break;
        }
        case 0x15: {
            ctx->opt.zz = 1;
            u = tcg_const_local_i32(_u);
            ret = arc_gen_ST(ctx, c, b, u);
            tcg_temp_free_i32(u);
            break;
        }
        case 0x16: {
            ctx->opt.zz = 2;
            u = tcg_const_local_i32(_u << 1);
            ret = arc_gen_ST(ctx, c, b, u);
            tcg_temp_free_i32(u);
            break;
        }
    }


    return ret;
}

/*
    Shift/Subtract/Bit Immediate
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-----+----------+
    | major   | b   | i   | u        |
    +---------+-----+-----+----------+
    |1 0 1 1 1|b b b|i i i|u u u u u |
    +---------+-----+-----+----------+

*/
static int arc_decode_major17(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _i = extract32(opcode, 5, 3);
    unsigned _u = extract32(opcode, 0, 5);
    unsigned _b = extract32(opcode, 8, 3);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv u = tcg_const_local_i32(_u);

    switch (_i) {
        case 0x00: {
            ret = arc_gen_ASLm(ctx, b, b, u);
            break;
        }
        case 0x01: {
            ret = arc_gen_LSRm(ctx, b, b, u);
            break;
        }
        case 0x02: {
            ret = arc_gen_ASRm(ctx, b, b, u);
            break;
        }
        case 0x03: {
            ret = arc_gen_SUB(ctx, b, b, u);
            break;
        }
        case 0x04: {
            ret = arc_gen_BSET(ctx, b, b, u);
            break;
        }
        case 0x05: {
            ret = arc_gen_BCLR(ctx, b, b, u);
            break;
        }
        case 0x06: {
            ret = arc_gen_BMSK(ctx, b, b, u);
            break;
        }
        case 0x07: {
            ret = arc_gen_BTST(ctx, b, u);
            break;
        }
    }

    tcg_temp_free_i32(u);

    return ret;
}

/*
    Stack Pointer Based Instructions
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-----+----------+
    | major   | b   | i   | u        |
    +---------+-----+-----+----------+
    |1 1 0 0 0|b b b|i i i|u u u u u |
    +---------+-----+-----+----------+

*/
static int arc_decode_major18(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _i = extract32(opcode, 5, 3);
    unsigned _u = extract32(opcode, 0, 5);
    unsigned _b = extract32(opcode, 8, 3);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv u = tcg_const_local_i32(_u << 2);

    switch (_i) {
        case 0x00: {
            ret = arc_gen_LD(ctx, b, cpu_sp, u);
            break;
        }
        case 0x01: {
            ctx->opt.zz = 1;
            ret = arc_gen_LD(ctx, b, cpu_sp, u);
            break;
        }
        case 0x02: {
            ret = arc_gen_ST(ctx, b, cpu_sp, u);
            break;
        }
        case 0x03: {
            ctx->opt.zz = 1;
            ret = arc_gen_ST(ctx, b, cpu_sp, u);
            break;
        }
        case 0x04: {
            ret = arc_gen_ADD(ctx, b, cpu_sp, u);
            break;
        }
        case 0x05: {
            switch (_b) {
                case 0x00: {
                    ret = arc_gen_ADD(ctx, cpu_sp, cpu_sp, u);
                    break;
                }
                case 0x01: {
                    ret = arc_gen_SUB(ctx, cpu_sp, cpu_sp, u);
                    break;
                }
                default: {
                    ret = arc_gen_INVALID(ctx);
                }
            }
            break;
        }
        case 0x06: {
            switch (_u) {
                case 0x01: {
                    ret = arc_gen_POP(ctx, b);
                    break;
                }
                case 0x11: {
                    ret = arc_gen_POP(ctx, cpu_blink);
                    break;
                }
                default: {
                    ret = arc_gen_INVALID(ctx);
                }
            }
            break;
        }
        case 0x07: {
            switch (_u) {
                case 0x01: {
                    ret = arc_gen_PUSH(ctx, b);
                    break;
                }
                case 0x11: {
                    ret = arc_gen_PUSH(ctx, cpu_blink);
                    break;
                }
                default: {
                    ret = arc_gen_INVALID(ctx);
                }
            }
            break;
        }
    }

    tcg_temp_free_i32(u);

    return ret;
}

/*
    Stack Pointer Based Instructions
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+---+------------------+
    | major   | i | s                |
    +---------+---+------------------+
    |1 1 0 0 1|i i|s s s s s s s s s |
    +---------+---+------------------+
*/
static int arc_decode_major19(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    signed _s = sextract32(opcode, 0, 9);
    unsigned _i = extract32(opcode, 9, 2);
    TCGv s;

    switch (_i) {
        case 0x00: {
            ctx->opt.zz = 0;
            s = tcg_const_local_i32(_s << 2);
            ret = arc_gen_LD(ctx, cpu_r[0], cpu_gp, s);
            break;
        }
        case 0x01: {
            ctx->opt.zz = 1;
            s = tcg_const_local_i32(_s);
            ret = arc_gen_LD(ctx, cpu_r[0], cpu_gp, s);
            break;
        }
        case 0x02: {
            ctx->opt.zz = 2;
            s = tcg_const_local_i32(_s << 1);
            ret = arc_gen_LD(ctx, cpu_r[0], cpu_gp, s);
            break;
        }
        case 0x03: {
            s = tcg_const_local_i32(_s << 2);
            ret = arc_gen_ADD(ctx, cpu_r[0], cpu_gp, s);
            break;
        }
    }

    tcg_temp_free_i32(s);
    return ret;
}

/*
    Load PCL-Relative
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+----------------+
    | major   | b   | s              |
    +---------+-----+----------------+
    |1 1 0 1 0|b b b|s s s s s s s s |
    +---------+-----+----------------+

*/
static int arc_decode_major1A(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _u = extract32(opcode, 0, 8);
    unsigned _b = extract32(opcode, 8, 3);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv u = tcg_const_local_i32(_u << 2);

    ret = arc_gen_LD(ctx, b, cpu_pcl, u);

    tcg_temp_free_i32(u);
    return ret;
}

/*
    Move Immediate
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+----------------+
    | major   | b   | u              |
    +---------+-----+----------------+
    |1 1 0 1 1|b b b|u u u u u u u u |
    +---------+-----+----------------+
*/
static int arc_decode_major1B(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _u = extract32(opcode, 0, 8);
    unsigned _b = extract32(opcode, 8, 3);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv u = tcg_const_local_i32(_u);

    ret = arc_gen_MOV(ctx, b, u);

    tcg_temp_free_i32(u);
    return ret;
}

/*
    ADD/CMP Immediate
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-+--------------+
    | major   | b   |i| u            |
    +---------+-----+-+--------------+
    |1 1 1 0 0|b b b|i|u u u u u u u |
    +---------+-----+-+--------------+
*/
static int arc_decode_major1C(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    unsigned _u = extract32(opcode, 0, 7);
    unsigned _b = extract32(opcode, 8, 3);
    unsigned _i = extract32(opcode, 7, 1);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv u = tcg_const_local_i32(_u);

    if (_i == 0) {
        ret = arc_gen_ADD(ctx, b, b, u);
    } else {
        ret = arc_gen_CMP(ctx, b, u);
    }

    tcg_temp_free_i32(u);
    return ret;
}

/*
    Branch on Compare Register with Zero
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+-----+-+--------------+
    | major   | b   |i| s            |
    +---------+-----+-+--------------+
    |1 1 1 0 1|b b b|i|s s s s s s s |
    +---------+-----+-+--------------+
*/
static int arc_decode_major1D(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    signed _s = sextract32(opcode, 0, 7);
    unsigned _b = extract32(opcode, 8, 3);
    unsigned _i = extract32(opcode, 7, 1);

    TCGv b = cpu_r[(_b / 4) * 12 + _b % 4];
    TCGv s = tcg_const_local_i32(_s);

    if (_i == 0) {
        ret = arc_gen_BR(ctx, b, ctx->zero, s, TCG_COND_EQ);
    } else {
        ret = arc_gen_BR(ctx, b, ctx->zero, s, TCG_COND_NE);
    }

    tcg_temp_free_i32(s);
    return ret;
}

/*
    Branch on Compare Register with Zero
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+---+------------------+
    | major   | b | s                |
    +---------+---+------------------+
    |1 1 1 1 0|i i|s s s s s s s s s |
    +---------+---+------------------+
*/
static int arc_decode_major1E(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    signed _s = sextract32(opcode, 0, 9);
    unsigned _i = extract32(opcode, 9, 2);
    TCGv s = tcg_const_local_i32(_s);

    switch (_i) {
        case 0x00: {
            ret = arc_gen_B(ctx, s, ARC_COND_AL);
            break;
        }
        case 0x01: {
            ret = arc_gen_B(ctx, s, ARC_COND_EQ);
            break;
        }
        case 0x02: {
            ret = arc_gen_B(ctx, s, ARC_COND_NE);
            break;
        }
        case 0x03: {
            signed _s = sextract32(opcode, 0, 6);
            unsigned _i = extract32(opcode, 5, 3);
            TCGv s = tcg_const_local_i32(_s);

            switch (_i) {
                case 0x00: {
                    ret = arc_gen_B(ctx, s, ARC_COND_GT);
                    break;
                }
                case 0x01: {
                    ret = arc_gen_B(ctx, s, ARC_COND_GE);
                    break;
                }
                case 0x02: {
                    ret = arc_gen_B(ctx, s, ARC_COND_LT);
                    break;
                }
                case 0x03: {
                    ret = arc_gen_B(ctx, s, ARC_COND_LE);
                    break;
                }
                case 0x04: {
                    ret = arc_gen_B(ctx, s, ARC_COND_HI);
                    break;
                }
                case 0x05: {
                    ret = arc_gen_B(ctx, s, ARC_COND_HS);
                    break;
                }
                case 0x06: {
                    ret = arc_gen_B(ctx, s, ARC_COND_LO);
                    break;
                }
                case 0x07: {
                    ret = arc_gen_B(ctx, s, ARC_COND_LS);
                    break;
                }
            }

            tcg_temp_free_i32(s);
            break;
        }
    }

    tcg_temp_free_i32(s);
    return ret;
}

/*
    Branch on Compare Register with Zero
    +--------------------------------+
    |1 1 1 1 1 1 0 0 0 0 0 0 0 0 0 0 |
    |5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 |
    +---------+----------------------+
    | major   | s                    |
    +---------+----------------------+
    |1 1 1 1 1|s s s s s s s s s s s |
    +---------+----------------------+
*/
static int arc_decode_major1F(DisasCtxt *ctx, uint32_t opcode)
{
    int ret = BS_NONE;
    signed _s = sextract32(opcode, 0, 11);

    TCGv s = tcg_const_local_i32(_s);

    ret = arc_gen_BL(ctx, s, ARC_COND_AL);

    tcg_temp_free_i32(s);
    return ret;
}

int arc_decode(DisasCtxt *ctx)
{
    unsigned curr_major;
    uint32_t curr_opcode;

    unsigned next_major;
    uint32_t next_opcode;

    decode_f    decode[32] = {
        [0x00]  = arc_decode_major00,
        [0x01]  = arc_decode_major01,
        [0x02]  = arc_decode_major02,
        [0x03]  = arc_decode_major03,
        [0x04]  = arc_decode_major04,
        [0x05]  = arc_decode_major05,

        [0x06]  = arc_decode_invalid,
        [0x07]  = arc_decode_invalid,
        [0x08]  = arc_decode_invalid,
        [0x09]  = arc_decode_invalid,
        [0x0a]  = arc_decode_invalid,
        [0x0b]  = arc_decode_invalid,

        [0x0c]  = arc_decode_major0C,
        [0x0d]  = arc_decode_major0D,
        [0x0e]  = arc_decode_major0E,
        [0x0f]  = arc_decode_major0F,

        [0x10]  = arc_decode_major10_16,
        [0x11]  = arc_decode_major10_16,
        [0x12]  = arc_decode_major10_16,
        [0x13]  = arc_decode_major10_16,
        [0x14]  = arc_decode_major10_16,
        [0x15]  = arc_decode_major10_16,
        [0x16]  = arc_decode_major10_16,

        [0x17]  = arc_decode_major17,
        [0x18]  = arc_decode_major18,
        [0x19]  = arc_decode_major19,
        [0x1a]  = arc_decode_major1A,
        [0x1b]  = arc_decode_major1B,
        [0x1c]  = arc_decode_major1C,
        [0x1d]  = arc_decode_major1D,
        [0x1e]  = arc_decode_major1E,
        [0x1f]  = arc_decode_major1F,

    };
    memset(&ctx->opt, 0, sizeof(ctx->opt));

    /*
        1. get current opcode
        2. set npc
    */
    curr_opcode = cpu_ldl_code(ctx->env, ctx->cpc);
    curr_major  = extract32(curr_opcode, 11, 5);

    if (curr_major <= 5) {
        curr_opcode = (curr_opcode << 16) | (curr_opcode >> 16);
        ctx->npc = ctx->cpc + 4;
    } else {
        ctx->npc = ctx->cpc + 2;
    }

    /*
        3. set dpc (used by BL)
    */
    next_opcode = cpu_ldl_code(ctx->env, ctx->npc);
    next_major  = extract32(next_opcode, 11, 5);
    if (next_major <= 5) {
        ctx->dpc = ctx->npc + 4;
    } else {
        ctx->dpc = ctx->npc + 2;
    }

    ctx->pcl = ctx->cpc & 0xfffffffc;

    return  decode[curr_major](ctx, curr_opcode);
}
