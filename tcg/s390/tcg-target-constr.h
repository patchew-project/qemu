/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * S390 target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

C_O0_I1(r)
C_O0_I2(L, L)
C_O0_I2(r, r)
C_O0_I2(r, ri)
C_O1_I1(r, L)
C_O1_I1(r, r)
C_O1_I2(r, 0, ri)
C_O1_I2(r, 0, rI)
C_O1_I2(r, 0, rJ)
C_O1_I2(r, r, ri)
C_O1_I2(r, rZ, r)
C_O1_I4(r, r, ri, r, 0)
C_O1_I4(r, r, ri, rI, 0)
C_O2_I2(b, a, 0, r)
C_O2_I3(b, a, 0, 1, r)
C_O2_I4(r, r, 0, 1, rA, r)
C_O2_I4(r, r, 0, 1, ri, r)
C_O2_I4(r, r, 0, 1, r, r)
