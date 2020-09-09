/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AArch64 target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

C_O0_I1(r)
C_O0_I2(lZ, l)
C_O0_I2(r, rA)
C_O0_I2(rZ, r)
C_O0_I2(w, r)
C_O1_I1(r, l)
C_O1_I1(r, r)
C_O1_I1(w, r)
C_O1_I1(w, w)
C_O1_I1(w, wr)
C_O1_I2(r, 0, rZ)
C_O1_I2(r, r, r)
C_O1_I2(r, r, rA)
C_O1_I2(r, r, rAL)
C_O1_I2(r, r, ri)
C_O1_I2(r, r, rL)
C_O1_I2(r, rZ, rZ)
C_O1_I2(w, 0, w)
C_O1_I2(w, w, w)
C_O1_I2(w, w, wN)
C_O1_I2(w, w, wO)
C_O1_I2(w, w, wZ)
C_O1_I3(w, w, w, w)
C_O1_I4(r, r, rA, rZ, rZ)
C_O2_I4(r, r, rZ, rZ, rA, rMZ)
