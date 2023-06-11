/*
 * ARM M-profile helper definitions
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

DEF_HELPER_3(v7m_msr, void, env, i32, i32)
DEF_HELPER_2(v7m_mrs, i32, env, i32)

DEF_HELPER_2(v7m_bxns, void, env, i32)
DEF_HELPER_2(v7m_blxns, void, env, i32)

DEF_HELPER_3(v7m_tt, i32, env, i32, i32)

DEF_HELPER_1(v7m_preserve_fp_state, void, env)

DEF_HELPER_2(v7m_vlstm, void, env, i32)
DEF_HELPER_2(v7m_vlldm, void, env, i32)

DEF_HELPER_2(v8m_stackcheck, void, env, i32)
