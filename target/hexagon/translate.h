/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "cpu.h"
#include "exec/translator.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

typedef struct DisasContext {
    DisasContextBase base;
    uint32_t mem_idx;
} DisasContext;

extern TCGv hex_gpr[TOTAL_PER_THREAD_REGS];
extern TCGv hex_pred[NUM_PREGS];

extern void gen_exception(int excp);
extern void gen_exception_debug(void);

#endif
