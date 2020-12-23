/* SPDX-License-Identifier: MIT */
/*
 * TCI target-specific operand constaints.
 * Copyright (c) 2020 Linaro
 */

REGS('r', MAKE_64BIT_MASK(0, TCG_TARGET_NB_REGS))
REGS('L', MAKE_64BIT_MASK(0, TCG_TARGET_NB_REGS))
REGS('S', MAKE_64BIT_MASK(0, TCG_TARGET_NB_REGS))
