/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Definition of TCGTBCPUState.
 */

#ifndef EXEC_TB_CPU_STATE_H
#define EXEC_TB_CPU_STATE_H

#include "exec/vaddr.h"

/*
 * Default value 0 means to refer to target_long_bits(). It allows to stay
 * compatible with architectures that don't yet have varying definition of TCGv
 * depending on execution mode.
 */
typedef enum TCGvType {
    TCGV_TYPE_TARGET_LONG = 0,
    TCGV_TYPE_I32,
    TCGV_TYPE_I64,
} TCGvType;

typedef struct TCGTBCPUState {
    vaddr pc;
    uint32_t flags;
    uint32_t cflags;
    uint64_t cs_base;
    TCGvType tcgv_type;
} TCGTBCPUState;

#endif
