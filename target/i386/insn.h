#ifndef INSN
#   define INSN(mnem, prefix, opcode, feat)
#endif /* INSN */

#ifndef INSN_R
#   define INSN_R(mnem, prefix, opcode, feat, opR1)
#endif /* INSN_R */

#ifndef INSN_RR
#   define INSN_RR(mnem, prefix, opcode, feat, opR1, opR2)
#endif /* INSN_RR */

#ifndef INSN_W
#   define INSN_W(mnem, prefix, opcode, feat, opW1)
#endif /* INSN_W */

#ifndef INSN_WR
#   define INSN_WR(mnem, prefix, opcode, feat, opW1, opR1)
#endif /* INSN_WR */

#ifndef INSN_WRR
#   define INSN_WRR(mnem, prefix, opcode, feat, opW1, opR1, opR2)
#endif /* INSN_WRR */

#ifndef INSN_WRRR
#   define INSN_WRRR(mnem, prefix, opcode, feat, opW1, opR1, opR2, opR3)
#endif /* INSN_WRRR */

#ifndef INSN_GRP
#   define INSN_GRP(grpname, prefix, opcode)
#endif /* INSN_GRP */

#ifndef INSN_GRP_BEGIN
#   define INSN_GRP_BEGIN(grpname)
#endif /* INSN_GRP_BEGIN */

#ifndef INSN_GRPMEMB
#   define INSN_GRPMEMB(grpname, mnem, opcode, feat)
#endif /* INSN_GRPMEMB */

#ifndef INSN_GRPMEMB_R
#   define INSN_GRPMEMB_R(grpname, mnem, opcode, feat, opR1)
#endif /* INSN_GRPMEMB_R */

#ifndef INSN_GRPMEMB_RR
#   define INSN_GRPMEMB_RR(grpname, mnem, opcode, feat, opR1, opR2)
#endif /* INSN_GRPMEMB_RR */

#ifndef INSN_GRPMEMB_W
#   define INSN_GRPMEMB_W(grpname, mnem, opcode, feat, opW1)
#endif /* INSN_GRPMEMB_W */

#ifndef INSN_GRPMEMB_WR
#   define INSN_GRPMEMB_WR(grpname, mnem, opcode, feat, opW1, opR1)
#endif /* INSN_GRPMEMB_WR */

#ifndef INSN_GRPMEMB_WRR
#   define INSN_GRPMEMB_WRR(grpname, mnem, opcode, feat, opW1, opR1, opR2)
#endif /* INSN_GRPMEMB_WRR */

#ifndef INSN_GRPMEMB_WRRR
#   define INSN_GRPMEMB_WRRR(grpname, mnem, opcode, feat, opW1, opR1, opR2, opR3)
#endif /* INSN_GRPMEMB_WRRR */

#ifndef INSN_GRP_END
#   define INSN_GRP_END(grpname)
#endif /* INSN_GRP_END */

#undef LEG
#undef VEX
#undef INSN
#undef INSN_R
#undef INSN_RR
#undef INSN_W
#undef INSN_WR
#undef INSN_WRR
#undef INSN_WRRR
#undef INSN_GRP
#undef INSN_GRP_BEGIN
#undef INSN_GRPMEMB
#undef INSN_GRPMEMB_R
#undef INSN_GRPMEMB_RR
#undef INSN_GRPMEMB_W
#undef INSN_GRPMEMB_WR
#undef INSN_GRPMEMB_WRR
#undef INSN_GRPMEMB_WRRR
#undef INSN_GRP_END
