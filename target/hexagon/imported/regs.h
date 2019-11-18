/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */


/*
 * There are 32 general user regs and up to 32 user control regs.
 */


#ifndef _REGS_H
#define _REGS_H

#define NUM_GEN_REGS 32
#define NUM_PREGS 4
/* user + guest + per-thread supervisor + A regs */
#define NUM_PER_THREAD_CR (32 + 32 + 16 + 48)
#define TOTAL_PER_THREAD_REGS 64
#define NUM_GLOBAL_REGS (128 + 32) /* + A regs */

#endif
