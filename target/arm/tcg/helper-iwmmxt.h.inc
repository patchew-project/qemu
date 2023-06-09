/*
 *  XScale iwMMXt specific helper definitions
 *
 * Copyright (c) 2007 OpenedHand, Ltd.
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 * Copyright (c) 2008 CodeSourcery
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

DEF_HELPER_2(iwmmxt_maddsq, i64, i64, i64)
DEF_HELPER_2(iwmmxt_madduq, i64, i64, i64)
DEF_HELPER_2(iwmmxt_sadb, i64, i64, i64)
DEF_HELPER_2(iwmmxt_sadw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_mulslw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_mulshw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_mululw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_muluhw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_macsw, i64, i64, i64)
DEF_HELPER_2(iwmmxt_macuw, i64, i64, i64)
DEF_HELPER_1(iwmmxt_setpsr_nz, i32, i64)

#define DEF_IWMMXT_HELPER_SIZE_ENV(name) \
DEF_HELPER_3(iwmmxt_##name##b, i64, env, i64, i64) \
DEF_HELPER_3(iwmmxt_##name##w, i64, env, i64, i64) \
DEF_HELPER_3(iwmmxt_##name##l, i64, env, i64, i64) \

DEF_IWMMXT_HELPER_SIZE_ENV(unpackl)
DEF_IWMMXT_HELPER_SIZE_ENV(unpackh)

DEF_HELPER_2(iwmmxt_unpacklub, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackluw, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpacklul, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhub, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhuw, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhul, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpacklsb, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpacklsw, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpacklsl, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhsb, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhsw, i64, env, i64)
DEF_HELPER_2(iwmmxt_unpackhsl, i64, env, i64)

DEF_IWMMXT_HELPER_SIZE_ENV(cmpeq)
DEF_IWMMXT_HELPER_SIZE_ENV(cmpgtu)
DEF_IWMMXT_HELPER_SIZE_ENV(cmpgts)

DEF_IWMMXT_HELPER_SIZE_ENV(mins)
DEF_IWMMXT_HELPER_SIZE_ENV(minu)
DEF_IWMMXT_HELPER_SIZE_ENV(maxs)
DEF_IWMMXT_HELPER_SIZE_ENV(maxu)

DEF_IWMMXT_HELPER_SIZE_ENV(subn)
DEF_IWMMXT_HELPER_SIZE_ENV(addn)
DEF_IWMMXT_HELPER_SIZE_ENV(subu)
DEF_IWMMXT_HELPER_SIZE_ENV(addu)
DEF_IWMMXT_HELPER_SIZE_ENV(subs)
DEF_IWMMXT_HELPER_SIZE_ENV(adds)

DEF_HELPER_3(iwmmxt_avgb0, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_avgb1, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_avgw0, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_avgw1, i64, env, i64, i64)

DEF_HELPER_3(iwmmxt_align, i64, i64, i64, i32)
DEF_HELPER_4(iwmmxt_insr, i64, i64, i32, i32, i32)

DEF_HELPER_1(iwmmxt_bcstb, i64, i32)
DEF_HELPER_1(iwmmxt_bcstw, i64, i32)
DEF_HELPER_1(iwmmxt_bcstl, i64, i32)

DEF_HELPER_1(iwmmxt_addcb, i64, i64)
DEF_HELPER_1(iwmmxt_addcw, i64, i64)
DEF_HELPER_1(iwmmxt_addcl, i64, i64)

DEF_HELPER_1(iwmmxt_msbb, i32, i64)
DEF_HELPER_1(iwmmxt_msbw, i32, i64)
DEF_HELPER_1(iwmmxt_msbl, i32, i64)

DEF_HELPER_3(iwmmxt_srlw, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_srll, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_srlq, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sllw, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_slll, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sllq, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sraw, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sral, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_sraq, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_rorw, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_rorl, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_rorq, i64, env, i64, i32)
DEF_HELPER_3(iwmmxt_shufh, i64, env, i64, i32)

DEF_HELPER_3(iwmmxt_packuw, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packul, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packuq, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packsw, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packsl, i64, env, i64, i64)
DEF_HELPER_3(iwmmxt_packsq, i64, env, i64, i64)

DEF_HELPER_3(iwmmxt_muladdsl, i64, i64, i32, i32)
DEF_HELPER_3(iwmmxt_muladdsw, i64, i64, i32, i32)
DEF_HELPER_3(iwmmxt_muladdswl, i64, i64, i32, i32)
