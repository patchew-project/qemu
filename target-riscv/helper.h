/* Exceptions */
DEF_HELPER_2(raise_exception, noreturn, env, i32)
DEF_HELPER_1(raise_exception_debug, noreturn, env)
DEF_HELPER_3(raise_exception_mbadaddr, noreturn, env, i32, tl)

#if defined(TARGET_RISCV64)
DEF_HELPER_FLAGS_3(mulhsu, TCG_CALL_NO_RWG_SE, tl, env, tl, tl)
#endif

/* Floating Point - fused */
DEF_HELPER_FLAGS_5(fmadd_s, TCG_CALL_NO_RWG, i64, env, i64, i64, i64, i64)
DEF_HELPER_FLAGS_5(fmadd_d, TCG_CALL_NO_RWG, i64, env, i64, i64, i64, i64)
DEF_HELPER_FLAGS_5(fmsub_s, TCG_CALL_NO_RWG, i64, env, i64, i64, i64, i64)
DEF_HELPER_FLAGS_5(fmsub_d, TCG_CALL_NO_RWG, i64, env, i64, i64, i64, i64)
DEF_HELPER_FLAGS_5(fnmsub_s, TCG_CALL_NO_RWG, i64, env, i64, i64, i64, i64)
DEF_HELPER_FLAGS_5(fnmsub_d, TCG_CALL_NO_RWG, i64, env, i64, i64, i64, i64)
DEF_HELPER_FLAGS_5(fnmadd_s, TCG_CALL_NO_RWG, i64, env, i64, i64, i64, i64)
DEF_HELPER_FLAGS_5(fnmadd_d, TCG_CALL_NO_RWG, i64, env, i64, i64, i64, i64)
