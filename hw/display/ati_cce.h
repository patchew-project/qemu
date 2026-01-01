/*
 * QEMU ATI SVGA emulation
 * CCE engine functions
 *
 * Copyright (c) 2025 Chad Jablonski
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ATI_CCE_H
#define ATI_CCE_H

#include "qemu/osdep.h"
#include "qemu/log.h"

typedef struct ATIPM4MicrocodeState {
    uint8_t addr;
    uint8_t raddr;
    uint64_t microcode[256];
} ATIPM4MicrocodeState;

typedef struct ATICCEState {
    ATIPM4MicrocodeState microcode;
    /* MicroCntl */
    bool freerun;
    /* BufferCntl */
    uint32_t buffer_size_l2qw;
    bool no_update;
    uint8_t buffer_mode;
} ATICCEState;

#endif /* ATI_CCE_H */
