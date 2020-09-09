/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MIPS target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

C_O0_I1(r)
C_O0_I2(rZ, r)
C_O0_I2(rZ, rZ)
C_O0_I2(SZ, S)
C_O0_I3(SZ, S, S)
C_O0_I3(SZ, SZ, S)
C_O0_I4(rZ, rZ, rZ, rZ)
C_O0_I4(SZ, SZ, S, S)
C_O1_I1(r, L)
C_O1_I1(r, r)
C_O1_I2(r, 0, rZ)
C_O1_I2(r, L, L)
C_O1_I2(r, r, ri)
C_O1_I2(r, r, rI)
C_O1_I2(r, r, rIK)
C_O1_I2(r, r, rJ)
C_O1_I2(r, r, rWZ)
C_O1_I2(r, rZ, rN)
C_O1_I2(r, rZ, rZ)
C_O1_I4(r, rZ, rZ, rZ, 0)
C_O1_I4(r, rZ, rZ, rZ, rZ)
C_O2_I1(r, r, L)
C_O2_I2(r, r, L, L)
C_O2_I2(r, r, r, r)
C_O2_I4(r, r, rZ, rZ, rN, rN)
