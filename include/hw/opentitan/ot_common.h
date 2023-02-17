/*
 * QEMU RISC-V Helpers for OpenTitan EarlGrey
 *
 * Copyright (c) 2023 Rivos, Inc.
 *
 * Author(s):
 *  Emmanuel Blot <eblot@rivosinc.com>
 *  Loïc Lefort <loic@rivosinc.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_OT_COMMON_H
#define HW_RISCV_OT_COMMON_H

#include "qemu/osdep.h"

/* ------------------------------------------------------------------------ */
/* Shadow Registers */
/* ------------------------------------------------------------------------ */

/*
 * Shadow register, concept documented at:
 * https://docs.opentitan.org/doc/rm/register_tool/#shadow-registers
 */
typedef struct OtShadowReg {
    /* committed register value */
    uint32_t committed;
    /* staged register value */
    uint32_t staged;
    /* true if 'staged' holds a value */
    bool staged_p;
} OtShadowReg;

enum {
    OT_SHADOW_REG_ERROR = -1,
    OT_SHADOW_REG_COMMITTED = 0,
    OT_SHADOW_REG_STAGED = 1,
};

/**
 * Initialize a shadow register with a committed value and no staged value
 */
static inline void ot_shadow_reg_init(OtShadowReg *sreg, uint32_t value)
{
    sreg->committed = value;
    sreg->staged_p = false;
}

/**
 * Write a new value to a shadow register.
 * If no value was previously staged, the new value is only staged for next
 * write and the function returns OT_SHADOW_REG_STAGED.
 * If a value was previously staged and the new value is different, the function
 * returns OT_SHADOW_REG_ERROR and the new value is ignored. Otherwise the value
 * is committed, the staged value is discarded and the function returns
 * OT_SHADOW_REG_COMMITTED.
 */
static inline int ot_shadow_reg_write(OtShadowReg *sreg, uint32_t value)
{
    if (sreg->staged_p) {
        if (value != sreg->staged) {
            /* second write is different, return error status */
            return OT_SHADOW_REG_ERROR;
        }
        sreg->committed = value;
        sreg->staged_p = false;
        return OT_SHADOW_REG_COMMITTED;
    } else {
        sreg->staged = value;
        sreg->staged_p = true;
        return OT_SHADOW_REG_STAGED;
    }
}

/**
 * Return the current committed register value
 */
static inline uint32_t ot_shadow_reg_peek(const OtShadowReg *sreg)
{
    return sreg->committed;
}

/**
 * Discard the staged value and return the current committed register value
 */
static inline uint32_t ot_shadow_reg_read(OtShadowReg *sreg)
{
    sreg->staged_p = false;
    return sreg->committed;
}

#endif /* HW_RISCV_OT_COMMON_H */
