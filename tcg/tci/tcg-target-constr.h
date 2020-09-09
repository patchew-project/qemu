/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * TCI target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

C_O0_I2(r, r)
C_O0_I2(r, ri)
C_O0_I2(r, S)
C_O0_I3(r, r, S)
C_O0_I3(r, S, S)
C_O0_I4(r, r, S, S)
C_O1_I1(r, L)
C_O1_I1(r, r)
C_O1_I2(r, 0, r)
C_O1_I2(r, L, L)
C_O1_I2(r, ri, ri)
C_O1_I2(r, r, r)
C_O1_I2(r, r, ri)
C_O2_I1(r, r, L)
C_O2_I2(r, r, L, L)

#if TCG_TARGET_REG_BITS == 32
C_O0_I4(r, r, ri, ri)
C_O1_I4(r, r, r, ri, ri)
C_O2_I2(r, r, r, r)
C_O2_I4(r, r, r, r, r, r)
#endif
