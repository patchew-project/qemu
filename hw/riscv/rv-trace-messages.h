/*
 * Helpers for RISC-V Trace Messages
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_RV_TRACE_MESSAGES_H
#define RISCV_RV_TRACE_MESSAGES_H

typedef enum {
    U = 0,
    S_HS = 1,
    RESERVED = 2,
    M = 3,
    D = 4,
    VU = 5,
    VS = 6,
} TracePrivLevel;

size_t rv_etrace_gen_encoded_sync_msg(uint8_t *buf, uint64_t pc,
                                      TracePrivLevel priv_level);
size_t rv_etrace_gen_encoded_trap_msg(uint8_t *buf, uint64_t trap_addr,
                                      TracePrivLevel priv_level,
                                      uint8_t ecause,
                                      bool is_interrupt,
                                      uint64_t tval);
size_t rv_etrace_gen_encoded_format2_msg(uint8_t *buf, uint64_t addr,
                                         bool notify, bool updiscon);

#endif
