#include <string.h>
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "disas/dis-asm.h"

typedef disassemble_info DisasContext;

/* 16-bit */

typedef struct {
    int rs;
    int rt;
} arg_16_decode0;

typedef struct {
    int offset;
    int rs;
} arg_16_decode1;

typedef struct {
    int rd;
    int rt;
    int sa;
} arg_16_decode2;

typedef struct {
    int rd;
    int rs;
    int rt;
} arg_16_decode3;

typedef struct {
    int base;
    int offset;
    int rt;
} arg_16_decode4;

typedef struct {
    int code;
} arg_16_decode5;

typedef struct {
    int offset;
    int rt;
} arg_16_decode6;

typedef struct {
    int rs;
} arg_16_decode7;

typedef struct {
    int imm;
    int rd;
} arg_16_decode8;

typedef struct {
    int offset;
    int reglist;
} arg_16_decode9;

typedef struct {
    int imm;
} arg_16_decode10;

typedef struct {
    int imm;
    int rd;
    int rs;
} arg_16_decode11;

typedef struct {
    int offset;
} arg_16_decode12;

typedef struct {
    int rd;
    int rs;
} arg_16_decode13;

typedef struct {
    int rd;
    int rs;
    int rt;
} arg_rd_rt_rs;

/* 32-bit */

typedef struct {
    int base;
    int offset;
    int reglist;
} arg_decode0;

typedef struct {
    int base;
    int offset;
    int rd;
} arg_decode1;

typedef struct {
    int base;
    int offset;
    int rs1;
} arg_decode2;

typedef struct {
    int rd;
    int rs;
    int rt;
} arg_decode3;

typedef struct {
    int imm;
    int rs;
    int rt;
} arg_decode4;

typedef struct {
} arg_decode5;

typedef struct {
    int ft;
    int offset;
} arg_decode6;

typedef struct {
    int ct;
    int offset;
} arg_decode7;

typedef struct {
    int code;
    int rs;
    int rt;
} arg_decode8;

typedef struct {
    int code;
} arg_decode9;

typedef struct {
    int offset;
    int rt;
} arg_decode10;

typedef struct {
    int offset;
    int rs;
    int rt;
} arg_decode11;

typedef struct {
    int fd;
    int fmt;
    int fs;
    int ft;
} arg_decode12;

typedef struct {
    int imm;
    int rt;
} arg_decode13;

typedef struct {
    int base;
    int offset;
    int op;
} arg_decode14;

typedef struct {
    int base;
    int offset;
    int rt;
} arg_decode15;

typedef struct {
    int offset;
} arg_decode16;

typedef struct {
    int rs;
    int rt;
} arg_decode17;

typedef struct {
    int rs;
} arg_decode18;

typedef struct {
    int bp;
    int rd;
    int rs;
    int rt;
} arg_decode19;

typedef struct {
    int lsb;
    int msbd;
    int rs;
    int rt;
} arg_decode20;

typedef struct {
    int fmt;
    int fs;
    int ft;
} arg_decode21;

typedef struct {
    int fd;
    int fmt;
    int fs;
} arg_decode22;

typedef struct {
    int base;
    int hint;
    int offset;
} arg_decode23;

typedef struct {
    int rs;
    int rt;
    int sel;
} arg_decode24;

typedef struct {
    int rs;
    int rt;
    int sa;
} arg_decode25;

typedef struct {
    int fs;
    int rt;
} arg_decode26;

typedef struct {
    int impl;
    int rt;
} arg_decode27;

typedef struct {
    int stype;
} arg_decode28;

typedef struct {
    int base;
    int rd;
    int rt;
} arg_decode29;

typedef struct {
    int base;
    int ft;
    int offset;
} arg_decode30;

typedef struct {
    int base;
    int offset;
} arg_decode31;

typedef struct {
    int condn;
    int fd;
    int fs;
    int ft;
} arg_decode32;

typedef struct {
    int rd;
    int rt;
} arg_decode33;

typedef struct {
    int cofun;
} arg_decode34;

typedef struct {
    int rd;
    int rs;
    int rt;
    int sa;
} arg_decode35;

typedef struct {
    int offset;
    int rs;
} arg_decode36;

/* 16-bit */

typedef arg_16_decode0 arg_AND16;
typedef arg_16_decode0 arg_OR16;
typedef arg_16_decode0 arg_NOT16;
typedef arg_16_decode0 arg_XOR16;

static bool trans_AND16(DisasContext *ctx, arg_AND16 *a);
static bool trans_OR16(DisasContext *ctx, arg_OR16 *a);
static bool trans_NOT16(DisasContext *ctx, arg_NOT16 *a);
static bool trans_XOR16(DisasContext *ctx, arg_XOR16 *a);

typedef arg_16_decode1 arg_BEQZC16;
typedef arg_16_decode1 arg_BNEZC16;

static bool trans_BEQZC16(DisasContext *ctx, arg_BEQZC16 *a);
static bool trans_BNEZC16(DisasContext *ctx, arg_BNEZC16 *a);

typedef arg_16_decode2 arg_SLL16;
typedef arg_16_decode2 arg_SRL16;

static bool trans_SLL16(DisasContext *ctx, arg_SLL16 *a);
static bool trans_SRL16(DisasContext *ctx, arg_SRL16 *a);

typedef arg_16_decode3 arg_ADDU16;
typedef arg_16_decode3 arg_SUBU16;

static bool trans_ADDU16(DisasContext *ctx, arg_ADDU16 *a);
static bool trans_SUBU16(DisasContext *ctx, arg_SUBU16 *a);

typedef arg_16_decode4 arg_SB16;
typedef arg_16_decode4 arg_SH16;
typedef arg_16_decode4 arg_SW16;
typedef arg_16_decode4 arg_LBU16;
typedef arg_16_decode4 arg_LHU16;
typedef arg_16_decode4 arg_LW16;

static bool trans_SB16(DisasContext *ctx, arg_SB16 *a);
static bool trans_SH16(DisasContext *ctx, arg_SH16 *a);
static bool trans_SW16(DisasContext *ctx, arg_SW16 *a);
static bool trans_LBU16(DisasContext *ctx, arg_LBU16 *a);
static bool trans_LHU16(DisasContext *ctx, arg_LHU16 *a);
static bool trans_LW16(DisasContext *ctx, arg_LW16 *a);

typedef arg_16_decode5 arg_BREAK16;
typedef arg_16_decode5 arg_SDBBP16;

static bool trans_BREAK16(DisasContext *ctx, arg_BREAK16 *a);
static bool trans_SDBBP16(DisasContext *ctx, arg_SDBBP16 *a);

typedef arg_16_decode6 arg_LWGP;
typedef arg_16_decode6 arg_LWSP;
typedef arg_16_decode6 arg_SWSP;

static bool trans_LWGP(DisasContext *ctx, arg_LWGP *a);
static bool trans_LWSP(DisasContext *ctx, arg_LWSP *a);
static bool trans_SWSP(DisasContext *ctx, arg_SWSP *a);

typedef arg_16_decode7 arg_JALRC16;
typedef arg_16_decode7 arg_JRC16;

static bool trans_JALRC16(DisasContext *ctx, arg_JALRC16 *a);
static bool trans_JRC16(DisasContext *ctx, arg_JRC16 *a);

typedef arg_16_decode8 arg_ADDIUR1SP;
typedef arg_16_decode8 arg_ADDIUS5;
typedef arg_16_decode8 arg_LI16;

static bool trans_ADDIUR1SP(DisasContext *ctx, arg_ADDIUR1SP *a);
static bool trans_ADDIUS5(DisasContext *ctx, arg_ADDIUS5 *a);
static bool trans_LI16(DisasContext *ctx, arg_LI16 *a);

typedef arg_16_decode9 arg_LWM16;
typedef arg_16_decode9 arg_SWM16;

static bool trans_LWM16(DisasContext *ctx, arg_LWM16 *a);
static bool trans_SWM16(DisasContext *ctx, arg_SWM16 *a);

typedef arg_16_decode10 arg_ADDIUSP;
typedef arg_16_decode10 arg_JRCADDIUSP;

static bool trans_ADDIUSP(DisasContext *ctx, arg_ADDIUSP *a);
static bool trans_JRCADDIUSP(DisasContext *ctx, arg_JRCADDIUSP *a);

typedef arg_16_decode11 arg_ADDIUR2;
typedef arg_16_decode11 arg_ANDI16;

static bool trans_ADDIUR2(DisasContext *ctx, arg_ADDIUR2 *a);
static bool trans_ANDI16(DisasContext *ctx, arg_ANDI16 *a);

typedef arg_16_decode12 arg_BC16;

static bool trans_BC16(DisasContext *ctx, arg_BC16 *a);

typedef arg_16_decode13 arg_MOVE16;

static bool trans_MOVE16(DisasContext *ctx, arg_MOVE16 *a);

typedef arg_rd_rt_rs arg_MOVEP;

static bool trans_MOVEP(DisasContext *ctx, arg_MOVEP *a);


/* 16-bit */

static void decode_extract_decode_16_Fmt_0(DisasContext *ctx, arg_16_decode0 *a,
    uint16_t insn)
{
    a->rt = extract32(insn, 7, 3);
    a->rs = extract32(insn, 4, 3);
}

static void decode_extract_decode_16_Fmt_1(DisasContext *ctx, arg_16_decode1 *a,
    uint16_t insn)
{
    a->rs = extract32(insn, 7, 3);
    a->offset = sextract32(insn, 0, 7);
}

static void decode_extract_decode_16_Fmt_2(DisasContext *ctx, arg_16_decode2 *a,
    uint16_t insn)
{
    a->rd = extract32(insn, 7, 3);
    a->rt = extract32(insn, 4, 3);
    a->sa = extract32(insn, 1, 3);
}

static void decode_extract_decode_16_Fmt_3(DisasContext *ctx, arg_16_decode3 *a,
    uint16_t insn)
{
    a->rt = extract32(insn, 4, 3);
    a->rd = extract32(insn, 1, 3);
    a->rs = extract32(insn, 7, 3);
}

static void decode_extract_decode_16_Fmt_4(DisasContext *ctx, arg_16_decode4 *a,
    uint16_t insn)
{
    a->rt = extract32(insn, 7, 3);
    a->base = extract32(insn, 4, 3);
    a->offset = extract32(insn, 0, 4);
}

static void decode_extract_decode_16_Fmt_5(DisasContext *ctx, arg_16_decode5 *a,
    uint16_t insn)
{
    a->code = extract32(insn, 6, 4);
}

static void decode_extract_decode_16_Fmt_6(DisasContext *ctx, arg_16_decode6 *a,
    uint16_t insn)
{
    a->rt = extract32(insn, 7, 3);
    a->offset = sextract32(insn, 0, 7);
}

static void decode_extract_decode_16_Fmt_7(DisasContext *ctx, arg_16_decode6 *a,
    uint16_t insn)
{
    a->rt = extract32(insn, 5, 5);
    a->offset = sextract32(insn, 0, 5);
}

static void decode_extract_decode_16_Fmt_8(DisasContext *ctx, arg_16_decode7 *a,
    uint16_t insn)
{
    a->rs = extract32(insn, 5, 5);
}

static void decode_extract_decode_16_Fmt_9(DisasContext *ctx, arg_16_decode8 *a,
    uint16_t insn)
{
    a->rd = extract32(insn, 7, 3);
    a->imm = extract32(insn, 1, 6);
}

static void decode_extract_decode_16_Fmt_10(DisasContext *ctx,
    arg_16_decode8 *a, uint16_t insn)
{
    a->rd = extract32(insn, 5, 5);
    a->imm = extract32(insn, 1, 4);
}

static void decode_extract_decode_16_Fmt_11(DisasContext *ctx,
    arg_16_decode9 *a, uint16_t insn)
{
    a->reglist = extract32(insn, 8, 2);
    a->offset = extract32(insn, 4, 4);
}

static void decode_extract_decode_16_Fmt_12(DisasContext *ctx,
    arg_16_decode10 *a, uint16_t insn)
{
    a->imm = extract32(insn, 1, 9);
}

static void decode_extract_decode_16_Fmt_13(DisasContext *ctx,
    arg_16_decode10 *a, uint16_t insn)
{
    a->imm = extract32(insn, 5, 5);
}

static void decode_extract_decode_16_Fmt_14(DisasContext *ctx,
    arg_16_decode11 *a, uint16_t insn)
{
    a->rd = extract32(insn, 7, 3);
    a->imm = extract32(insn, 1, 3);
    a->rs = extract32(insn, 4, 3);
}

static void decode_extract_decode_16_Fmt_15(DisasContext *ctx,
    arg_16_decode11 *a, uint16_t insn)
{
    a->rd = extract32(insn, 7, 3);
    a->imm = extract32(insn, 0, 4);
    a->rs = extract32(insn, 4, 3);
}

static void decode_extract_decode_16_Fmt_16(DisasContext *ctx,
    arg_16_decode12 *a, uint16_t insn)
{
    a->offset = sextract32(insn, 0, 10);
}

static void decode_extract_decode_16_Fmt_17(DisasContext *ctx,
    arg_16_decode8 *a, uint16_t insn)
{
    a->rd = extract32(insn, 7, 3);
    a->imm = extract32(insn, 0, 7);
}

static void decode_extract_decode_16_Fmt_18(DisasContext *ctx,
    arg_16_decode13 *a, uint16_t insn)
{
    a->rd = extract32(insn, 5, 5);
    a->rs = extract32(insn, 0, 5);
}

static void decode_extract_decode_16_Fmt_19(DisasContext *ctx, arg_rd_rt_rs *a,
    uint16_t insn)
{
    a->rd = extract32(insn, 7, 3);
    a->rt = extract32(insn, 4, 3);
    a->rs = deposit32(extract32(insn, 3, 1), 1, 31, extract32(insn, 0, 2));
}

static int decode16(DisasContext *ctx, uint16_t insn)
{
    union {
        arg_16_decode0 f_decode0;
        arg_16_decode1 f_decode1;
        arg_16_decode10 f_decode10;
        arg_16_decode11 f_decode11;
        arg_16_decode12 f_decode12;
        arg_16_decode13 f_decode13;
        arg_16_decode2 f_decode2;
        arg_16_decode3 f_decode3;
        arg_16_decode4 f_decode4;
        arg_16_decode5 f_decode5;
        arg_16_decode6 f_decode6;
        arg_16_decode7 f_decode7;
        arg_16_decode8 f_decode8;
        arg_16_decode9 f_decode9;
        arg_rd_rt_rs f_rd_rt_rs;
    } u;

    switch ((insn >> 10) & 0b111111) {
    case 0b1:  /* 000001.. ........ */
        decode_extract_decode_16_Fmt_3(ctx, &u.f_decode3, insn);
        switch (insn & 0b1) {
        case 0b0:  /* 000001.. .......0 */
            if (trans_ADDU16(ctx, &u.f_decode3)) {
                return 2;
            }
            return 0;
        case 0b1:  /* 000001.. .......1 */
            if (trans_SUBU16(ctx, &u.f_decode3)) {
                return 2;
            }
            return 0;
        }
        return 0;
    case 0b10:  /* 000010.. ........ */
        decode_extract_decode_16_Fmt_4(ctx, &u.f_decode4, insn);
        if (trans_LBU16(ctx, &u.f_decode4)) {
            return 2;
        }
        return 0;
    case 0b11:  /* 000011.. ........ */
        decode_extract_decode_16_Fmt_18(ctx, &u.f_decode13, insn);
        if (trans_MOVE16(ctx, &u.f_decode13)) {
            return 2;
        }
        return 0;
    case 0b1001:  /* 001001.. ........ */
        decode_extract_decode_16_Fmt_2(ctx, &u.f_decode2, insn);
        switch (insn & 0b1) {
        case 0b0:  /* 001001.. .......0 */
            if (trans_SLL16(ctx, &u.f_decode2)) {
                return 2;
            }
            return 0;
        case 0b1:  /* 001001.. .......1 */
            if (trans_SRL16(ctx, &u.f_decode2)) {
                return 2;
            }
            return 0;
        }
        return 0;
    case 0b1010:  /* 001010.. ........ */
        decode_extract_decode_16_Fmt_4(ctx, &u.f_decode4, insn);
        if (trans_LHU16(ctx, &u.f_decode4)) {
            return 2;
        }
        return 0;
    case 0b1011:  /* 001011.. ........ */
        decode_extract_decode_16_Fmt_15(ctx, &u.f_decode11, insn);
        if (trans_ANDI16(ctx, &u.f_decode11)) {
            return 2;
        }
        return 0;
    case 0b10001:  /* 010001.. ........ */
        switch ((insn >> 2) & 0b1) {
        case 0b0:  /* 010001.. .....0.. */
            switch (insn & 0b1011) {
            case 0b0:  /* 010001.. ....0000 */
                decode_extract_decode_16_Fmt_0(ctx, &u.f_decode0, insn);
                if (trans_NOT16(ctx, &u.f_decode0)) {
                    return 2;
                }
                return 0;
            case 0b1:  /* 010001.. ....0001 */
                decode_extract_decode_16_Fmt_0(ctx, &u.f_decode0, insn);
                if (trans_AND16(ctx, &u.f_decode0)) {
                    return 2;
                }
                return 0;
            case 0b10:  /* 010001.. ....0010 */
                decode_extract_decode_16_Fmt_11(ctx, &u.f_decode9, insn);
                if (trans_LWM16(ctx, &u.f_decode9)) {
                    return 2;
                }
                return 0;
            case 0b11:  /* 010001.. ....0011 */
                switch ((insn >> 4) & 0b1) {
                case 0b0:  /* 010001.. ...00011 */
                    decode_extract_decode_16_Fmt_8(ctx, &u.f_decode7, insn);
                    if (trans_JRC16(ctx, &u.f_decode7)) {
                        return 2;
                    }
                    return 0;
                case 0b1:  /* 010001.. ...10011 */
                    decode_extract_decode_16_Fmt_13(ctx, &u.f_decode10, insn);
                    if (trans_JRCADDIUSP(ctx, &u.f_decode10)) {
                        return 2;
                    }
                    return 0;
                }
                return 0;
            case 0b1000:  /* 010001.. ....1000 */
                decode_extract_decode_16_Fmt_0(ctx, &u.f_decode0, insn);
                if (trans_XOR16(ctx, &u.f_decode0)) {
                    return 2;
                }
                return 0;
            case 0b1001:  /* 010001.. ....1001 */
                decode_extract_decode_16_Fmt_0(ctx, &u.f_decode0, insn);
                if (trans_OR16(ctx, &u.f_decode0)) {
                    return 2;
                }
                return 0;
            case 0b1010:  /* 010001.. ....1010 */
                decode_extract_decode_16_Fmt_11(ctx, &u.f_decode9, insn);
                if (trans_SWM16(ctx, &u.f_decode9)) {
                    return 2;
                }
                return 0;
            case 0b1011:  /* 010001.. ....1011 */
                switch ((insn >> 4) & 0b1) {
                case 0b0:  /* 010001.. ...01011 */
                    decode_extract_decode_16_Fmt_8(ctx, &u.f_decode7, insn);
                    if (trans_JALRC16(ctx, &u.f_decode7)) {
                        return 2;
                    }
                    return 0;
                case 0b1:  /* 010001.. ...11011 */
                    decode_extract_decode_16_Fmt_5(ctx, &u.f_decode5, insn);
                    switch ((insn >> 5) & 0b1) {
                    case 0b0:  /* 010001.. ..011011 */
                        if (trans_BREAK16(ctx, &u.f_decode5)) {
                            return 2;
                        }
                        return 0;
                    case 0b1:  /* 010001.. ..111011 */
                        if (trans_SDBBP16(ctx, &u.f_decode5)) {
                            return 2;
                        }
                        return 0;
                    }
                    return 0;
                }
                return 0;
            }
            return 0;
        case 0b1:  /* 010001.. .....1.. */
            decode_extract_decode_16_Fmt_19(ctx, &u.f_rd_rt_rs, insn);
            if (trans_MOVEP(ctx, &u.f_rd_rt_rs)) {
                return 2;
            }
            return 0;
        }
        return 0;
    case 0b10010:  /* 010010.. ........ */
        decode_extract_decode_16_Fmt_7(ctx, &u.f_decode6, insn);
        if (trans_LWSP(ctx, &u.f_decode6)) {
            return 2;
        }
        return 0;
    case 0b10011:  /* 010011.. ........ */
        switch (insn & 0b1) {
        case 0b0:  /* 010011.. .......0 */
            decode_extract_decode_16_Fmt_10(ctx, &u.f_decode8, insn);
            if (trans_ADDIUS5(ctx, &u.f_decode8)) {
                return 2;
            }
            return 0;
        case 0b1:  /* 010011.. .......1 */
            decode_extract_decode_16_Fmt_12(ctx, &u.f_decode10, insn);
            if (trans_ADDIUSP(ctx, &u.f_decode10)) {
                return 2;
            }
            return 0;
        }
        return 0;
    case 0b11001:  /* 011001.. ........ */
        decode_extract_decode_16_Fmt_6(ctx, &u.f_decode6, insn);
        if (trans_LWGP(ctx, &u.f_decode6)) {
            return 2;
        }
        return 0;
    case 0b11010:  /* 011010.. ........ */
        decode_extract_decode_16_Fmt_4(ctx, &u.f_decode4, insn);
        if (trans_LW16(ctx, &u.f_decode4)) {
            return 2;
        }
        return 0;
    case 0b11011:  /* 011011.. ........ */
        switch (insn & 0b1) {
        case 0b0:  /* 011011.. .......0 */
            decode_extract_decode_16_Fmt_14(ctx, &u.f_decode11, insn);
            if (trans_ADDIUR2(ctx, &u.f_decode11)) {
                return 2;
            }
            return 0;
        case 0b1:  /* 011011.. .......1 */
            decode_extract_decode_16_Fmt_9(ctx, &u.f_decode8, insn);
            if (trans_ADDIUR1SP(ctx, &u.f_decode8)) {
                return 2;
            }
            return 0;
        }
        return 0;
    case 0b100010:  /* 100010.. ........ */
        decode_extract_decode_16_Fmt_4(ctx, &u.f_decode4, insn);
        if (trans_SB16(ctx, &u.f_decode4)) {
            return 2;
        }
        return 0;
    case 0b100011:  /* 100011.. ........ */
        decode_extract_decode_16_Fmt_1(ctx, &u.f_decode1, insn);
        if (trans_BEQZC16(ctx, &u.f_decode1)) {
            return 2;
        }
        return 0;
    case 0b101010:  /* 101010.. ........ */
        decode_extract_decode_16_Fmt_4(ctx, &u.f_decode4, insn);
        if (trans_SH16(ctx, &u.f_decode4)) {
            return 2;
        }
        return 0;
    case 0b101011:  /* 101011.. ........ */
        decode_extract_decode_16_Fmt_1(ctx, &u.f_decode1, insn);
        if (trans_BNEZC16(ctx, &u.f_decode1)) {
            return 2;
        }
        return 0;
    case 0b110010:  /* 110010.. ........ */
        decode_extract_decode_16_Fmt_7(ctx, &u.f_decode6, insn);
        if (trans_SWSP(ctx, &u.f_decode6)) {
            return 2;
        }
        return 0;
    case 0b110011:  /* 110011.. ........ */
        decode_extract_decode_16_Fmt_16(ctx, &u.f_decode12, insn);
        if (trans_BC16(ctx, &u.f_decode12)) {
            return 2;
        }
        return 0;
    case 0b111010:  /* 111010.. ........ */
        decode_extract_decode_16_Fmt_4(ctx, &u.f_decode4, insn);
        if (trans_SW16(ctx, &u.f_decode4)) {
            return 2;
        }
        return 0;
    case 0b111011:  /* 111011.. ........ */
        decode_extract_decode_16_Fmt_17(ctx, &u.f_decode8, insn);
        if (trans_LI16(ctx, &u.f_decode8)) {
            return 2;
        }
        return 0;
    }
    return 0;
}

static int decode32(DisasContext *ctx, uint32_t insn)
{
    return 0;
}

static void getAlias(char *buffer, int regNo)
{
    switch (regNo) {
    case 0:
        strncpy(buffer, "zero", 5);
        break;
    case 1:
        strncpy(buffer, "at", 5);
        break;
    case 2:
    case 3:
        sprintf(buffer, "v%d", regNo - 2);
        break;
    case 4:
    case 5:
    case 6:
    case 7:
        sprintf(buffer, "a%d", regNo - 4);
        break;
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        sprintf(buffer, "t%d", regNo - 8);
        break;
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
        sprintf(buffer, "s%d", regNo - 16);
        break;
    case 24:
    case 25:
        sprintf(buffer, "t%d", regNo - 16);
        break;
    case 28:
        strncpy(buffer, "gp", 5);
        break;
    case 29:
        strncpy(buffer, "sp", 5);
        break;
    case 30:
        strncpy(buffer, "s8", 5);
        break;
    case 31:
        strncpy(buffer, "ra", 5);
        break;
    default:
        sprintf(buffer, "r%d", regNo);
        break;
    }
}

int print_insn_micromips_r6(bfd_vma addr, disassemble_info *info)
{
    bfd_byte buffer[4];
    uint16_t insn16;
    uint32_t insn32;
    int status, size;

    status = info->read_memory_func(addr, buffer, 4, info);
    if (status != 0) {
        info->memory_error_func(status, addr, info);
        return -1;
    }
    if (info->endian == BFD_ENDIAN_BIG) {
        insn32 = bfd_getb32(buffer);
        insn16 = (uint16_t) (insn32 >> 16);
    } else {
        insn32 = bfd_getl32(buffer);
        insn16 = (uint16_t) (insn32 >> 16);
    }

    size = decode16(info, insn16);
    if (size == 0) {
        size = decode32(info, insn32);
        (info->fprintf_func(info->stream, "%-9s " "%#08x", "\t.long", insn32));
    } else {
        (info->fprintf_func(info->stream, "%-9s " "%x", "\t.word", insn16));
    }

    return size == 0 ? -1 : size;
}

static bool trans_ADDIUR1SP(disassemble_info *info, arg_ADDIUR1SP *a)
{
    char alias[5];
    getAlias(alias, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "ADDIUR1SP",
     alias, a->imm));
    return true;
}

static bool trans_ADDIUR2(disassemble_info *info, arg_ADDIUR2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rd);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "ADDIUR2",
     alias, alias1, a->imm));
    return true;
}

static bool trans_ADDIUS5(disassemble_info *info, arg_ADDIUS5 *a)
{
    char alias[5];
    getAlias(alias, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "ADDIUS5",
     alias, a->imm));
    return true;
}

static bool trans_ADDIUSP(disassemble_info *info, arg_ADDIUSP *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "ADDIUSP", a->imm));
    return true;
}

static bool trans_ADDU16(disassemble_info *info, arg_ADDU16 *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rs);
    getAlias(alias1, a->rt);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "ADDU16",
     alias, alias1, alias2));
    return true;
}

static bool trans_AND16(disassemble_info *info, arg_AND16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "AND16",
     alias, alias1));
    return true;
}

static bool trans_ANDI16(disassemble_info *info, arg_ANDI16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rd);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "ANDI16",
     alias, alias1, a->imm));
    return true;
}

static bool trans_BC16(disassemble_info *info, arg_BC16 *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BC16", a->offset));
    return true;
}

static bool trans_BEQZC16(disassemble_info *info, arg_BEQZC16 *a)
{
    char alias[5];
    getAlias(alias, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "BEQZC16",
     alias, a->offset));
    return true;
}

static bool trans_BNEZC16(disassemble_info *info, arg_BNEZC16 *a)
{
    char alias[5];
    getAlias(alias, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "BNEZC16",
     alias, a->offset));
    return true;
}

static bool trans_BREAK16(disassemble_info *info, arg_BREAK16 *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BREAK16",
     a->code));
    return true;
}

static bool trans_JALRC16(disassemble_info *info, arg_JALRC16 *a)
{
    char alias[5];
    getAlias(alias, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s", "" "JALRC16", alias));
    return true;
}

static bool trans_JRCADDIUSP(disassemble_info *info, arg_JRCADDIUSP *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "JRCADDIUSP", a->imm));
    return true;
}

static bool trans_JRC16(disassemble_info *info, arg_JRC16 *a)
{
    char alias[5];
    getAlias(alias, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s", "" "JRC16", alias));
    return true;
}

static bool trans_LBU16(disassemble_info *info, arg_LBU16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LBU16",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LHU16(disassemble_info *info, arg_LHU16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LHU16",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LI16(disassemble_info *info, arg_LI16 *a)
{
    char alias[5];
    getAlias(alias, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "LI16",
     alias, a->imm));
    return true;
}

static bool trans_LW16(disassemble_info *info, arg_LW16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LW16",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LWM16(disassemble_info *info, arg_LWM16 *a)
{
    char alias[5];
    getAlias(alias, a->reglist);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "LWM16",
     alias, a->offset));
    return true;
}

static bool trans_LWGP(disassemble_info *info, arg_LWGP *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "LWGP",
     alias, a->offset));
    return true;
}

static bool trans_LWSP(disassemble_info *info, arg_LWSP *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "LWSP",
     alias, a->offset));
    return true;
}

static bool trans_MOVE16(disassemble_info *info, arg_MOVE16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rd);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "MOVE16",
     alias, alias1));
    return true;
}

static bool trans_MOVEP(disassemble_info *info, arg_MOVEP *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rd);
    getAlias(alias1, a->rt);
    getAlias(alias2, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MOVEP",
     alias, alias1, alias2));
    return true;
}

static bool trans_NOT16(disassemble_info *info, arg_NOT16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "NOT16",
     alias, alias1));
    return true;
}

static bool trans_OR16(disassemble_info *info, arg_OR16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "OR16",
     alias, alias1));
    return true;
}

static bool trans_SB16(disassemble_info *info, arg_SB16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SB16",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SDBBP16(disassemble_info *info, arg_SDBBP16 *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "SDBBP16",
     a->code));
    return true;
}

static bool trans_SH16(disassemble_info *info, arg_SH16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SH16",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SLL16(disassemble_info *info, arg_SLL16 *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rd);
    getAlias(alias1, a->rt);
    getAlias(alias2, a->sa);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SLL16",
     alias, alias1, alias2));
    return true;
}

static bool trans_SRL16(disassemble_info *info, arg_SRL16 *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rd);
    getAlias(alias1, a->rt);
    getAlias(alias2, a->sa);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SRL16",
     alias, alias1, alias2));
    return true;
}

static bool trans_SUBU16(disassemble_info *info, arg_SUBU16 *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rs);
    getAlias(alias1, a->rt);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SUBU16",
     alias, alias1, alias2));
    return true;
}

static bool trans_SW16(disassemble_info *info, arg_SW16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SW16",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SWSP(disassemble_info *info, arg_SWSP *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "SWSP",
     alias, a->offset));
    return true;
}

static bool trans_SWM16(disassemble_info *info, arg_SWM16 *a)
{
    char alias[5];
    getAlias(alias, a->reglist);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "SWM16",
     alias, a->offset));
    return true;
}

static bool trans_XOR16(disassemble_info *info, arg_XOR16 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "XOR16",
     alias, alias1));
    return true;
}

