/* SPDX-License-Identifier: MIT */
/*
 * TCI target-specific constaint sets.
 * Copyright (c) 2020 Linaro
 */

C_O0_I2(r, r)
C_O0_I2(r, ri)
C_O0_I3(r, r, r)
C_O0_I4(r, r, ri, ri)
C_O0_I4(r, r, r, r)
C_O1_I1(r, r)
C_O1_I2(r, 0, r)
C_O1_I2(r, ri, ri)
C_O1_I2(r, r, r)
C_O1_I2(r, r, ri)
C_O1_I4(r, r, r, ri, ri)
C_O2_I1(r, r, r)
C_O2_I2(r, r, r, r)
C_O2_I4(r, r, r, r, r, r)
