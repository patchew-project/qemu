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

/* 32-bit */

typedef arg_decode0 arg_LWM32;
typedef arg_decode0 arg_SWM32;

static bool trans_LWM32(DisasContext *ctx, arg_LWM32 *a);
static bool trans_SWM32(DisasContext *ctx, arg_SWM32 *a);

typedef arg_decode1 arg_LWP;

static bool trans_LWP(DisasContext *ctx, arg_LWP *a);

typedef arg_decode2 arg_SWP;

static bool trans_SWP(DisasContext *ctx, arg_SWP *a);

typedef arg_decode3 arg_ADD;
typedef arg_decode3 arg_ADDU;
typedef arg_decode3 arg_SUB;
typedef arg_decode3 arg_SUBU;
typedef arg_decode3 arg_SELEQZ;
typedef arg_decode3 arg_SELNEZ;
typedef arg_decode3 arg_DIV;
typedef arg_decode3 arg_MOD;
typedef arg_decode3 arg_DIVU;
typedef arg_decode3 arg_MODU;
typedef arg_decode3 arg_MUL;
typedef arg_decode3 arg_MUH;
typedef arg_decode3 arg_MULU;
typedef arg_decode3 arg_MUHU;
typedef arg_decode3 arg_AND;
typedef arg_decode3 arg_XOR;
typedef arg_decode3 arg_NOR;
typedef arg_decode3 arg_OR;
typedef arg_decode3 arg_SLLV;
typedef arg_decode3 arg_SLT;
typedef arg_decode3 arg_SLTU;
typedef arg_decode3 arg_SRAV;
typedef arg_decode3 arg_SRLV;
typedef arg_decode3 arg_ROTRV;

static bool trans_ADD(DisasContext *ctx, arg_ADD *a);
static bool trans_ADDU(DisasContext *ctx, arg_ADDU *a);
static bool trans_SUB(DisasContext *ctx, arg_SUB *a);
static bool trans_SUBU(DisasContext *ctx, arg_SUBU *a);
static bool trans_SELEQZ(DisasContext *ctx, arg_SELEQZ *a);
static bool trans_SELNEZ(DisasContext *ctx, arg_SELNEZ *a);
static bool trans_DIV(DisasContext *ctx, arg_DIV *a);
static bool trans_MOD(DisasContext *ctx, arg_MOD *a);
static bool trans_DIVU(DisasContext *ctx, arg_DIVU *a);
static bool trans_MODU(DisasContext *ctx, arg_MODU *a);
static bool trans_MUL(DisasContext *ctx, arg_MUL *a);
static bool trans_MUH(DisasContext *ctx, arg_MUH *a);
static bool trans_MULU(DisasContext *ctx, arg_MULU *a);
static bool trans_MUHU(DisasContext *ctx, arg_MUHU *a);
static bool trans_AND(DisasContext *ctx, arg_AND *a);
static bool trans_XOR(DisasContext *ctx, arg_XOR *a);
static bool trans_NOR(DisasContext *ctx, arg_NOR *a);
static bool trans_OR(DisasContext *ctx, arg_OR *a);
static bool trans_SLLV(DisasContext *ctx, arg_SLLV *a);
static bool trans_SLT(DisasContext *ctx, arg_SLT *a);
static bool trans_SLTU(DisasContext *ctx, arg_SLTU *a);
static bool trans_SRAV(DisasContext *ctx, arg_SRAV *a);
static bool trans_SRLV(DisasContext *ctx, arg_SRLV *a);
static bool trans_ROTRV(DisasContext *ctx, arg_ROTRV *a);

typedef arg_decode4 arg_ANDI;
typedef arg_decode4 arg_AUI;
typedef arg_decode4 arg_ORI;
typedef arg_decode4 arg_XORI;
typedef arg_decode4 arg_ADDIU;
typedef arg_decode4 arg_SLTI;
typedef arg_decode4 arg_SLTIU;

static bool trans_ANDI(DisasContext *ctx, arg_ANDI *a);
static bool trans_AUI(DisasContext *ctx, arg_AUI *a);
static bool trans_ORI(DisasContext *ctx, arg_ORI *a);
static bool trans_XORI(DisasContext *ctx, arg_XORI *a);
static bool trans_ADDIU(DisasContext *ctx, arg_ADDIU *a);
static bool trans_SLTI(DisasContext *ctx, arg_SLTI *a);
static bool trans_SLTIU(DisasContext *ctx, arg_SLTIU *a);

typedef arg_decode5 arg_TLBINV;
typedef arg_decode5 arg_TLBINVF;
typedef arg_decode5 arg_TLBP;
typedef arg_decode5 arg_TLBR;
typedef arg_decode5 arg_TLBWI;
typedef arg_decode5 arg_TLBWR;
typedef arg_decode5 arg_NOP;
typedef arg_decode5 arg_PAUSE;
typedef arg_decode5 arg_SSNOP;
typedef arg_decode5 arg_EHB;
typedef arg_decode5 arg_ERET;
typedef arg_decode5 arg_ERETNC;
typedef arg_decode5 arg_DERET;

static bool trans_TLBINV(DisasContext *ctx, arg_TLBINV *a);
static bool trans_TLBINVF(DisasContext *ctx, arg_TLBINVF *a);
static bool trans_TLBP(DisasContext *ctx, arg_TLBP *a);
static bool trans_TLBR(DisasContext *ctx, arg_TLBR *a);
static bool trans_TLBWI(DisasContext *ctx, arg_TLBWI *a);
static bool trans_TLBWR(DisasContext *ctx, arg_TLBWR *a);
static bool trans_NOP(DisasContext *ctx, arg_NOP *a);
static bool trans_PAUSE(DisasContext *ctx, arg_PAUSE *a);
static bool trans_SSNOP(DisasContext *ctx, arg_SSNOP *a);
static bool trans_EHB(DisasContext *ctx, arg_EHB *a);
static bool trans_ERET(DisasContext *ctx, arg_ERET *a);
static bool trans_ERETNC(DisasContext *ctx, arg_ERETNC *a);
static bool trans_DERET(DisasContext *ctx, arg_DERET *a);

typedef arg_decode6 arg_BC1EQZC;
typedef arg_decode6 arg_BC1NEZC;

static bool trans_BC1EQZC(DisasContext *ctx, arg_BC1EQZC *a);
static bool trans_BC1NEZC(DisasContext *ctx, arg_BC1NEZC *a);

typedef arg_decode7 arg_BC2EQZC;
typedef arg_decode7 arg_BC2NEZC;

static bool trans_BC2EQZC(DisasContext *ctx, arg_BC2EQZC *a);
static bool trans_BC2NEZC(DisasContext *ctx, arg_BC2NEZC *a);

typedef arg_decode8 arg_TEQ;
typedef arg_decode8 arg_TGE;
typedef arg_decode8 arg_TGEU;
typedef arg_decode8 arg_TLT;
typedef arg_decode8 arg_TLTU;
typedef arg_decode8 arg_TNE;

static bool trans_TEQ(DisasContext *ctx, arg_TEQ *a);
static bool trans_TGE(DisasContext *ctx, arg_TGE *a);
static bool trans_TGEU(DisasContext *ctx, arg_TGEU *a);
static bool trans_TLT(DisasContext *ctx, arg_TLT *a);
static bool trans_TLTU(DisasContext *ctx, arg_TLTU *a);
static bool trans_TNE(DisasContext *ctx, arg_TNE *a);

typedef arg_decode9 arg_BREAK;
typedef arg_decode9 arg_SYSCALL;
typedef arg_decode9 arg_SDBBP;
typedef arg_decode9 arg_WAIT;
typedef arg_decode9 arg_SIGRIE;

static bool trans_BREAK(DisasContext *ctx, arg_BREAK *a);
static bool trans_SYSCALL(DisasContext *ctx, arg_SYSCALL *a);
static bool trans_SDBBP(DisasContext *ctx, arg_SDBBP *a);
static bool trans_WAIT(DisasContext *ctx, arg_WAIT *a);
static bool trans_SIGRIE(DisasContext *ctx, arg_SIGRIE *a);

typedef arg_decode10 arg_BLEZC;
typedef arg_decode10 arg_BGTZC;
typedef arg_decode10 arg_JIALC;
typedef arg_decode10 arg_JIC;
typedef arg_decode10 arg_LWPC;
typedef arg_decode10 arg_BEQZALC;
typedef arg_decode10 arg_BNEZALC;
typedef arg_decode10 arg_BLEZALC;
typedef arg_decode10 arg_BGTZALC;

static bool trans_BLEZC(DisasContext *ctx, arg_BLEZC *a);
static bool trans_BGTZC(DisasContext *ctx, arg_BGTZC *a);
static bool trans_JIALC(DisasContext *ctx, arg_JIALC *a);
static bool trans_JIC(DisasContext *ctx, arg_JIC *a);
static bool trans_LWPC(DisasContext *ctx, arg_LWPC *a);
static bool trans_BEQZALC(DisasContext *ctx, arg_BEQZALC *a);
static bool trans_BNEZALC(DisasContext *ctx, arg_BNEZALC *a);
static bool trans_BLEZALC(DisasContext *ctx, arg_BLEZALC *a);
static bool trans_BGTZALC(DisasContext *ctx, arg_BGTZALC *a);

typedef arg_decode11 arg_BGEUC;
typedef arg_decode11 arg_BLTUC;
typedef arg_decode11 arg_BOVC;
typedef arg_decode11 arg_BNVC;
typedef arg_decode11 arg_BEQC;
typedef arg_decode11 arg_BNEC;
typedef arg_decode11 arg_BGEZALC;
typedef arg_decode11 arg_BLTZC;
typedef arg_decode11 arg_BLTC;
typedef arg_decode11 arg_BLTZALC;
typedef arg_decode11 arg_BGEZC;
typedef arg_decode11 arg_BGEC;

static bool trans_BGEUC(DisasContext *ctx, arg_BGEUC *a);
static bool trans_BLTUC(DisasContext *ctx, arg_BLTUC *a);
static bool trans_BOVC(DisasContext *ctx, arg_BOVC *a);
static bool trans_BNVC(DisasContext *ctx, arg_BNVC *a);
static bool trans_BEQC(DisasContext *ctx, arg_BEQC *a);
static bool trans_BNEC(DisasContext *ctx, arg_BNEC *a);
static bool trans_BGEZALC(DisasContext *ctx, arg_BGEZALC *a);
static bool trans_BLTZC(DisasContext *ctx, arg_BLTZC *a);
static bool trans_BLTC(DisasContext *ctx, arg_BLTC *a);
static bool trans_BLTZALC(DisasContext *ctx, arg_BLTZALC *a);
static bool trans_BGEZC(DisasContext *ctx, arg_BGEZC *a);
static bool trans_BGEC(DisasContext *ctx, arg_BGEC *a);

typedef arg_decode12 arg_MADDFfmt;
typedef arg_decode12 arg_MSUBFfmt;
typedef arg_decode12 arg_MAXfmt;
typedef arg_decode12 arg_MINfmt;
typedef arg_decode12 arg_MAXAfmt;
typedef arg_decode12 arg_MINAfmt;
typedef arg_decode12 arg_SELfmt;
typedef arg_decode12 arg_SELEQZfmt;
typedef arg_decode12 arg_SELNEQZfmt;
typedef arg_decode12 arg_ADDfmt;
typedef arg_decode12 arg_SUBfmt;
typedef arg_decode12 arg_DIVfmt;
typedef arg_decode12 arg_MULfmt;

static bool trans_MADDFfmt(DisasContext *ctx, arg_MADDFfmt *a);
static bool trans_MSUBFfmt(DisasContext *ctx, arg_MSUBFfmt *a);
static bool trans_MAXfmt(DisasContext *ctx, arg_MAXfmt *a);
static bool trans_MINfmt(DisasContext *ctx, arg_MINfmt *a);
static bool trans_MAXAfmt(DisasContext *ctx, arg_MAXAfmt *a);
static bool trans_MINAfmt(DisasContext *ctx, arg_MINAfmt *a);
static bool trans_SELfmt(DisasContext *ctx, arg_SELfmt *a);
static bool trans_SELEQZfmt(DisasContext *ctx, arg_SELEQZfmt *a);
static bool trans_SELNEQZfmt(DisasContext *ctx, arg_SELNEQZfmt *a);
static bool trans_ADDfmt(DisasContext *ctx, arg_ADDfmt *a);
static bool trans_SUBfmt(DisasContext *ctx, arg_SUBfmt *a);
static bool trans_DIVfmt(DisasContext *ctx, arg_DIVfmt *a);
static bool trans_MULfmt(DisasContext *ctx, arg_MULfmt *a);

typedef arg_decode13 arg_ALUIPC;
typedef arg_decode13 arg_AUIPC;
typedef arg_decode13 arg_ADDIUPC;
typedef arg_decode13 arg_LUI;

static bool trans_ALUIPC(DisasContext *ctx, arg_ALUIPC *a);
static bool trans_AUIPC(DisasContext *ctx, arg_AUIPC *a);
static bool trans_ADDIUPC(DisasContext *ctx, arg_ADDIUPC *a);
static bool trans_LUI(DisasContext *ctx, arg_LUI *a);

typedef arg_decode14 arg_CACHE;
typedef arg_decode14 arg_CACHEE;

static bool trans_CACHE(DisasContext *ctx, arg_CACHE *a);
static bool trans_CACHEE(DisasContext *ctx, arg_CACHEE *a);

typedef arg_decode15 arg_SBE;
typedef arg_decode15 arg_SC;
typedef arg_decode15 arg_SCE;
typedef arg_decode15 arg_SHE;
typedef arg_decode15 arg_SWE;
typedef arg_decode15 arg_LHE;
typedef arg_decode15 arg_LHUE;
typedef arg_decode15 arg_LL;
typedef arg_decode15 arg_LLE;
typedef arg_decode15 arg_LBE;
typedef arg_decode15 arg_LBUE;
typedef arg_decode15 arg_LWE;
typedef arg_decode15 arg_SB;
typedef arg_decode15 arg_LW;
typedef arg_decode15 arg_LH;
typedef arg_decode15 arg_LHU;
typedef arg_decode15 arg_SW;
typedef arg_decode15 arg_LB;
typedef arg_decode15 arg_LBU;
typedef arg_decode15 arg_SH;
typedef arg_decode15 arg_LDC2;
typedef arg_decode15 arg_SWC2;
typedef arg_decode15 arg_LWC2;
typedef arg_decode15 arg_SDC2;

static bool trans_SBE(DisasContext *ctx, arg_SBE *a);
static bool trans_SC(DisasContext *ctx, arg_SC *a);
static bool trans_SCE(DisasContext *ctx, arg_SCE *a);
static bool trans_SHE(DisasContext *ctx, arg_SHE *a);
static bool trans_SWE(DisasContext *ctx, arg_SWE *a);
static bool trans_LHE(DisasContext *ctx, arg_LHE *a);
static bool trans_LHUE(DisasContext *ctx, arg_LHUE *a);
static bool trans_LL(DisasContext *ctx, arg_LL *a);
static bool trans_LLE(DisasContext *ctx, arg_LLE *a);
static bool trans_LBE(DisasContext *ctx, arg_LBE *a);
static bool trans_LBUE(DisasContext *ctx, arg_LBUE *a);
static bool trans_LWE(DisasContext *ctx, arg_LWE *a);
static bool trans_SB(DisasContext *ctx, arg_SB *a);
static bool trans_LW(DisasContext *ctx, arg_LW *a);
static bool trans_LH(DisasContext *ctx, arg_LH *a);
static bool trans_LHU(DisasContext *ctx, arg_LHU *a);
static bool trans_SW(DisasContext *ctx, arg_SW *a);
static bool trans_LB(DisasContext *ctx, arg_LB *a);
static bool trans_LBU(DisasContext *ctx, arg_LBU *a);
static bool trans_SH(DisasContext *ctx, arg_SH *a);
static bool trans_LDC2(DisasContext *ctx, arg_LDC2 *a);
static bool trans_SWC2(DisasContext *ctx, arg_SWC2 *a);
static bool trans_LWC2(DisasContext *ctx, arg_LWC2 *a);
static bool trans_SDC2(DisasContext *ctx, arg_SDC2 *a);

typedef arg_decode16 arg_BALC;
typedef arg_decode16 arg_BC;

static bool trans_BALC(DisasContext *ctx, arg_BALC *a);
static bool trans_BC(DisasContext *ctx, arg_BC *a);

typedef arg_decode17 arg_CLO;
typedef arg_decode17 arg_CLZ;
typedef arg_decode17 arg_JALRC;
typedef arg_decode17 arg_JALRCHB;
typedef arg_decode17 arg_SEB;
typedef arg_decode17 arg_SEH;
typedef arg_decode17 arg_WRPGPR;
typedef arg_decode17 arg_WSBH;
typedef arg_decode17 arg_RDPGPR;

static bool trans_CLO(DisasContext *ctx, arg_CLO *a);
static bool trans_CLZ(DisasContext *ctx, arg_CLZ *a);
static bool trans_JALRC(DisasContext *ctx, arg_JALRC *a);
static bool trans_JALRCHB(DisasContext *ctx, arg_JALRCHB *a);
static bool trans_SEB(DisasContext *ctx, arg_SEB *a);
static bool trans_SEH(DisasContext *ctx, arg_SEH *a);
static bool trans_WRPGPR(DisasContext *ctx, arg_WRPGPR *a);
static bool trans_WSBH(DisasContext *ctx, arg_WSBH *a);
static bool trans_RDPGPR(DisasContext *ctx, arg_RDPGPR *a);

typedef arg_decode18 arg_EI;
typedef arg_decode18 arg_EVP;
typedef arg_decode18 arg_DI;
typedef arg_decode18 arg_DVP;

static bool trans_EI(DisasContext *ctx, arg_EI *a);
static bool trans_EVP(DisasContext *ctx, arg_EVP *a);
static bool trans_DI(DisasContext *ctx, arg_DI *a);
static bool trans_DVP(DisasContext *ctx, arg_DVP *a);

typedef arg_decode19 arg_ALIGN;

static bool trans_ALIGN(DisasContext *ctx, arg_ALIGN *a);

typedef arg_decode20 arg_EXT;
typedef arg_decode20 arg_INS;

static bool trans_EXT(DisasContext *ctx, arg_EXT *a);
static bool trans_INS(DisasContext *ctx, arg_INS *a);

typedef arg_decode21 arg_MOVfmt;
typedef arg_decode21 arg_ABSfmt;
typedef arg_decode21 arg_CVTDfmt;
typedef arg_decode21 arg_CVTSfmt;
typedef arg_decode21 arg_NEGfmt;
typedef arg_decode21 arg_CVTLfmt;
typedef arg_decode21 arg_CVTWfmt;
typedef arg_decode21 arg_FLOORLfmt;
typedef arg_decode21 arg_FLOORWfmt;
typedef arg_decode21 arg_RECIPfmt;
typedef arg_decode21 arg_ROUNDLfmt;
typedef arg_decode21 arg_ROUNDWfmt;
typedef arg_decode21 arg_RSQRTfmt;
typedef arg_decode21 arg_SQRTfmt;
typedef arg_decode21 arg_TRUNCLfmt;
typedef arg_decode21 arg_TRUNCWfmt;
typedef arg_decode21 arg_CEILLfmt;
typedef arg_decode21 arg_CEILWfmt;

static bool trans_MOVfmt(DisasContext *ctx, arg_MOVfmt *a);
static bool trans_ABSfmt(DisasContext *ctx, arg_ABSfmt *a);
static bool trans_CVTDfmt(DisasContext *ctx, arg_CVTDfmt *a);
static bool trans_CVTSfmt(DisasContext *ctx, arg_CVTSfmt *a);
static bool trans_NEGfmt(DisasContext *ctx, arg_NEGfmt *a);
static bool trans_CVTLfmt(DisasContext *ctx, arg_CVTLfmt *a);
static bool trans_CVTWfmt(DisasContext *ctx, arg_CVTWfmt *a);
static bool trans_FLOORLfmt(DisasContext *ctx, arg_FLOORLfmt *a);
static bool trans_FLOORWfmt(DisasContext *ctx, arg_FLOORWfmt *a);
static bool trans_RECIPfmt(DisasContext *ctx, arg_RECIPfmt *a);
static bool trans_ROUNDLfmt(DisasContext *ctx, arg_ROUNDLfmt *a);
static bool trans_ROUNDWfmt(DisasContext *ctx, arg_ROUNDWfmt *a);
static bool trans_RSQRTfmt(DisasContext *ctx, arg_RSQRTfmt *a);
static bool trans_SQRTfmt(DisasContext *ctx, arg_SQRTfmt *a);
static bool trans_TRUNCLfmt(DisasContext *ctx, arg_TRUNCLfmt *a);
static bool trans_TRUNCWfmt(DisasContext *ctx, arg_TRUNCWfmt *a);
static bool trans_CEILLfmt(DisasContext *ctx, arg_CEILLfmt *a);
static bool trans_CEILWfmt(DisasContext *ctx, arg_CEILWfmt *a);

typedef arg_decode22 arg_CLASSfmt;
typedef arg_decode22 arg_RINTfmt;

static bool trans_CLASSfmt(DisasContext *ctx, arg_CLASSfmt *a);
static bool trans_RINTfmt(DisasContext *ctx, arg_RINTfmt *a);

typedef arg_decode23 arg_PREF;
typedef arg_decode23 arg_PREFE;

static bool trans_PREF(DisasContext *ctx, arg_PREF *a);
static bool trans_PREFE(DisasContext *ctx, arg_PREFE *a);

typedef arg_decode24 arg_MFC0;
typedef arg_decode24 arg_MFHC0;
typedef arg_decode24 arg_MTC0;
typedef arg_decode24 arg_MTHC0;
typedef arg_decode24 arg_RDHWR;

static bool trans_MFC0(DisasContext *ctx, arg_MFC0 *a);
static bool trans_MFHC0(DisasContext *ctx, arg_MFHC0 *a);
static bool trans_MTC0(DisasContext *ctx, arg_MTC0 *a);
static bool trans_MTHC0(DisasContext *ctx, arg_MTHC0 *a);
static bool trans_RDHWR(DisasContext *ctx, arg_RDHWR *a);

typedef arg_decode25 arg_SRL;
typedef arg_decode25 arg_SRA;
typedef arg_decode25 arg_ROTR;
typedef arg_decode25 arg_SLL;

static bool trans_SRL(DisasContext *ctx, arg_SRL *a);
static bool trans_SRA(DisasContext *ctx, arg_SRA *a);
static bool trans_ROTR(DisasContext *ctx, arg_ROTR *a);
static bool trans_SLL(DisasContext *ctx, arg_SLL *a);

typedef arg_decode26 arg_MFC1;
typedef arg_decode26 arg_MFHC1;
typedef arg_decode26 arg_CFC1;
typedef arg_decode26 arg_CTC1;
typedef arg_decode26 arg_MTC1;
typedef arg_decode26 arg_MTHC1;

static bool trans_MFC1(DisasContext *ctx, arg_MFC1 *a);
static bool trans_MFHC1(DisasContext *ctx, arg_MFHC1 *a);
static bool trans_CFC1(DisasContext *ctx, arg_CFC1 *a);
static bool trans_CTC1(DisasContext *ctx, arg_CTC1 *a);
static bool trans_MTC1(DisasContext *ctx, arg_MTC1 *a);
static bool trans_MTHC1(DisasContext *ctx, arg_MTHC1 *a);

typedef arg_decode27 arg_CFC2;
typedef arg_decode27 arg_CTC2;
typedef arg_decode27 arg_MTC2;
typedef arg_decode27 arg_MTHC2;
typedef arg_decode27 arg_MFC2;
typedef arg_decode27 arg_MFHC2;

static bool trans_CFC2(DisasContext *ctx, arg_CFC2 *a);
static bool trans_CTC2(DisasContext *ctx, arg_CTC2 *a);
static bool trans_MTC2(DisasContext *ctx, arg_MTC2 *a);
static bool trans_MTHC2(DisasContext *ctx, arg_MTHC2 *a);
static bool trans_MFC2(DisasContext *ctx, arg_MFC2 *a);
static bool trans_MFHC2(DisasContext *ctx, arg_MFHC2 *a);

typedef arg_decode28 arg_SYNC;

static bool trans_SYNC(DisasContext *ctx, arg_SYNC *a);

typedef arg_decode29 arg_LLWP;
typedef arg_decode29 arg_LLWPE;
typedef arg_decode29 arg_SCWP;
typedef arg_decode29 arg_SCWPE;

static bool trans_LLWP(DisasContext *ctx, arg_LLWP *a);
static bool trans_LLWPE(DisasContext *ctx, arg_LLWPE *a);
static bool trans_SCWP(DisasContext *ctx, arg_SCWP *a);
static bool trans_SCWPE(DisasContext *ctx, arg_SCWPE *a);

typedef arg_decode30 arg_SWC1;
typedef arg_decode30 arg_LDC1;
typedef arg_decode30 arg_LWC1;
typedef arg_decode30 arg_SDC1;

static bool trans_SWC1(DisasContext *ctx, arg_SWC1 *a);
static bool trans_LDC1(DisasContext *ctx, arg_LDC1 *a);
static bool trans_LWC1(DisasContext *ctx, arg_LWC1 *a);
static bool trans_SDC1(DisasContext *ctx, arg_SDC1 *a);

typedef arg_decode31 arg_SYNCI;

static bool trans_SYNCI(DisasContext *ctx, arg_SYNCI *a);

typedef arg_decode32 arg_CMPcondnS;
typedef arg_decode32 arg_CMPcondnD;

static bool trans_CMPcondnS(DisasContext *ctx, arg_CMPcondnS *a);
static bool trans_CMPcondnD(DisasContext *ctx, arg_CMPcondnD *a);

typedef arg_decode33 arg_BITSWAP;

static bool trans_BITSWAP(DisasContext *ctx, arg_BITSWAP *a);

typedef arg_decode34 arg_COP2;

static bool trans_COP2(DisasContext *ctx, arg_COP2 *a);

typedef arg_decode35 arg_LSA;

static bool trans_LSA(DisasContext *ctx, arg_LSA *a);

typedef arg_decode36 arg_BNEZC;
typedef arg_decode36 arg_BEQZC;

static bool trans_BNEZC(DisasContext *ctx, arg_BNEZC *a);
static bool trans_BEQZC(DisasContext *ctx, arg_BEQZC *a);


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

/* 32-bit */

static void decode_extract_decode_Fmt_0(DisasContext *ctx, arg_decode0 *a,
    uint32_t insn)
{
    a->base = extract32(insn, 16, 5);
    a->reglist = extract32(insn, 21, 5);
    a->offset = sextract32(insn, 0, 12);
}

static void decode_extract_decode_Fmt_1(DisasContext *ctx, arg_decode1 *a,
    uint32_t insn)
{
    a->rd = extract32(insn, 21, 5);
    a->base = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 12);
}

static void decode_extract_decode_Fmt_2(DisasContext *ctx, arg_decode2 *a,
    uint32_t insn)
{
    a->base = extract32(insn, 16, 5);
    a->rs1 = extract32(insn, 21, 5);
    a->offset = sextract32(insn, 0, 12);
}

static void decode_extract_decode_Fmt_3(DisasContext *ctx, arg_decode3 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->rd = extract32(insn, 11, 5);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_4(DisasContext *ctx, arg_decode4 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->imm = extract32(insn, 0, 16);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_5(DisasContext *ctx, arg_decode4 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->imm = sextract32(insn, 0, 16);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_6(DisasContext *ctx, arg_decode5 *a,
    uint32_t insn)
{
}

static void decode_extract_decode_Fmt_7(DisasContext *ctx, arg_decode6 *a,
    uint32_t insn)
{
    a->ft = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_8(DisasContext *ctx, arg_decode7 *a,
    uint32_t insn)
{
    a->ct = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_9(DisasContext *ctx, arg_decode8 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->code = extract32(insn, 12, 4);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_10(DisasContext *ctx, arg_decode9 *a,
    uint32_t insn)
{
    a->code = extract32(insn, 6, 20);
}

static void decode_extract_decode_Fmt_11(DisasContext *ctx, arg_decode10 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->offset = extract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_12(DisasContext *ctx, arg_decode11 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->rs = extract32(insn, 16, 5);
    a->offset = extract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_13(DisasContext *ctx, arg_decode9 *a,
    uint32_t insn)
{
    a->code = extract32(insn, 16, 10);
}

static void decode_extract_decode_Fmt_14(DisasContext *ctx, arg_decode12 *a,
    uint32_t insn)
{
    a->ft = extract32(insn, 21, 5);
    a->fmt = extract32(insn, 9, 2);
    a->fs = extract32(insn, 16, 5);
    a->fd = extract32(insn, 11, 5);
}

static void decode_extract_decode_Fmt_15(DisasContext *ctx, arg_decode13 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->imm = sextract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_16(DisasContext *ctx, arg_decode14 *a,
    uint32_t insn)
{
    a->base = extract32(insn, 16, 5);
    a->op = extract32(insn, 21, 5);
    a->offset = sextract32(insn, 0, 9);
}

static void decode_extract_decode_Fmt_17(DisasContext *ctx, arg_decode15 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->base = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 9);
}

static void decode_extract_decode_Fmt_18(DisasContext *ctx, arg_decode11 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->rs = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_19(DisasContext *ctx, arg_decode16 *a,
    uint32_t insn)
{
    a->offset = sextract32(insn, 0, 26);
}

static void decode_extract_decode_Fmt_20(DisasContext *ctx, arg_decode17 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_21(DisasContext *ctx, arg_decode12 *a,
    uint32_t insn)
{
    a->ft = extract32(insn, 21, 5);
    a->fmt = extract32(insn, 8, 2);
    a->fs = extract32(insn, 16, 5);
    a->fd = extract32(insn, 11, 5);
}

static void decode_extract_decode_Fmt_22(DisasContext *ctx, arg_decode18 *a,
    uint32_t insn)
{
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_23(DisasContext *ctx, arg_decode10 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_24(DisasContext *ctx, arg_decode19 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->rd = extract32(insn, 11, 5);
    a->bp = extract32(insn, 9, 2);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_25(DisasContext *ctx, arg_decode20 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->msbd = extract32(insn, 11, 5);
    a->lsb = extract32(insn, 6, 5);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_26(DisasContext *ctx, arg_decode21 *a,
    uint32_t insn)
{
    a->fmt = extract32(insn, 13, 2);
    a->fs = extract32(insn, 16, 5);
    a->ft = extract32(insn, 21, 5);
}

static void decode_extract_decode_Fmt_27(DisasContext *ctx, arg_decode21 *a,
    uint32_t insn)
{
    a->fmt = extract32(insn, 14, 1);
    a->fs = extract32(insn, 16, 5);
    a->ft = extract32(insn, 21, 5);
}

static void decode_extract_decode_Fmt_28(DisasContext *ctx, arg_decode22 *a,
    uint32_t insn)
{
    a->fmt = extract32(insn, 9, 2);
    a->fs = extract32(insn, 21, 5);
    a->fd = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_29(DisasContext *ctx, arg_decode23 *a,
    uint32_t insn)
{
    a->hint = extract32(insn, 21, 5);
    a->base = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 9);
}

static void decode_extract_decode_Fmt_30(DisasContext *ctx, arg_decode24 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->sel = extract32(insn, 11, 3);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_31(DisasContext *ctx, arg_decode25 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->sa = extract32(insn, 11, 5);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_32(DisasContext *ctx, arg_decode26 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->fs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_33(DisasContext *ctx, arg_decode27 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->impl = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_34(DisasContext *ctx, arg_decode28 *a,
    uint32_t insn)
{
    a->stype = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_35(DisasContext *ctx, arg_decode29 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->rd = extract32(insn, 4, 5);
    a->base = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_36(DisasContext *ctx, arg_decode15 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->base = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_37(DisasContext *ctx, arg_decode30 *a,
    uint32_t insn)
{
    a->base = extract32(insn, 16, 5);
    a->ft = extract32(insn, 21, 5);
    a->offset = sextract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_38(DisasContext *ctx, arg_decode31 *a,
    uint32_t insn)
{
    a->base = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_39(DisasContext *ctx, arg_decode15 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->base = extract32(insn, 16, 5);
    a->offset = sextract32(insn, 0, 11);
}

static void decode_extract_decode_Fmt_40(DisasContext *ctx, arg_decode32 *a,
    uint32_t insn)
{
    a->ft = extract32(insn, 21, 5);
    a->fs = extract32(insn, 16, 5);
    a->fd = extract32(insn, 11, 5);
    a->condn = extract32(insn, 6, 5);
}

static void decode_extract_decode_Fmt_41(DisasContext *ctx, arg_decode33 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->rd = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_42(DisasContext *ctx, arg_decode13 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->imm = sextract32(insn, 0, 19);
}

static void decode_extract_decode_Fmt_43(DisasContext *ctx, arg_decode34 *a,
    uint32_t insn)
{
    a->cofun = extract32(insn, 3, 23);
}

static void decode_extract_decode_Fmt_44(DisasContext *ctx, arg_decode9 *a,
    uint32_t insn)
{
    a->code = extract32(insn, 6, 16);
}

static void decode_extract_decode_Fmt_45(DisasContext *ctx, arg_decode10 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->offset = sextract32(insn, 0, 19);
}

static void decode_extract_decode_Fmt_46(DisasContext *ctx, arg_decode35 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->rd = extract32(insn, 11, 5);
    a->sa = extract32(insn, 9, 2);
    a->rs = extract32(insn, 16, 5);
}

static void decode_extract_decode_Fmt_47(DisasContext *ctx, arg_decode13 *a,
    uint32_t insn)
{
    a->rt = extract32(insn, 21, 5);
    a->imm = extract32(insn, 0, 16);
}

static void decode_extract_decode_Fmt_48(DisasContext *ctx, arg_decode36 *a,
    uint32_t insn)
{
    a->rs = extract32(insn, 21, 5);
    a->offset = extract32(insn, 0, 21);
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
    union {
        arg_decode0 f_decode0;
        arg_decode1 f_decode1;
        arg_decode2 f_decode2;
        arg_decode3 f_decode3;
        arg_decode4 f_decode4;
        arg_decode5 f_decode5;
        arg_decode6 f_decode6;
        arg_decode7 f_decode7;
        arg_decode8 f_decode8;
        arg_decode9 f_decode9;
        arg_decode10 f_decode10;
        arg_decode11 f_decode11;
        arg_decode12 f_decode12;
        arg_decode13 f_decode13;
        arg_decode14 f_decode14;
        arg_decode15 f_decode15;
        arg_decode16 f_decode16;
        arg_decode17 f_decode17;
        arg_decode18 f_decode18;
        arg_decode19 f_decode19;
        arg_decode20 f_decode20;
        arg_decode21 f_decode21;
        arg_decode22 f_decode22;
        arg_decode23 f_decode23;
        arg_decode24 f_decode24;
        arg_decode25 f_decode25;
        arg_decode26 f_decode26;
        arg_decode27 f_decode27;
        arg_decode28 f_decode28;
        arg_decode29 f_decode29;
        arg_decode30 f_decode30;
        arg_decode31 f_decode31;
        arg_decode32 f_decode32;
        arg_decode33 f_decode33;
        arg_decode34 f_decode34;
        arg_decode35 f_decode35;
        arg_decode36 f_decode36;
    } u;

    switch ((insn >> 26) & 0b111111) {
    case 0b0:  /* 000000.. ........ ........ ........ */
        switch (insn & 0b111) {
        case 0b0:  /* 000000.. ........ ........ .....000 */
            switch ((insn >> 3) & 0b11111111) {
            case 0b0:  /* 000000.. ........ .....000 00000000 */
                decode_extract_decode_Fmt_6(ctx, &u.f_decode5, insn);
                switch ((insn >> 11) & 0b111111111111111) {
                case 0b0:  /* 00000000 00000000 00000000 00000000 */
                    if (trans_NOP(ctx, &u.f_decode5)) {
                        return 4;
                    }
                    return 0;
                case 0b1:  /* 00000000 00000000 00001000 00000000 */
                    if (trans_SSNOP(ctx, &u.f_decode5)) {
                        return 4;
                    }
                    return 0;
                case 0b11:  /* 00000000 00000000 00011000 00000000 */
                    if (trans_EHB(ctx, &u.f_decode5)) {
                        return 4;
                    }
                    return 0;
                case 0b101:  /* 00000000 00000000 00101000 00000000 */
                    if (trans_PAUSE(ctx, &u.f_decode5)) {
                        return 4;
                    }
                    return 0;
                default:  /* 000000.. ........ .....000 00000000 */
                    decode_extract_decode_Fmt_31(ctx, &u.f_decode25, insn);
                    if (trans_SLL(ctx, &u.f_decode25)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b10:  /* 000000.. ........ .....000 00010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_SLLV(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b11:  /* 000000.. ........ .....000 00011000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_MUL(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b1000:  /* 000000.. ........ .....000 01000000 */
                decode_extract_decode_Fmt_31(ctx, &u.f_decode25, insn);
                if (trans_SRL(ctx, &u.f_decode25)) {
                    return 4;
                }
                return 0;
            case 0b1001:  /* 000000.. ........ .....000 01001000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_SRLV(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b1011:  /* 000000.. ........ .....000 01011000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_MUH(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b10000:  /* 000000.. ........ .....000 10000000 */
                decode_extract_decode_Fmt_31(ctx, &u.f_decode25, insn);
                if (trans_SRA(ctx, &u.f_decode25)) {
                    return 4;
                }
                return 0;
            case 0b10010:  /* 000000.. ........ .....000 10010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_SRAV(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b10011:  /* 000000.. ........ .....000 10011000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_MULU(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b11000:  /* 000000.. ........ .....000 11000000 */
                decode_extract_decode_Fmt_31(ctx, &u.f_decode25, insn);
                if (trans_ROTR(ctx, &u.f_decode25)) {
                    return 4;
                }
                return 0;
            case 0b11010:  /* 000000.. ........ .....000 11010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_ROTRV(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b11011:  /* 000000.. ........ .....000 11011000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_MUHU(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b100010:  /* 000000.. ........ .....001 00010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_ADD(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b100011:  /* 000000.. ........ .....001 00011000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_DIV(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b101000:  /* 000000.. ........ .....001 01000000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_SELEQZ(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b101010:  /* 000000.. ........ .....001 01010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_ADDU(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b101011:  /* 000000.. ........ .....001 01011000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_MOD(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b110000:  /* 000000.. ........ .....001 10000000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_SELNEZ(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b110010:  /* 000000.. ........ .....001 10010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_SUB(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b110011:  /* 000000.. ........ .....001 10011000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_DIVU(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b111000:  /* 000000.. ........ .....001 11000000 */
                decode_extract_decode_Fmt_30(ctx, &u.f_decode24, insn);
                switch ((insn >> 14) & 0b11) {
                case 0b0:  /* 000000.. ........ 00...001 11000000 */
                    if (trans_RDHWR(ctx, &u.f_decode24)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b111010:  /* 000000.. ........ .....001 11010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_SUBU(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b111011:  /* 000000.. ........ .....001 11011000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_MODU(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b1001010:  /* 000000.. ........ .....010 01010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_AND(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b1010010:  /* 000000.. ........ .....010 10010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_OR(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b1011010:  /* 000000.. ........ .....010 11010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_NOR(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b1100010:  /* 000000.. ........ .....011 00010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_XOR(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b1101010:  /* 000000.. ........ .....011 01010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_SLT(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            case 0b1110010:  /* 000000.. ........ .....011 10010000 */
                decode_extract_decode_Fmt_3(ctx, &u.f_decode3, insn);
                if (trans_SLTU(ctx, &u.f_decode3)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b10:  /* 000000.. ........ ........ .....010 */
            decode_extract_decode_Fmt_43(ctx, &u.f_decode34, insn);
            if (trans_COP2(ctx, &u.f_decode34)) {
                return 4;
            }
            return 0;
        case 0b100:  /* 000000.. ........ ........ .....100 */
            switch ((insn >> 3) & 0b111) {
            case 0b1:  /* 000000.. ........ ........ ..001100 */
                decode_extract_decode_Fmt_25(ctx, &u.f_decode20, insn);
                if (trans_INS(ctx, &u.f_decode20)) {
                    return 4;
                }
                return 0;
            case 0b101:  /* 000000.. ........ ........ ..101100 */
                decode_extract_decode_Fmt_25(ctx, &u.f_decode20, insn);
                if (trans_EXT(ctx, &u.f_decode20)) {
                    return 4;
                }
                return 0;
            case 0b110:  /* 000000.. ........ ........ ..110100 */
                decode_extract_decode_Fmt_30(ctx, &u.f_decode24, insn);
                switch (insn & 0b1100011111000000) {
                case 0b11000000:  /* 000000.. ........ 00...000 11110100 */
                    if (trans_MFHC0(ctx, &u.f_decode24)) {
                        return 4;
                    }
                    return 0;
                case 0b1011000000:  /* 000000.. ........ 00...010 11110100 */
                    if (trans_MTHC0(ctx, &u.f_decode24)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b111:  /* 000000.. ........ ........ ..111100 */
                switch ((insn >> 6) & 0b11111) {
                case 0b0:  /* 000000.. ........ .....000 00111100 */
                    decode_extract_decode_Fmt_9(ctx, &u.f_decode8, insn);
                    switch ((insn >> 11) & 0b1) {
                    case 0b0:  /* 000000.. ........ ....0000 00111100 */
                        if (trans_TEQ(ctx, &u.f_decode8)) {
                            return 4;
                        }
                        return 0;
                    case 0b1:  /* 000000.. ........ ....1000 00111100 */
                        if (trans_TLT(ctx, &u.f_decode8)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                case 0b11:  /* 000000.. ........ .....000 11111100 */
                    decode_extract_decode_Fmt_30(ctx, &u.f_decode24, insn);
                    switch ((insn >> 14) & 0b11) {
                    case 0b0:  /* 000000.. ........ 00...000 11111100 */
                        if (trans_MFC0(ctx, &u.f_decode24)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                case 0b101:  /* 000000.. ........ .....001 01111100 */
                    switch ((insn >> 11) & 0b11111) {
                    case 0b11:  /* 000000.. ........ 00011001 01111100 */
                        decode_extract_decode_Fmt_22(ctx, &u.f_decode18, insn);
                        switch ((insn >> 21) & 0b11111) {
                        case 0b0:  /* 00000000 000..... 00011001 01111100 */
                            if (trans_DVP(ctx, &u.f_decode18)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b111:  /* 000000.. ........ 00111001 01111100 */
                        decode_extract_decode_Fmt_22(ctx, &u.f_decode18, insn);
                        switch ((insn >> 21) & 0b11111) {
                        case 0b0:  /* 00000000 000..... 00111001 01111100 */
                            if (trans_EVP(ctx, &u.f_decode18)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b11100:  /* 000000.. ........ 11100001 01111100 */
                        decode_extract_decode_Fmt_20(ctx, &u.f_decode17, insn);
                        if (trans_RDPGPR(ctx, &u.f_decode17)) {
                            return 4;
                        }
                        return 0;
                    case 0b11110:  /* 000000.. ........ 11110001 01111100 */
                        decode_extract_decode_Fmt_20(ctx, &u.f_decode17, insn);
                        if (trans_WRPGPR(ctx, &u.f_decode17)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                case 0b1000:  /* 000000.. ........ .....010 00111100 */
                    decode_extract_decode_Fmt_9(ctx, &u.f_decode8, insn);
                    switch ((insn >> 11) & 0b1) {
                    case 0b0:  /* 000000.. ........ ....0010 00111100 */
                        if (trans_TGE(ctx, &u.f_decode8)) {
                            return 4;
                        }
                        return 0;
                    case 0b1:  /* 000000.. ........ ....1010 00111100 */
                        if (trans_TLTU(ctx, &u.f_decode8)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                case 0b1011:  /* 000000.. ........ .....010 11111100 */
                    decode_extract_decode_Fmt_30(ctx, &u.f_decode24, insn);
                    switch ((insn >> 14) & 0b11) {
                    case 0b0:  /* 000000.. ........ 00...010 11111100 */
                        if (trans_MTC0(ctx, &u.f_decode24)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                case 0b1100:  /* 000000.. ........ .....011 00111100 */
                    switch ((insn >> 11) & 0b11111) {
                    case 0b1:  /* 000000.. ........ 00001011 00111100 */
                        decode_extract_decode_Fmt_41(ctx, &u.f_decode33, insn);
                        if (trans_BITSWAP(ctx, &u.f_decode33)) {
                            return 4;
                        }
                        return 0;
                    case 0b101:  /* 000000.. ........ 00101011 00111100 */
                        decode_extract_decode_Fmt_20(ctx, &u.f_decode17, insn);
                        if (trans_SEB(ctx, &u.f_decode17)) {
                            return 4;
                        }
                        return 0;
                    case 0b111:  /* 000000.. ........ 00111011 00111100 */
                        decode_extract_decode_Fmt_20(ctx, &u.f_decode17, insn);
                        if (trans_SEH(ctx, &u.f_decode17)) {
                            return 4;
                        }
                        return 0;
                    case 0b1001:  /* 000000.. ........ 01001011 00111100 */
                        decode_extract_decode_Fmt_20(ctx, &u.f_decode17, insn);
                        if (trans_CLO(ctx, &u.f_decode17)) {
                            return 4;
                        }
                        return 0;
                    case 0b1011:  /* 000000.. ........ 01011011 00111100 */
                        decode_extract_decode_Fmt_20(ctx, &u.f_decode17, insn);
                        if (trans_CLZ(ctx, &u.f_decode17)) {
                            return 4;
                        }
                        return 0;
                    case 0b1111:  /* 000000.. ........ 01111011 00111100 */
                        decode_extract_decode_Fmt_20(ctx, &u.f_decode17, insn);
                        if (trans_WSBH(ctx, &u.f_decode17)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                case 0b1101:  /* 000000.. ........ .....011 01111100 */
                    switch ((insn >> 11) & 0b11111) {
                    case 0b0:  /* 000000.. ........ 00000011 01111100 */
                        decode_extract_decode_Fmt_6(ctx, &u.f_decode5, insn);
                        switch ((insn >> 16) & 0b1111111111) {
                        case 0b0:  /* 00000000 00000000 00000011 01111100 */
                            if (trans_TLBP(ctx, &u.f_decode5)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b10:  /* 000000.. ........ 00010011 01111100 */
                        decode_extract_decode_Fmt_6(ctx, &u.f_decode5, insn);
                        switch ((insn >> 16) & 0b1111111111) {
                        case 0b0:  /* 00000000 00000000 00010011 01111100 */
                            if (trans_TLBR(ctx, &u.f_decode5)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b100:  /* 000000.. ........ 00100011 01111100 */
                        decode_extract_decode_Fmt_6(ctx, &u.f_decode5, insn);
                        switch ((insn >> 16) & 0b1111111111) {
                        case 0b0:  /* 00000000 00000000 00100011 01111100 */
                            if (trans_TLBWI(ctx, &u.f_decode5)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b110:  /* 000000.. ........ 00110011 01111100 */
                        decode_extract_decode_Fmt_6(ctx, &u.f_decode5, insn);
                        switch ((insn >> 16) & 0b1111111111) {
                        case 0b0:  /* 00000000 00000000 00110011 01111100 */
                            if (trans_TLBWR(ctx, &u.f_decode5)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b1000:  /* 000000.. ........ 01000011 01111100 */
                        decode_extract_decode_Fmt_6(ctx, &u.f_decode5, insn);
                        switch ((insn >> 16) & 0b1111111111) {
                        case 0b0:  /* 00000000 00000000 01000011 01111100 */
                            if (trans_TLBINV(ctx, &u.f_decode5)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b1010:  /* 000000.. ........ 01010011 01111100 */
                        decode_extract_decode_Fmt_6(ctx, &u.f_decode5, insn);
                        switch ((insn >> 16) & 0b1111111111) {
                        case 0b0:  /* 00000000 00000000 01010011 01111100 */
                            if (trans_TLBINVF(ctx, &u.f_decode5)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b1101:  /* 000000.. ........ 01101011 01111100 */
                        decode_extract_decode_Fmt_34(ctx, &u.f_decode28, insn);
                        switch ((insn >> 21) & 0b11111) {
                        case 0b0:  /* 00000000 000..... 01101011 01111100 */
                            if (trans_SYNC(ctx, &u.f_decode28)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b10001:  /* 000000.. ........ 10001011 01111100 */
                        decode_extract_decode_Fmt_13(ctx, &u.f_decode9, insn);
                        if (trans_SYSCALL(ctx, &u.f_decode9)) {
                            return 4;
                        }
                        return 0;
                    case 0b10010:  /* 000000.. ........ 10010011 01111100 */
                        decode_extract_decode_Fmt_13(ctx, &u.f_decode9, insn);
                        if (trans_WAIT(ctx, &u.f_decode9)) {
                            return 4;
                        }
                        return 0;
                    case 0b11011:  /* 000000.. ........ 11011011 01111100 */
                        decode_extract_decode_Fmt_13(ctx, &u.f_decode9, insn);
                        if (trans_SDBBP(ctx, &u.f_decode9)) {
                            return 4;
                        }
                        return 0;
                    case 0b11100:  /* 000000.. ........ 11100011 01111100 */
                        decode_extract_decode_Fmt_6(ctx, &u.f_decode5, insn);
                        switch ((insn >> 16) & 0b1111111111) {
                        case 0b0:  /* 00000000 00000000 11100011 01111100 */
                            if (trans_DERET(ctx, &u.f_decode5)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    case 0b11110:  /* 000000.. ........ 11110011 01111100 */
                        decode_extract_decode_Fmt_6(ctx, &u.f_decode5, insn);
                        switch ((insn >> 16) & 0b1111111111) {
                        case 0b0:  /* 00000000 00000000 11110011 01111100 */
                            if (trans_ERET(ctx, &u.f_decode5)) {
                                return 4;
                            }
                            return 0;
                        case 0b1:  /* 00000000 00000001 11110011 01111100 */
                            if (trans_ERETNC(ctx, &u.f_decode5)) {
                                return 4;
                            }
                            return 0;
                        }
                        return 0;
                    }
                    return 0;
                case 0b10000:  /* 000000.. ........ .....100 00111100 */
                    decode_extract_decode_Fmt_9(ctx, &u.f_decode8, insn);
                    switch ((insn >> 11) & 0b1) {
                    case 0b0:  /* 000000.. ........ ....0100 00111100 */
                        if (trans_TGEU(ctx, &u.f_decode8)) {
                            return 4;
                        }
                        return 0;
                    case 0b1:  /* 000000.. ........ ....1100 00111100 */
                        if (trans_TNE(ctx, &u.f_decode8)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                case 0b10100:  /* 000000.. ........ .....101 00111100 */
                    decode_extract_decode_Fmt_33(ctx, &u.f_decode27, insn);
                    switch ((insn >> 11) & 0b11111) {
                    case 0b1001:  /* 000000.. ........ 01001101 00111100 */
                        if (trans_MFC2(ctx, &u.f_decode27)) {
                            return 4;
                        }
                        return 0;
                    case 0b1011:  /* 000000.. ........ 01011101 00111100 */
                        if (trans_MTC2(ctx, &u.f_decode27)) {
                            return 4;
                        }
                        return 0;
                    case 0b10001:  /* 000000.. ........ 10001101 00111100 */
                        if (trans_MFHC2(ctx, &u.f_decode27)) {
                            return 4;
                        }
                        return 0;
                    case 0b10011:  /* 000000.. ........ 10011101 00111100 */
                        if (trans_MTHC2(ctx, &u.f_decode27)) {
                            return 4;
                        }
                        return 0;
                    case 0b11001:  /* 000000.. ........ 11001101 00111100 */
                        if (trans_CFC2(ctx, &u.f_decode27)) {
                            return 4;
                        }
                        return 0;
                    case 0b11011:  /* 000000.. ........ 11011101 00111100 */
                        if (trans_CTC2(ctx, &u.f_decode27)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                case 0b11100:  /* 000000.. ........ .....111 00111100 */
                    decode_extract_decode_Fmt_20(ctx, &u.f_decode17, insn);
                    switch ((insn >> 11) & 0b11111) {
                    case 0b1:  /* 000000.. ........ 00001111 00111100 */
                        if (trans_JALRC(ctx, &u.f_decode17)) {
                            return 4;
                        }
                        return 0;
                    case 0b11:  /* 000000.. ........ 00011111 00111100 */
                        if (trans_JALRCHB(ctx, &u.f_decode17)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                case 0b11101:  /* 000000.. ........ .....111 01111100 */
                    decode_extract_decode_Fmt_22(ctx, &u.f_decode18, insn);
                    switch (insn & 0b11111000001111100000000000) {
                    case 0b100000000000000:
                        /* 00000000 000..... 01000111 01111100 */
                        if (trans_DI(ctx, &u.f_decode18)) {
                            return 4;
                        }
                        return 0;
                    case 0b101000000000000:
                        /* 00000000 000..... 01010111 01111100 */
                        if (trans_EI(ctx, &u.f_decode18)) {
                            return 4;
                        }
                        return 0;
                    }
                    return 0;
                }
                return 0;
            }
            return 0;
        case 0b111:  /* 000000.. ........ ........ .....111 */
            switch ((insn >> 3) & 0b111) {
            case 0b0:  /* 000000.. ........ ........ ..000111 */
                decode_extract_decode_Fmt_10(ctx, &u.f_decode9, insn);
                if (trans_BREAK(ctx, &u.f_decode9)) {
                    return 4;
                }
                return 0;
            case 0b1:  /* 000000.. ........ ........ ..001111 */
                decode_extract_decode_Fmt_46(ctx, &u.f_decode35, insn);
                switch ((insn >> 6) & 0b111) {
                case 0b0:  /* 000000.. ........ .......0 00001111 */
                    if (trans_LSA(ctx, &u.f_decode35)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b11:  /* 000000.. ........ ........ ..011111 */
                decode_extract_decode_Fmt_24(ctx, &u.f_decode19, insn);
                switch ((insn >> 6) & 0b111) {
                case 0b0:  /* 000000.. ........ .......0 00011111 */
                    if (trans_ALIGN(ctx, &u.f_decode19)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b111:  /* 000000.. ........ ........ ..111111 */
                decode_extract_decode_Fmt_44(ctx, &u.f_decode9, insn);
                switch ((insn >> 22) & 0b1111) {
                case 0b0:  /* 00000000 00...... ........ ..111111 */
                    if (trans_SIGRIE(ctx, &u.f_decode9)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            }
            return 0;
        }
        return 0;
    case 0b100:  /* 000100.. ........ ........ ........ */
        switch ((insn >> 16) & 0b11111) {
        case 0b0:  /* 000100.. ...00000 ........ ........ */
            decode_extract_decode_Fmt_47(ctx, &u.f_decode13, insn);
            if (trans_LUI(ctx, &u.f_decode13)) {
                return 4;
            }
            return 0;
        default:  /* 000100.. ........ ........ ........ */
            decode_extract_decode_Fmt_4(ctx, &u.f_decode4, insn);
            if (trans_AUI(ctx, &u.f_decode4)) {
                return 4;
            }
            return 0;
        }
        return 0;
    case 0b101:  /* 000101.. ........ ........ ........ */
        decode_extract_decode_Fmt_36(ctx, &u.f_decode15, insn);
        if (trans_LBU(ctx, &u.f_decode15)) {
            return 4;
        }
        return 0;
    case 0b110:  /* 000110.. ........ ........ ........ */
        decode_extract_decode_Fmt_36(ctx, &u.f_decode15, insn);
        if (trans_SB(ctx, &u.f_decode15)) {
            return 4;
        }
        return 0;
    case 0b111:  /* 000111.. ........ ........ ........ */
        decode_extract_decode_Fmt_36(ctx, &u.f_decode15, insn);
        if (trans_LB(ctx, &u.f_decode15)) {
            return 4;
        }
        return 0;
    case 0b1000:  /* 001000.. ........ ........ ........ */
        switch ((insn >> 12) & 0b1111) {
        case 0b0:  /* 001000.. ........ 0000.... ........ */
            decode_extract_decode_Fmt_39(ctx, &u.f_decode15, insn);
            switch ((insn >> 11) & 0b1) {
            case 0b0:  /* 001000.. ........ 00000... ........ */
                if (trans_LWC2(ctx, &u.f_decode15)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b1:  /* 001000.. ........ 0001.... ........ */
            decode_extract_decode_Fmt_1(ctx, &u.f_decode1, insn);
            if (trans_LWP(ctx, &u.f_decode1)) {
                return 4;
            }
            return 0;
        case 0b10:  /* 001000.. ........ 0010.... ........ */
            decode_extract_decode_Fmt_39(ctx, &u.f_decode15, insn);
            switch ((insn >> 11) & 0b1) {
            case 0b0:  /* 001000.. ........ 00100... ........ */
                if (trans_LDC2(ctx, &u.f_decode15)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b101:  /* 001000.. ........ 0101.... ........ */
            decode_extract_decode_Fmt_0(ctx, &u.f_decode0, insn);
            if (trans_LWM32(ctx, &u.f_decode0)) {
                return 4;
            }
            return 0;
        case 0b110:  /* 001000.. ........ 0110.... ........ */
            decode_extract_decode_Fmt_16(ctx, &u.f_decode14, insn);
            switch ((insn >> 9) & 0b111) {
            case 0b0:  /* 001000.. ........ 0110000. ........ */
                if (trans_CACHE(ctx, &u.f_decode14)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b1000:  /* 001000.. ........ 1000.... ........ */
            decode_extract_decode_Fmt_39(ctx, &u.f_decode15, insn);
            switch ((insn >> 11) & 0b1) {
            case 0b0:  /* 001000.. ........ 10000... ........ */
                if (trans_SWC2(ctx, &u.f_decode15)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b1001:  /* 001000.. ........ 1001.... ........ */
            decode_extract_decode_Fmt_2(ctx, &u.f_decode2, insn);
            if (trans_SWP(ctx, &u.f_decode2)) {
                return 4;
            }
            return 0;
        case 0b1101:  /* 001000.. ........ 1101.... ........ */
            decode_extract_decode_Fmt_0(ctx, &u.f_decode0, insn);
            if (trans_SWM32(ctx, &u.f_decode0)) {
                return 4;
            }
            return 0;
        }
        return 0;
    case 0b1100:  /* 001100.. ........ ........ ........ */
        decode_extract_decode_Fmt_5(ctx, &u.f_decode4, insn);
        if (trans_ADDIU(ctx, &u.f_decode4)) {
            return 4;
        }
        return 0;
    case 0b1101:  /* 001101.. ........ ........ ........ */
        decode_extract_decode_Fmt_36(ctx, &u.f_decode15, insn);
        if (trans_LHU(ctx, &u.f_decode15)) {
            return 4;
        }
        return 0;
    case 0b1110:  /* 001110.. ........ ........ ........ */
        decode_extract_decode_Fmt_36(ctx, &u.f_decode15, insn);
        if (trans_SH(ctx, &u.f_decode15)) {
            return 4;
        }
        return 0;
    case 0b1111:  /* 001111.. ........ ........ ........ */
        decode_extract_decode_Fmt_36(ctx, &u.f_decode15, insn);
        if (trans_LH(ctx, &u.f_decode15)) {
            return 4;
        }
        return 0;
    case 0b10000:  /* 010000.. ........ ........ ........ */
        decode_extract_decode_Fmt_38(ctx, &u.f_decode31, insn);
        switch ((insn >> 21) & 0b11111) {
        case 0b1100:  /* 01000001 100..... ........ ........ */
            if (trans_SYNCI(ctx, &u.f_decode31)) {
                return 4;
            }
            return 0;
        }
        return 0;
    case 0b10001:  /* 010001.. ........ ........ ........ */
        switch ((insn >> 21) & 0b11111) {
        case 0b1000:  /* 01000101 000..... ........ ........ */
            decode_extract_decode_Fmt_7(ctx, &u.f_decode6, insn);
            if (trans_BC1EQZC(ctx, &u.f_decode6)) {
                return 4;
            }
            return 0;
        case 0b1001:  /* 01000101 001..... ........ ........ */
            decode_extract_decode_Fmt_7(ctx, &u.f_decode6, insn);
            if (trans_BC1NEZC(ctx, &u.f_decode6)) {
                return 4;
            }
            return 0;
        case 0b1010:  /* 01000101 010..... ........ ........ */
            decode_extract_decode_Fmt_8(ctx, &u.f_decode7, insn);
            if (trans_BC2EQZC(ctx, &u.f_decode7)) {
                return 4;
            }
            return 0;
        case 0b1011:  /* 01000101 011..... ........ ........ */
            decode_extract_decode_Fmt_8(ctx, &u.f_decode7, insn);
            if (trans_BC2NEZC(ctx, &u.f_decode7)) {
                return 4;
            }
            return 0;
        }
        return 0;
    case 0b10100:  /* 010100.. ........ ........ ........ */
        decode_extract_decode_Fmt_4(ctx, &u.f_decode4, insn);
        if (trans_ORI(ctx, &u.f_decode4)) {
            return 4;
        }
        return 0;
    case 0b10101:  /* 010101.. ........ ........ ........ */
        switch (insn & 0b111111) {
        case 0b11:  /* 010101.. ........ ........ ..000011 */
            decode_extract_decode_Fmt_14(ctx, &u.f_decode12, insn);
            switch ((insn >> 6) & 0b111) {
            case 0b0:  /* 010101.. ........ .......0 00000011 */
                if (trans_MAXAfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b101:  /* 010101.. ........ ........ ..000101 */
            decode_extract_decode_Fmt_40(ctx, &u.f_decode32, insn);
            if (trans_CMPcondnS(ctx, &u.f_decode32)) {
                return 4;
            }
            return 0;
        case 0b1011:  /* 010101.. ........ ........ ..001011 */
            decode_extract_decode_Fmt_14(ctx, &u.f_decode12, insn);
            switch ((insn >> 6) & 0b111) {
            case 0b0:  /* 010101.. ........ .......0 00001011 */
                if (trans_MAXfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b10101:  /* 010101.. ........ ........ ..010101 */
            decode_extract_decode_Fmt_40(ctx, &u.f_decode32, insn);
            if (trans_CMPcondnD(ctx, &u.f_decode32)) {
                return 4;
            }
            return 0;
        case 0b100000:  /* 010101.. ........ ........ ..100000 */
            decode_extract_decode_Fmt_28(ctx, &u.f_decode22, insn);
            switch (insn & 0b1111100111000000) {
            case 0b0:  /* 010101.. ........ 00000..0 00100000 */
                if (trans_RINTfmt(ctx, &u.f_decode22)) {
                    return 4;
                }
                return 0;
            case 0b1000000:  /* 010101.. ........ 00000..0 01100000 */
                if (trans_CLASSfmt(ctx, &u.f_decode22)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b100011:  /* 010101.. ........ ........ ..100011 */
            decode_extract_decode_Fmt_14(ctx, &u.f_decode12, insn);
            switch ((insn >> 6) & 0b111) {
            case 0b0:  /* 010101.. ........ .......0 00100011 */
                if (trans_MINAfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b101011:  /* 010101.. ........ ........ ..101011 */
            decode_extract_decode_Fmt_14(ctx, &u.f_decode12, insn);
            switch ((insn >> 6) & 0b111) {
            case 0b0:  /* 010101.. ........ .......0 00101011 */
                if (trans_MINfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b110000:  /* 010101.. ........ ........ ..110000 */
            decode_extract_decode_Fmt_21(ctx, &u.f_decode12, insn);
            switch (insn & 0b10011000000) {
            case 0b0:  /* 010101.. ........ .....0.. 00110000 */
                if (trans_ADDfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            case 0b1000000:  /* 010101.. ........ .....0.. 01110000 */
                if (trans_SUBfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            case 0b10000000:  /* 010101.. ........ .....0.. 10110000 */
                if (trans_MULfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            case 0b11000000:  /* 010101.. ........ .....0.. 11110000 */
                if (trans_DIVfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b111000:  /* 010101.. ........ ........ ..111000 */
            decode_extract_decode_Fmt_14(ctx, &u.f_decode12, insn);
            switch ((insn >> 6) & 0b111) {
            case 0b0:  /* 010101.. ........ .......0 00111000 */
                if (trans_SELEQZfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            case 0b1:  /* 010101.. ........ .......0 01111000 */
                if (trans_SELNEQZfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            case 0b10:  /* 010101.. ........ .......0 10111000 */
                if (trans_SELfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            case 0b110:  /* 010101.. ........ .......1 10111000 */
                if (trans_MADDFfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            case 0b111:  /* 010101.. ........ .......1 11111000 */
                if (trans_MSUBFfmt(ctx, &u.f_decode12)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b111011:  /* 010101.. ........ ........ ..111011 */
            switch (insn & 0b1001111111000000) {
            case 0b0:  /* 010101.. ........ 0..00000 00111011 */
                decode_extract_decode_Fmt_32(ctx, &u.f_decode26, insn);
                switch ((insn >> 13) & 0b11) {
                case 0b1:  /* 010101.. ........ 00100000 00111011 */
                    if (trans_MFC1(ctx, &u.f_decode26)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b1000000:  /* 010101.. ........ 0..00000 01111011 */
                decode_extract_decode_Fmt_26(ctx, &u.f_decode21, insn);
                if (trans_MOVfmt(ctx, &u.f_decode21)) {
                    return 4;
                }
                return 0;
            case 0b100000000:  /* 010101.. ........ 0..00001 00111011 */
                decode_extract_decode_Fmt_27(ctx, &u.f_decode21, insn);
                switch ((insn >> 13) & 0b1) {
                case 0b0:  /* 010101.. ........ 0.000001 00111011 */
                    if (trans_CVTLfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b1000000000:  /* 010101.. ........ 0..00010 00111011 */
                decode_extract_decode_Fmt_27(ctx, &u.f_decode21, insn);
                switch ((insn >> 13) & 0b1) {
                case 0b0:  /* 010101.. ........ 0.000010 00111011 */
                    if (trans_RSQRTfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b1100000000:  /* 010101.. ........ 0..00011 00111011 */
                decode_extract_decode_Fmt_27(ctx, &u.f_decode21, insn);
                switch ((insn >> 13) & 0b1) {
                case 0b0:  /* 010101.. ........ 0.000011 00111011 */
                    if (trans_FLOORLfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                case 0b1:  /* 010101.. ........ 0.100011 00111011 */
                    if (trans_TRUNCLfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b1101000000:  /* 010101.. ........ 0..00011 01111011 */
                decode_extract_decode_Fmt_26(ctx, &u.f_decode21, insn);
                if (trans_ABSfmt(ctx, &u.f_decode21)) {
                    return 4;
                }
                return 0;
            case 0b100000000000:  /* 010101.. ........ 0..01000 00111011 */
                decode_extract_decode_Fmt_32(ctx, &u.f_decode26, insn);
                switch ((insn >> 13) & 0b11) {
                case 0b1:  /* 010101.. ........ 00101000 00111011 */
                    if (trans_MTC1(ctx, &u.f_decode26)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b100100000000:  /* 010101.. ........ 0..01001 00111011 */
                decode_extract_decode_Fmt_27(ctx, &u.f_decode21, insn);
                switch ((insn >> 13) & 0b1) {
                case 0b0:  /* 010101.. ........ 0.001001 00111011 */
                    if (trans_CVTWfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b101000000000:  /* 010101.. ........ 0..01010 00111011 */
                decode_extract_decode_Fmt_27(ctx, &u.f_decode21, insn);
                switch ((insn >> 13) & 0b1) {
                case 0b0:  /* 010101.. ........ 0.001010 00111011 */
                    if (trans_SQRTfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b101100000000:  /* 010101.. ........ 0..01011 00111011 */
                decode_extract_decode_Fmt_27(ctx, &u.f_decode21, insn);
                switch ((insn >> 13) & 0b1) {
                case 0b0:  /* 010101.. ........ 0.001011 00111011 */
                    if (trans_FLOORWfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                case 0b1:  /* 010101.. ........ 0.101011 00111011 */
                    if (trans_TRUNCWfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b101101000000:  /* 010101.. ........ 0..01011 01111011 */
                decode_extract_decode_Fmt_26(ctx, &u.f_decode21, insn);
                if (trans_NEGfmt(ctx, &u.f_decode21)) {
                    return 4;
                }
                return 0;
            case 0b1000000000000:  /* 010101.. ........ 0..10000 00111011 */
                decode_extract_decode_Fmt_32(ctx, &u.f_decode26, insn);
                switch ((insn >> 13) & 0b11) {
                case 0b0:  /* 010101.. ........ 00010000 00111011 */
                    if (trans_CFC1(ctx, &u.f_decode26)) {
                        return 4;
                    }
                    return 0;
                case 0b1:  /* 010101.. ........ 00110000 00111011 */
                    if (trans_MFHC1(ctx, &u.f_decode26)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b1001000000000:  /* 010101.. ........ 0..10010 00111011 */
                decode_extract_decode_Fmt_27(ctx, &u.f_decode21, insn);
                switch ((insn >> 13) & 0b1) {
                case 0b0:  /* 010101.. ........ 0.010010 00111011 */
                    if (trans_RECIPfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b1001100000000:  /* 010101.. ........ 0..10011 00111011 */
                decode_extract_decode_Fmt_27(ctx, &u.f_decode21, insn);
                switch ((insn >> 13) & 0b1) {
                case 0b0:  /* 010101.. ........ 0.010011 00111011 */
                    if (trans_CEILLfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                case 0b1:  /* 010101.. ........ 0.110011 00111011 */
                    if (trans_ROUNDLfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b1001101000000:  /* 010101.. ........ 0..10011 01111011 */
                decode_extract_decode_Fmt_26(ctx, &u.f_decode21, insn);
                if (trans_CVTDfmt(ctx, &u.f_decode21)) {
                    return 4;
                }
                return 0;
            case 0b1100000000000:  /* 010101.. ........ 0..11000 00111011 */
                decode_extract_decode_Fmt_32(ctx, &u.f_decode26, insn);
                switch ((insn >> 13) & 0b11) {
                case 0b0:  /* 010101.. ........ 00011000 00111011 */
                    if (trans_CTC1(ctx, &u.f_decode26)) {
                        return 4;
                    }
                    return 0;
                case 0b1:  /* 010101.. ........ 00111000 00111011 */
                    if (trans_MTHC1(ctx, &u.f_decode26)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b1101100000000:  /* 010101.. ........ 0..11011 00111011 */
                decode_extract_decode_Fmt_27(ctx, &u.f_decode21, insn);
                switch ((insn >> 13) & 0b1) {
                case 0b0:  /* 010101.. ........ 0.011011 00111011 */
                    if (trans_CEILWfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                case 0b1:  /* 010101.. ........ 0.111011 00111011 */
                    if (trans_ROUNDWfmt(ctx, &u.f_decode21)) {
                        return 4;
                    }
                    return 0;
                }
                return 0;
            case 0b1101101000000:  /* 010101.. ........ 0..11011 01111011 */
                decode_extract_decode_Fmt_26(ctx, &u.f_decode21, insn);
                if (trans_CVTSfmt(ctx, &u.f_decode21)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        }
        return 0;
    case 0b11000:  /* 011000.. ........ ........ ........ */
        switch ((insn >> 9) & 0b1111111) {
        case 0b1000:  /* 011000.. ........ 0001000. ........ */
            decode_extract_decode_Fmt_35(ctx, &u.f_decode29, insn);
            switch (insn & 0b1111) {
            case 0b0:  /* 011000.. ........ 0001000. ....0000 */
                if (trans_LLWP(ctx, &u.f_decode29)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b10000:  /* 011000.. ........ 0010000. ........ */
            decode_extract_decode_Fmt_29(ctx, &u.f_decode23, insn);
            if (trans_PREF(ctx, &u.f_decode23)) {
                return 4;
            }
            return 0;
        case 0b11000:  /* 011000.. ........ 0011000. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_LL(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b110000:  /* 011000.. ........ 0110000. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_LBUE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b110001:  /* 011000.. ........ 0110001. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_LHUE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b110010:  /* 011000.. ........ 0110010. ........ */
            decode_extract_decode_Fmt_35(ctx, &u.f_decode29, insn);
            switch (insn & 0b1111) {
            case 0b0:  /* 011000.. ........ 0110010. ....0000 */
                if (trans_LLWPE(ctx, &u.f_decode29)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b110100:  /* 011000.. ........ 0110100. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_LBE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b110101:  /* 011000.. ........ 0110101. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_LHE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b110110:  /* 011000.. ........ 0110110. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_LLE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b110111:  /* 011000.. ........ 0110111. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_LWE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b1001000:  /* 011000.. ........ 1001000. ........ */
            decode_extract_decode_Fmt_35(ctx, &u.f_decode29, insn);
            switch (insn & 0b1111) {
            case 0b0:  /* 011000.. ........ 1001000. ....0000 */
                if (trans_SCWP(ctx, &u.f_decode29)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b1010000:  /* 011000.. ........ 1010000. ........ */
            decode_extract_decode_Fmt_35(ctx, &u.f_decode29, insn);
            switch (insn & 0b1111) {
            case 0b0:  /* 011000.. ........ 1010000. ....0000 */
                if (trans_SCWPE(ctx, &u.f_decode29)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        case 0b1010010:  /* 011000.. ........ 1010010. ........ */
            decode_extract_decode_Fmt_29(ctx, &u.f_decode23, insn);
            if (trans_PREFE(ctx, &u.f_decode23)) {
                return 4;
            }
            return 0;
        case 0b1010011:  /* 011000.. ........ 1010011. ........ */
            decode_extract_decode_Fmt_16(ctx, &u.f_decode14, insn);
            if (trans_CACHEE(ctx, &u.f_decode14)) {
                return 4;
            }
            return 0;
        case 0b1010100:  /* 011000.. ........ 1010100. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_SBE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b1010101:  /* 011000.. ........ 1010101. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_SHE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b1010110:  /* 011000.. ........ 1010110. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_SCE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b1010111:  /* 011000.. ........ 1010111. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_SWE(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        case 0b1011000:  /* 011000.. ........ 1011000. ........ */
            decode_extract_decode_Fmt_17(ctx, &u.f_decode15, insn);
            if (trans_SC(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        }
        return 0;
    case 0b11100:  /* 011100.. ........ ........ ........ */
        decode_extract_decode_Fmt_4(ctx, &u.f_decode4, insn);
        if (trans_XORI(ctx, &u.f_decode4)) {
            return 4;
        }
        return 0;
    case 0b11101:  /* 011101.. ........ ........ ........ */
        if (((insn >> 16) & 0b11111) >= ((insn >> 21) & 0b11111)) {
            decode_extract_decode_Fmt_18(ctx, &u.f_decode11, insn);
            if (trans_BOVC(ctx, &u.f_decode11)) {
                return 4;
            }
            return 0;
        } else {
            if ((insn >> 16) & 0b11111) {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BEQC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            } else {
                decode_extract_decode_Fmt_11(ctx, &u.f_decode10, insn);
                if (trans_BEQZALC(ctx, &u.f_decode10)) {
                    return 4;
                }
                return 0;
            }
        }
        return 0;
    case 0b11110:  /* 011110.. ........ ........ ........ */
        switch ((insn >> 19) & 0b11) {
        case 0b0:  /* 011110.. ...00... ........ ........ */
            decode_extract_decode_Fmt_42(ctx, &u.f_decode13, insn);
            if (trans_ADDIUPC(ctx, &u.f_decode13)) {
                return 4;
            }
            return 0;
        case 0b1:  /* 011110.. ...01... ........ ........ */
            decode_extract_decode_Fmt_45(ctx, &u.f_decode10, insn);
            if (trans_LWPC(ctx, &u.f_decode10)) {
                return 4;
            }
            return 0;
        case 0b11:  /* 011110.. ...11... ........ ........ */
            decode_extract_decode_Fmt_15(ctx, &u.f_decode13, insn);
            switch ((insn >> 16) & 0b111) {
            case 0b110:  /* 011110.. ...11110 ........ ........ */
                if (trans_AUIPC(ctx, &u.f_decode13)) {
                    return 4;
                }
                return 0;
            case 0b111:  /* 011110.. ...11111 ........ ........ */
                if (trans_ALUIPC(ctx, &u.f_decode13)) {
                    return 4;
                }
                return 0;
            }
            return 0;
        }
        return 0;
    case 0b11111:  /* 011111.. ........ ........ ........ */
        if (((insn >> 16) & 0b11111) >= ((insn >> 21) & 0b11111)) {
            decode_extract_decode_Fmt_18(ctx, &u.f_decode11, insn);
            if (trans_BNVC(ctx, &u.f_decode11)) {
                return 4;
            }
            return 0;
        } else {
            if ((insn >> 16) & 0b11111) {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BNEC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            } else {
                decode_extract_decode_Fmt_11(ctx, &u.f_decode10, insn);
                if (trans_BNEZALC(ctx, &u.f_decode10)) {
                    return 4;
                }
                return 0;
            }
        }
        return 0;
    case 0b100000:  /* 100000.. ........ ........ ........ */
        switch ((insn >> 21) & 0b11111) {
        case 0b0:  /* 10000000 000..... ........ ........ */
            decode_extract_decode_Fmt_23(ctx, &u.f_decode10, insn);
            if (trans_JIC(ctx, &u.f_decode10)) {
                return 4;
            }
            return 0;
        default:  /* 100000.. ........ ........ ........ */
            decode_extract_decode_Fmt_48(ctx, &u.f_decode36, insn);
            if (trans_BEQZC(ctx, &u.f_decode36)) {
                return 4;
            }
            return 0;
        }
        return 0;
    case 0b100100:  /* 100100.. ........ ........ ........ */
        decode_extract_decode_Fmt_5(ctx, &u.f_decode4, insn);
        if (trans_SLTI(ctx, &u.f_decode4)) {
            return 4;
        }
        return 0;
    case 0b100101:  /* 100101.. ........ ........ ........ */
        decode_extract_decode_Fmt_19(ctx, &u.f_decode16, insn);
        if (trans_BC(ctx, &u.f_decode16)) {
            return 4;
        }
        return 0;
    case 0b100110:  /* 100110.. ........ ........ ........ */
        decode_extract_decode_Fmt_37(ctx, &u.f_decode30, insn);
        if (trans_SWC1(ctx, &u.f_decode30)) {
            return 4;
        }
        return 0;
    case 0b100111:  /* 100111.. ........ ........ ........ */
        decode_extract_decode_Fmt_37(ctx, &u.f_decode30, insn);
        if (trans_LWC1(ctx, &u.f_decode30)) {
            return 4;
        }
        return 0;
    case 0b101000:  /* 101000.. ........ ........ ........ */
        /* 101000.. ........ ........ ........ */
        switch ((insn >> 21) & 0b11111) {
        case 0b0:  /* 10100000 000..... ........ ........ */
            decode_extract_decode_Fmt_23(ctx, &u.f_decode10, insn);
            if (trans_JIALC(ctx, &u.f_decode10)) {
                return 4;
            }
            return 0;
        default:  /* 101000.. ........ ........ ........ */
            decode_extract_decode_Fmt_48(ctx, &u.f_decode36, insn);
            if (trans_BNEZC(ctx, &u.f_decode36)) {
                return 4;
            }
            return 0;
        }
        return 0;
    case 0b101100:  /* 101100.. ........ ........ ........ */
        decode_extract_decode_Fmt_5(ctx, &u.f_decode4, insn);
        if (trans_SLTIU(ctx, &u.f_decode4)) {
            return 4;
        }
        return 0;
    case 0b101101:  /* 101101.. ........ ........ ........ */
        decode_extract_decode_Fmt_19(ctx, &u.f_decode16, insn);
        if (trans_BALC(ctx, &u.f_decode16)) {
            return 4;
        }
        return 0;
    case 0b101110:  /* 101110.. ........ ........ ........ */
        decode_extract_decode_Fmt_37(ctx, &u.f_decode30, insn);
        if (trans_SDC1(ctx, &u.f_decode30)) {
            return 4;
        }
        return 0;
    case 0b101111:  /* 101111.. ........ ........ ........ */
        decode_extract_decode_Fmt_37(ctx, &u.f_decode30, insn);
        if (trans_LDC1(ctx, &u.f_decode30)) {
            return 4;
        }
        return 0;
    case 0b110000:  /* 110000.. ........ ........ ........ */
        switch ((insn >> 16) & 0b11111) {
        case 0b0:
            decode_extract_decode_Fmt_11(ctx, &u.f_decode10, insn);
            if (trans_BLEZALC(ctx, &u.f_decode10)) {
                return 4;
            }
            return 0;
        default:
            if (((insn >> 16) & 0b11111) == ((insn >> 21) & 0b11111)) {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BGEZALC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            } else {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BGEUC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            }
        }
        return 0;
    case 0b110100:  /* 110100.. ........ ........ ........ */
        decode_extract_decode_Fmt_4(ctx, &u.f_decode4, insn);
        if (trans_ANDI(ctx, &u.f_decode4)) {
            return 4;
        }
        return 0;
    case 0b110101:  /* 110101.. ........ ........ ........ */
        switch ((insn >> 16) & 0b11111) {
        case 0b0:  /* 110101.. ...00000 ........ ........ */
            decode_extract_decode_Fmt_11(ctx, &u.f_decode10, insn);
            if (trans_BGTZC(ctx, &u.f_decode10)) {
                return 4;
            }
            return 0;
        default:
            if (((insn >> 16) & 0b11111) == ((insn >> 21) & 0b11111)) {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BLTZC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            } else {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BLTC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            }
        }
        return 0;
    case 0b110110:  /* 110110.. ........ ........ ........ */
        decode_extract_decode_Fmt_39(ctx, &u.f_decode15, insn);
        switch ((insn >> 11) & 0b11111) {
        case 0b10100:  /* 110110.. ........ 10100... ........ */
            if (trans_SDC2(ctx, &u.f_decode15)) {
                return 4;
            }
            return 0;
        }
        return 0;
    case 0b111000:  /* 111000.. ........ ........ ........ */
        switch ((insn >> 16) & 0b11111) {
        case 0b0:
            decode_extract_decode_Fmt_11(ctx, &u.f_decode10, insn);
            if (trans_BGTZALC(ctx, &u.f_decode10)) {
                return 4;
            }
            return 0;
        default:
            if (((insn >> 16) & 0b11111) == ((insn >> 21) & 0b11111)) {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BLTZALC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            } else {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BLTUC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            }
        }
        return 0;
    case 0b111101:  /* 111101.. ........ ........ ........ */
        switch ((insn >> 16) & 0b11111) {
        case 0b0:  /* 111101.. ...00000 ........ ........ */
            decode_extract_decode_Fmt_11(ctx, &u.f_decode10, insn);
            if (trans_BLEZC(ctx, &u.f_decode10)) {
                return 4;
            }
            return 0;
        default:
            if (((insn >> 16) & 0b11111) == ((insn >> 21) & 0b11111)) {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BGEZC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            } else {
                decode_extract_decode_Fmt_12(ctx, &u.f_decode11, insn);
                if (trans_BGEC(ctx, &u.f_decode11)) {
                    return 4;
                }
                return 0;
            }
        }
        return 0;
    case 0b111110:  /* 111110.. ........ ........ ........ */
        decode_extract_decode_Fmt_36(ctx, &u.f_decode15, insn);
        if (trans_SW(ctx, &u.f_decode15)) {
            return 4;
        }
        return 0;
    case 0b111111:  /* 111111.. ........ ........ ........ */
        decode_extract_decode_Fmt_36(ctx, &u.f_decode15, insn);
        if (trans_LW(ctx, &u.f_decode15)) {
            return 4;
        }
        return 0;
    }
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

static bool trans_LWP(disassemble_info *info, arg_LWP *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rd);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LWP",
     alias, alias1, a->offset));
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

static bool trans_LWM32(disassemble_info *info, arg_LWM32 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->reglist);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LWM32",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SWM32(disassemble_info *info, arg_SWM32 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->reglist);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SWM32",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SWP(disassemble_info *info, arg_SWP *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rs1);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SWP",
     alias, alias1, a->offset));
    return true;
}

static bool trans_ABSfmt(disassemble_info *info, arg_ABSfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "ABSfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_ADD(disassemble_info *info, arg_ADD *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "ADD",
     alias, alias1, alias2));
    return true;
}

static bool trans_ADDfmt(disassemble_info *info, arg_ADDfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d", "" "ADDfmt",
     a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_ADDIU(disassemble_info *info, arg_ADDIU *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "ADDIU",
     alias, alias1, a->imm));
    return true;
}

static bool trans_ADDIUPC(disassemble_info *info, arg_ADDIUPC *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "ADDIUPC",
     alias, a->imm));
    return true;
}

static bool trans_ADDU(disassemble_info *info, arg_ADDU *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "ADDU",
     alias, alias1, alias2));
    return true;
}

static bool trans_ALIGN(disassemble_info *info, arg_ALIGN *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    char alias3[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    getAlias(alias3, a->bp);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s, %s", "" "ALIGN",
     alias, alias1, alias2, alias3));
    return true;
}

static bool trans_ALUIPC(disassemble_info *info, arg_ALUIPC *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "ALUIPC",
     alias, a->imm));
    return true;
}

static bool trans_AND(disassemble_info *info, arg_AND *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "AND",
     alias, alias1, alias2));
    return true;
}

static bool trans_ANDI(disassemble_info *info, arg_ANDI *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "ANDI",
     alias, alias1, a->imm));
    return true;
}

static bool trans_AUI(disassemble_info *info, arg_AUI *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "AUI",
     alias, alias1, a->imm));
    return true;
}

static bool trans_AUIPC(disassemble_info *info, arg_AUIPC *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "AUIPC",
     alias, a->imm));
    return true;
}

static bool trans_BALC(disassemble_info *info, arg_BALC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BALC",
     a->offset));
    return true;
}

static bool trans_BC1EQZC(disassemble_info *info, arg_BC1EQZC *a)
{
    char alias[5];
    getAlias(alias, a->ft);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "BC1EQZC",
     alias, a->offset));
    return true;
}

static bool trans_BC1NEZC(disassemble_info *info, arg_BC1NEZC *a)
{
    char alias[5];
    getAlias(alias, a->ft);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "BC1NEZC",
     alias, a->offset));
    return true;
}

static bool trans_BC2EQZC(disassemble_info *info, arg_BC2EQZC *a)
{
    char alias[5];
    getAlias(alias, a->ct);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "BC2EQZC",
     alias, a->offset));
    return true;
}

static bool trans_BC2NEZC(disassemble_info *info, arg_BC2NEZC *a)
{
    char alias[5];
    getAlias(alias, a->ct);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "BC2NEZC",
     alias, a->offset));
    return true;
}

static bool trans_BLEZALC(disassemble_info *info, arg_BLEZALC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BLEZALC", a->offset));
    return true;
}

static bool trans_BGEZALC(disassemble_info *info, arg_BGEZALC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BGEZALC", a->offset));
    return true;
}

static bool trans_BGTZALC(disassemble_info *info, arg_BGTZALC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BGTZALC", a->offset));
    return true;
}

static bool trans_BLTZALC(disassemble_info *info, arg_BLTZALC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BLTZALC", a->offset));
    return true;
}

static bool trans_BEQZALC(disassemble_info *info, arg_BEQZALC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BEQZALC", a->offset));
    return true;
}

static bool trans_BNEZALC(disassemble_info *info, arg_BNEZALC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BNEZALC", a->offset));
    return true;
}

static bool trans_BLEZC(disassemble_info *info, arg_BLEZC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BLEZC", a->offset));
    return true;
}

static bool trans_BGEZC(disassemble_info *info, arg_BGEZC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BGEZC", a->offset));
    return true;
}

static bool trans_BGEC(disassemble_info *info, arg_BGEC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BGEC", a->offset));
    return true;
}

static bool trans_BGTZC(disassemble_info *info, arg_BGTZC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BGTZC", a->offset));
    return true;
}

static bool trans_BLTZC(disassemble_info *info, arg_BLTZC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BLTZC", a->offset));
    return true;
}

static bool trans_BLTC(disassemble_info *info, arg_BLTC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BLTC", a->offset));
    return true;
}

static bool trans_BGEUC(disassemble_info *info, arg_BGEUC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BGEUC", a->offset));
    return true;
}

static bool trans_BLTUC(disassemble_info *info, arg_BLTUC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BLTUC", a->offset));
    return true;
}

static bool trans_BEQC(disassemble_info *info, arg_BEQC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BEQC", a->offset));
    return true;
}

static bool trans_BNEC(disassemble_info *info, arg_BNEC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BNEC", a->offset));
    return true;
}

static bool trans_BEQZC(disassemble_info *info, arg_BEQZC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BEQZC", a->offset));
    return true;
}

static bool trans_BNEZC(disassemble_info *info, arg_BNEZC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BNEZC", a->offset));
    return true;
}

static bool trans_BC(disassemble_info *info, arg_BC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BC", a->offset));
    return true;
}

static bool trans_BREAK(disassemble_info *info, arg_BREAK *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "BREAK", a->code));
    return true;
}

static bool trans_BITSWAP(disassemble_info *info, arg_BITSWAP *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "BITSWAP",
     alias, alias1));
    return true;
}

static bool trans_BOVC(disassemble_info *info, arg_BOVC *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "BOVC",
     alias, alias1, a->offset));
    return true;
}

static bool trans_BNVC(disassemble_info *info, arg_BNVC *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "BNVC",
     alias, alias1, a->offset));
    return true;
}

static bool trans_CACHE(disassemble_info *info, arg_CACHE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->op);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "CACHE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_CACHEE(disassemble_info *info, arg_CACHEE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->op);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "CACHEE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_CEILLfmt(disassemble_info *info, arg_CEILLfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "CEILLfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_CEILWfmt(disassemble_info *info, arg_CEILWfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "CEILWfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_CFC1(disassemble_info *info, arg_CFC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->fs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "CFC1",
     alias, alias1));
    return true;
}

static bool trans_CFC2(disassemble_info *info, arg_CFC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->impl);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "CFC2",
     alias, alias1));
    return true;
}

static bool trans_CLASSfmt(disassemble_info *info, arg_CLASSfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "CLASSfmt",
     a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_CLO(disassemble_info *info, arg_CLO *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "CLO",
     alias, alias1));
    return true;
}

static bool trans_CLZ(disassemble_info *info, arg_CLZ *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "CLZ",
     alias, alias1));
    return true;
}

static bool trans_CMPcondnS(disassemble_info *info, arg_CMPcondnS *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    char alias3[5];
    getAlias(alias, a->ft);
    getAlias(alias1, a->fs);
    getAlias(alias2, a->fd);
    getAlias(alias3, a->condn);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s, %s",
     "" "CMPcondnS", alias, alias1, alias2, alias3));
    return true;
}

static bool trans_CMPcondnD(disassemble_info *info, arg_CMPcondnD *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    char alias3[5];
    getAlias(alias, a->ft);
    getAlias(alias1, a->fs);
    getAlias(alias2, a->fd);
    getAlias(alias3, a->condn);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s, %s",
     "" "CMPcondnD", alias, alias1, alias2, alias3));
    return true;
}

static bool trans_COP2(disassemble_info *info, arg_COP2 *a)
{
    char alias[5];
    getAlias(alias, a->cofun);
    (info->fprintf_func(info->stream, "%-9s " "%s", "" "COP2", alias));
    return true;
}

static bool trans_CTC1(disassemble_info *info, arg_CTC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->fs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "CTC1",
     alias, alias1));
    return true;
}

static bool trans_CTC2(disassemble_info *info, arg_CTC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->impl);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "CTC2",
     alias, alias1));
    return true;
}

static bool trans_CVTDfmt(disassemble_info *info, arg_CVTDfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "CVTDfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_CVTLfmt(disassemble_info *info, arg_CVTLfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "CVTLfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_CVTSfmt(disassemble_info *info, arg_CVTSfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "CVTSfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_CVTWfmt(disassemble_info *info, arg_CVTWfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "CVTWfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_DERET(disassemble_info *info, arg_DERET *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "DERET"));
    return true;
}

static bool trans_DI(disassemble_info *info, arg_DI *a)
{
    char alias[5];
    getAlias(alias, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s", "" "DI", alias));
    return true;
}

static bool trans_DIVfmt(disassemble_info *info, arg_DIVfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d", "" "DIVfmt",
     a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_DIV(disassemble_info *info, arg_DIV *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "DIV",
     alias, alias1, alias2));
    return true;
}

static bool trans_MOD(disassemble_info *info, arg_MOD *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MOD",
     alias, alias1, alias2));
    return true;
}

static bool trans_DIVU(disassemble_info *info, arg_DIVU *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "DIVU",
     alias, alias1, alias2));
    return true;
}

static bool trans_MODU(disassemble_info *info, arg_MODU *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MODU",
     alias, alias1, alias2));
    return true;
}

static bool trans_DVP(disassemble_info *info, arg_DVP *a)
{
    char alias[5];
    getAlias(alias, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s", "" "DVP", alias));
    return true;
}

static bool trans_EHB(disassemble_info *info, arg_EHB *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "EHB"));
    return true;
}

static bool trans_EI(disassemble_info *info, arg_EI *a)
{
    char alias[5];
    getAlias(alias, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s", "" "EI", alias));
    return true;
}

static bool trans_ERET(disassemble_info *info, arg_ERET *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "ERET"));
    return true;
}

static bool trans_ERETNC(disassemble_info *info, arg_ERETNC *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "ERETNC"));
    return true;
}

static bool trans_EXT(disassemble_info *info, arg_EXT *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    char alias3[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->msbd);
    getAlias(alias3, a->lsb);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s, %s", "" "EXT",
     alias, alias1, alias2, alias3));
    return true;
}

static bool trans_EVP(disassemble_info *info, arg_EVP *a)
{
    char alias[5];
    getAlias(alias, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s", "" "EVP", alias));
    return true;
}

static bool trans_FLOORLfmt(disassemble_info *info, arg_FLOORLfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "FLOORLfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_FLOORWfmt(disassemble_info *info, arg_FLOORWfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "FLOORWfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_INS(disassemble_info *info, arg_INS *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    char alias3[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->msbd);
    getAlias(alias3, a->lsb);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s, %s", "" "INS",
     alias, alias1, alias2, alias3));
    return true;
}

static bool trans_JALRC(disassemble_info *info, arg_JALRC *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "JALRC",
     alias, alias1));
    return true;
}

static bool trans_JALRCHB(disassemble_info *info, arg_JALRCHB *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "JALRCHB",
     alias, alias1));
    return true;
}

static bool trans_JIALC(disassemble_info *info, arg_JIALC *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "JIALC",
     alias, a->offset));
    return true;
}

static bool trans_JIC(disassemble_info *info, arg_JIC *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "JIC",
     alias, a->offset));
    return true;
}

static bool trans_LB(disassemble_info *info, arg_LB *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LB",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LBE(disassemble_info *info, arg_LBE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LBE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LBU(disassemble_info *info, arg_LBU *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LBU",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LBUE(disassemble_info *info, arg_LBUE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LBUE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LDC1(disassemble_info *info, arg_LDC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->ft);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LDC1",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LDC2(disassemble_info *info, arg_LDC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LDC2",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LH(disassemble_info *info, arg_LH *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LH",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LHE(disassemble_info *info, arg_LHE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LHE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LHU(disassemble_info *info, arg_LHU *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LHU",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LHUE(disassemble_info *info, arg_LHUE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LHUE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LL(disassemble_info *info, arg_LL *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LL",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LLE(disassemble_info *info, arg_LLE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LLE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LLWP(disassemble_info *info, arg_LLWP *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "LLWP",
     alias, alias1, alias2));
    return true;
}

static bool trans_LLWPE(disassemble_info *info, arg_LLWPE *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "LLWPE",
     alias, alias1, alias2));
    return true;
}

static bool trans_LSA(disassemble_info *info, arg_LSA *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    char alias3[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    getAlias(alias3, a->sa);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s, %s", "" "LSA",
     alias, alias1, alias2, alias3));
    return true;
}

static bool trans_LUI(disassemble_info *info, arg_LUI *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, 0x%x", "" "LUI",
     alias, a->imm));
    return true;
}

static bool trans_LW(disassemble_info *info, arg_LW *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LW",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LWC1(disassemble_info *info, arg_LWC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->ft);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LWC1",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LWC2(disassemble_info *info, arg_LWC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LWC2",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LWE(disassemble_info *info, arg_LWE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "LWE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_LWPC(disassemble_info *info, arg_LWPC *a)
{
    char alias[5];
    getAlias(alias, a->rt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "LWPC",
     alias, a->offset));
    return true;
}

static bool trans_MADDFfmt(disassemble_info *info, arg_MADDFfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d",
     "" "MADDFfmt", a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_MSUBFfmt(disassemble_info *info, arg_MSUBFfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d",
     "" "MSUBFfmt", a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_MAXfmt(disassemble_info *info, arg_MAXfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d",
     "" "MAXfmt", a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_MINfmt(disassemble_info *info, arg_MINfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d",
     "" "MINfmt", a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_MAXAfmt(disassemble_info *info, arg_MAXAfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d",
     "" "MAXAfmt", a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_MINAfmt(disassemble_info *info, arg_MINAfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d",
     "" "MINAfmt", a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_MFC0(disassemble_info *info, arg_MFC0 *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s",
     "" "MFC0", alias, alias1, alias2));
    return true;
}

static bool trans_MFC1(disassemble_info *info, arg_MFC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->fs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "MFC1",
     alias, alias1));
    return true;
}

static bool trans_MFC2(disassemble_info *info, arg_MFC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->impl);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "MFC2",
     alias, alias1));
    return true;
}

static bool trans_MFHC0(disassemble_info *info, arg_MFHC0 *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->sel);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MFHC0",
     alias, alias1, alias2));
    return true;
}

static bool trans_MFHC1(disassemble_info *info, arg_MFHC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->fs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "MFHC1",
     alias, alias1));
    return true;
}

static bool trans_MFHC2(disassemble_info *info, arg_MFHC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->impl);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "MFHC2",
     alias, alias1));
    return true;
}

static bool trans_MOVfmt(disassemble_info *info, arg_MOVfmt *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->ft);
    getAlias(alias1, a->fs);
    getAlias(alias2, a->fmt);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MOVfmt",
     alias, alias1, alias2));
    return true;
}

static bool trans_MTC0(disassemble_info *info, arg_MTC0 *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->sel);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MTC0",
     alias, alias1, alias2));
    return true;
}

static bool trans_MTC1(disassemble_info *info, arg_MTC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->fs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "MTC1",
     alias, alias1));
    return true;
}

static bool trans_MTC2(disassemble_info *info, arg_MTC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->impl);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "MTC2",
     alias, alias1));
    return true;
}

static bool trans_MTHC0(disassemble_info *info, arg_MTHC0 *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->sel);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MTHC0",
     alias, alias1, alias2));
    return true;
}

static bool trans_MTHC1(disassemble_info *info, arg_MTHC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->fs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "MTHC1",
     alias, alias1));
    return true;
}

static bool trans_MTHC2(disassemble_info *info, arg_MTHC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->impl);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "MTHC2",
     alias, alias1));
    return true;
}

static bool trans_MUL(disassemble_info *info, arg_MUL *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MUL",
     alias, alias1, alias2));
    return true;
}

static bool trans_MUH(disassemble_info *info, arg_MUH *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MUH",
     alias, alias1, alias2));
    return true;
}

static bool trans_MULU(disassemble_info *info, arg_MULU *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MULU",
     alias, alias1, alias2));
    return true;
}

static bool trans_MUHU(disassemble_info *info, arg_MUHU *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "MUHU",
     alias, alias1, alias2));
    return true;
}

static bool trans_MULfmt(disassemble_info *info, arg_MULfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d", "" "MULfmt",
     a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_NEGfmt(disassemble_info *info, arg_NEGfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "NEGfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_NOP(disassemble_info *info, arg_NOP *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "NOP"));
    return true;
}

static bool trans_NOR(disassemble_info *info, arg_NOR *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "NOR",
     alias, alias1, alias2));
    return true;
}

static bool trans_OR(disassemble_info *info, arg_OR *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "OR",
     alias, alias1, alias2));
    return true;
}

static bool trans_ORI(disassemble_info *info, arg_ORI *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "ORI",
     alias, alias1, a->imm));
    return true;
}

static bool trans_PAUSE(disassemble_info *info, arg_PAUSE *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "PAUSE"));
    return true;
}

static bool trans_PREF(disassemble_info *info, arg_PREF *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->hint);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "PREF",
     alias, alias1, a->offset));
    return true;
}

static bool trans_PREFE(disassemble_info *info, arg_PREFE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->hint);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "PREFE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_RDHWR(disassemble_info *info, arg_RDHWR *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->sel);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "RDHWR",
     alias, alias1, alias2));
    return true;
}

static bool trans_RDPGPR(disassemble_info *info, arg_RDPGPR *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "RDPGPR",
     alias, alias1));
    return true;
}

static bool trans_RECIPfmt(disassemble_info *info, arg_RECIPfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "RECIPfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_RINTfmt(disassemble_info *info, arg_RINTfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "RINTfmt",
     a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_ROTR(disassemble_info *info, arg_ROTR *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->sa);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "ROTR",
     alias, alias1, alias2));
    return true;
}

static bool trans_ROTRV(disassemble_info *info, arg_ROTRV *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "ROTRV",
     alias, alias1, alias2));
    return true;
}

static bool trans_ROUNDLfmt(disassemble_info *info, arg_ROUNDLfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "ROUNDLfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_ROUNDWfmt(disassemble_info *info, arg_ROUNDWfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "ROUNDWfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_RSQRTfmt(disassemble_info *info, arg_RSQRTfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "RSQRTfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_SB(disassemble_info *info, arg_SB *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SB",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SBE(disassemble_info *info, arg_SBE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SBE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SC(disassemble_info *info, arg_SC *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SC",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SCE(disassemble_info *info, arg_SCE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SCE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SCWP(disassemble_info *info, arg_SCWP *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SCWP",
     alias, alias1, alias2));
    return true;
}

static bool trans_SCWPE(disassemble_info *info, arg_SCWPE *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SCWPE",
     alias, alias1, alias2));
    return true;
}

static bool trans_SDBBP(disassemble_info *info, arg_SDBBP *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "SDBBP", a->code));
    return true;
}

static bool trans_SDC1(disassemble_info *info, arg_SDC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->ft);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SDC1",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SDC2(disassemble_info *info, arg_SDC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SDC2",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SEB(disassemble_info *info, arg_SEB *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "SEB",
     alias, alias1));
    return true;
}

static bool trans_SEH(disassemble_info *info, arg_SEH *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "SEH",
     alias, alias1));
    return true;
}

static bool trans_SELfmt(disassemble_info *info, arg_SELfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d", "" "SELfmt",
     a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_SELEQZ(disassemble_info *info, arg_SELEQZ *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SELEQZ",
     alias, alias1, alias2));
    return true;
}

static bool trans_SELNEZ(disassemble_info *info, arg_SELNEZ *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SELNEZ",
     alias, alias1, alias2));
    return true;
}

static bool trans_SELEQZfmt(disassemble_info *info, arg_SELEQZfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d",
     "" "SELEQZfmt", a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_SELNEQZfmt(disassemble_info *info, arg_SELNEQZfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d",
     "" "SELNEQZfmt", a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_SH(disassemble_info *info, arg_SH *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d",
     "" "SH", alias, alias1, a->offset));
    return true;
}

static bool trans_SHE(disassemble_info *info, arg_SHE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SHE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SIGRIE(disassemble_info *info, arg_SIGRIE *a)
{
    (info->fprintf_func(info->stream, "%-9s " "%d", "" "SIGRIE", a->code));
    return true;
}

static bool trans_SLL(disassemble_info *info, arg_SLL *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->sa);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SLL",
     alias, alias1, alias2));
    return true;
}

static bool trans_SLLV(disassemble_info *info, arg_SLLV *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SLLV",
     alias, alias1, alias2));
    return true;
}

static bool trans_SLT(disassemble_info *info, arg_SLT *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SLT",
     alias, alias1, alias2));
    return true;
}

static bool trans_SLTI(disassemble_info *info, arg_SLTI *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SLTI",
     alias, alias1, a->imm));
    return true;
}

static bool trans_SLTIU(disassemble_info *info, arg_SLTIU *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SLTIU",
     alias, alias1, a->imm));
    return true;
}

static bool trans_SLTU(disassemble_info *info, arg_SLTU *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SLTU",
     alias, alias1, alias2));
    return true;
}

static bool trans_SQRTfmt(disassemble_info *info, arg_SQRTfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "SQRTfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_SRA(disassemble_info *info, arg_SRA *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->sa);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SRA",
     alias, alias1, alias2));
    return true;
}

static bool trans_SRAV(disassemble_info *info, arg_SRAV *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SRAV",
     alias, alias1, alias2));
    return true;
}

static bool trans_SRL(disassemble_info *info, arg_SRL *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->sa);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SRL",
     alias, alias1, alias2));
    return true;
}

static bool trans_SRLV(disassemble_info *info, arg_SRLV *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SRLV",
     alias, alias1, alias2));
    return true;
}

static bool trans_SSNOP(disassemble_info *info, arg_SSNOP *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "SSNOP"));
    return true;
}

static bool trans_SUB(disassemble_info *info, arg_SUB *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SUB",
     alias, alias1, alias2));
    return true;
}

static bool trans_SUBfmt(disassemble_info *info, arg_SUBfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d, r%d", "" "SUBfmt",
     a->ft, a->fs, a->fd, a->fmt));
    return true;
}

static bool trans_SUBU(disassemble_info *info, arg_SUBU *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "SUBU",
     alias, alias1, alias2));
    return true;
}

static bool trans_SW(disassemble_info *info, arg_SW *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SW",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SWE(disassemble_info *info, arg_SWE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SWE",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SWC1(disassemble_info *info, arg_SWC1 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->ft);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SWC1",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SWC2(disassemble_info *info, arg_SWC2 *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "SWC2",
     alias, alias1, a->offset));
    return true;
}

static bool trans_SYNC(disassemble_info *info, arg_SYNC *a)
{
    char alias[5];
    getAlias(alias, a->stype);
    (info->fprintf_func(info->stream, "%-9s " "%s", "" "SYNC", alias));
    return true;
}

static bool trans_SYNCI(disassemble_info *info, arg_SYNCI *a)
{
    char alias[5];
    getAlias(alias, a->base);
    (info->fprintf_func(info->stream, "%-9s " "%s, %d", "" "SYNCI",
     alias, a->offset));
    return true;
}

static bool trans_SYSCALL(disassemble_info *info, arg_SYSCALL *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d", "" "SYSCALL", a->code));
    return true;
}

static bool trans_TEQ(disassemble_info *info, arg_TEQ *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, r%d", "" "TEQ",
     alias, alias1, a->code));
    return true;
}

static bool trans_TGE(disassemble_info *info, arg_TGE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, r%d", "" "TGE",
     alias, alias1, a->code));
    return true;
}

static bool trans_TGEU(disassemble_info *info, arg_TGEU *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, r%d", "" "TGEU",
     alias, alias1, a->code));
    return true;
}

static bool trans_TLBINV(disassemble_info *info, arg_TLBINV *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "TLBINV"));
    return true;
}

static bool trans_TLBINVF(disassemble_info *info, arg_TLBINVF *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "TLBINVF"));
    return true;
}

static bool trans_TLBP(disassemble_info *info, arg_TLBP *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "TLBP"));
    return true;
}

static bool trans_TLBR(disassemble_info *info, arg_TLBR *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "TLBR"));
    return true;
}

static bool trans_TLBWI(disassemble_info *info, arg_TLBWI *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "TLBWI"));
    return true;
}

static bool trans_TLBWR(disassemble_info *info, arg_TLBWR *a)
{
    (info->fprintf_func(info->stream, "%-9s " "", "" "TLBWR"));
    return true;
}

static bool trans_TLT(disassemble_info *info, arg_TLT *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "TLT",
     alias, alias1, a->code));
    return true;
}

static bool trans_TLTU(disassemble_info *info, arg_TLTU *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "TLTU",
     alias, alias1, a->code));
    return true;
}

static bool trans_TNE(disassemble_info *info, arg_TNE *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "TNE",
     alias, alias1, a->code));
    return true;
}

static bool trans_TRUNCLfmt(disassemble_info *info, arg_TRUNCLfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "TRUNCLfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_TRUNCWfmt(disassemble_info *info, arg_TRUNCWfmt *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d, r%d, r%d", "" "TRUNCWfmt",
     a->ft, a->fs, a->fmt));
    return true;
}

static bool trans_WAIT(disassemble_info *info, arg_WAIT *a)
{
    (info->fprintf_func(info->stream, "%-9s " "r%d", "" "WAIT", a->code));
    return true;
}

static bool trans_WRPGPR(disassemble_info *info, arg_WRPGPR *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "WRPGPR",
     alias, alias1));
    return true;
}

static bool trans_WSBH(disassemble_info *info, arg_WSBH *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s", "" "WSBH",
     alias, alias1));
    return true;
}

static bool trans_XOR(disassemble_info *info, arg_XOR *a)
{
    char alias[5];
    char alias1[5];
    char alias2[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    getAlias(alias2, a->rd);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %s", "" "XOR",
     alias, alias1, alias2));
    return true;
}

static bool trans_XORI(disassemble_info *info, arg_XORI *a)
{
    char alias[5];
    char alias1[5];
    getAlias(alias, a->rt);
    getAlias(alias1, a->rs);
    (info->fprintf_func(info->stream, "%-9s " "%s, %s, %d", "" "XORI",
     alias, alias1, a->imm));
    return true;
}
