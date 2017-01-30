DEF_HELPER_1(bitrev, i32, i32)
DEF_HELPER_1(ff1, i32, i32)
DEF_HELPER_FLAGS_2(sats, TCG_CALL_NO_RWG_SE, i32, i32, i32)
DEF_HELPER_3(divuw, void, env, int, i32)
DEF_HELPER_3(divsw, void, env, int, s32)
DEF_HELPER_4(divul, void, env, int, int, i32)
DEF_HELPER_4(divsl, void, env, int, int, s32)
DEF_HELPER_4(divull, void, env, int, int, i32)
DEF_HELPER_4(divsll, void, env, int, int, s32)
DEF_HELPER_2(set_sr, void, env, i32)
DEF_HELPER_3(movec, void, env, i32, i32)
DEF_HELPER_4(cas2w, void, env, i32, i32, i32)
DEF_HELPER_4(cas2l, void, env, i32, i32, i32)

DEF_HELPER_1(exts32_FP0, void, env)
DEF_HELPER_1(extf32_FP0, void, env)
DEF_HELPER_1(extf64_FP0, void, env)
DEF_HELPER_1(redf32_FP0, void, env)
DEF_HELPER_1(redf64_FP0, void, env)
DEF_HELPER_1(reds32_FP0, void, env)
DEF_HELPER_1(iround_FP0, void, env)
DEF_HELPER_1(itrunc_FP0, void, env)
DEF_HELPER_1(sqrt_FP0, void, env)
DEF_HELPER_1(abs_FP0, void, env)
DEF_HELPER_1(chs_FP0, void, env)
DEF_HELPER_1(add_FP0_FP1, void, env)
DEF_HELPER_1(sub_FP0_FP1, void, env)
DEF_HELPER_1(mul_FP0_FP1, void, env)
DEF_HELPER_1(sglmul_FP0_FP1, void, env)
DEF_HELPER_1(div_FP0_FP1, void, env)
DEF_HELPER_1(sgldiv_FP0_FP1, void, env)
DEF_HELPER_1(cmp_FP0_FP1, void, env)
DEF_HELPER_2(set_fpcr, void, env, i32)
DEF_HELPER_1(tst_FP0, void, env)
DEF_HELPER_1(update_fpstatus, void, env)
DEF_HELPER_4(fmovem, void, env, i32, i32, i32)
DEF_HELPER_2(const_FP0, void, env, i32)
DEF_HELPER_1(getexp_FP0, void, env)
DEF_HELPER_1(getman_FP0, void, env)
DEF_HELPER_1(scale_FP0_FP1, void, env)
DEF_HELPER_1(mod_FP0_FP1, void, env)

DEF_HELPER_3(mac_move, void, env, i32, i32)
DEF_HELPER_3(macmulf, i64, env, i32, i32)
DEF_HELPER_3(macmuls, i64, env, i32, i32)
DEF_HELPER_3(macmulu, i64, env, i32, i32)
DEF_HELPER_2(macsats, void, env, i32)
DEF_HELPER_2(macsatu, void, env, i32)
DEF_HELPER_2(macsatf, void, env, i32)
DEF_HELPER_2(mac_set_flags, void, env, i32)
DEF_HELPER_2(set_macsr, void, env, i32)
DEF_HELPER_2(get_macf, i32, env, i64)
DEF_HELPER_1(get_macs, i32, i64)
DEF_HELPER_1(get_macu, i32, i64)
DEF_HELPER_2(get_mac_extf, i32, env, i32)
DEF_HELPER_2(get_mac_exti, i32, env, i32)
DEF_HELPER_3(set_mac_extf, void, env, i32, i32)
DEF_HELPER_3(set_mac_exts, void, env, i32, i32)
DEF_HELPER_3(set_mac_extu, void, env, i32, i32)

DEF_HELPER_2(flush_flags, void, env, i32)
DEF_HELPER_2(set_ccr, void, env, i32)
DEF_HELPER_FLAGS_1(get_ccr, TCG_CALL_NO_WG_SE, i32, env)
DEF_HELPER_2(raise_exception, void, env, i32)

DEF_HELPER_FLAGS_3(bfffo_reg, TCG_CALL_NO_RWG_SE, i32, i32, i32, i32)

DEF_HELPER_FLAGS_4(bfexts_mem, TCG_CALL_NO_WG, i32, env, i32, s32, i32)
DEF_HELPER_FLAGS_4(bfextu_mem, TCG_CALL_NO_WG, i64, env, i32, s32, i32)
DEF_HELPER_FLAGS_5(bfins_mem, TCG_CALL_NO_WG, i32, env, i32, i32, s32, i32)
DEF_HELPER_FLAGS_4(bfchg_mem, TCG_CALL_NO_WG, i32, env, i32, s32, i32)
DEF_HELPER_FLAGS_4(bfclr_mem, TCG_CALL_NO_WG, i32, env, i32, s32, i32)
DEF_HELPER_FLAGS_4(bfset_mem, TCG_CALL_NO_WG, i32, env, i32, s32, i32)
DEF_HELPER_FLAGS_4(bfffo_mem, TCG_CALL_NO_WG, i64, env, i32, s32, i32)
