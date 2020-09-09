/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARM32 target-specific operand constaints. 
 * Copyright (c) 2020 Linaro
 */

C_O0_I1(r)
C_O0_I2(r, r)
C_O0_I2(r, rIN)
C_O0_I2(s, s)
C_O0_I3(s, s, s)
C_O0_I4(r, r, rI, rI)
C_O0_I4(s, s, s, s)
C_O1_I1(r, l)
C_O1_I1(r, r)
C_O1_I2(r, 0, rZ)
C_O1_I2(r, l, l)
C_O1_I2(r, r, r)
C_O1_I2(r, r, rI)
C_O1_I2(r, r, rIK)
C_O1_I2(r, r, rIN)
C_O1_I2(r, r, ri)
C_O1_I2(r, rZ, rZ)
C_O1_I4(r, r, r, rI, rI)
C_O1_I4(r, r, rIN, rIK, 0)
C_O2_I1(r, r, l)
C_O2_I2(r, r, l, l)
C_O2_I2(r, r, r, r)
C_O2_I4(r, r, r, r, rIN, rIK)
C_O2_I4(r, r, rI, rI, rIN, rIK)
