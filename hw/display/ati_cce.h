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

typedef struct ATIVGAState ATIVGAState;

#define ATI_CCE_TYPE_MASK            0xc0000000
#define ATI_CCE_TYPE_SHIFT           30

#define ATI_CCE_TYPE0                0
#define ATI_CCE_TYPE0_BASE_REG_MASK  0x00007fff
#define ATI_CCE_TYPE0_BASE_REG_SHIFT 0
#define ATI_CCE_TYPE0_ONE_REG_WR     0x00008000
#define ATI_CCE_TYPE0_COUNT_MASK     0x3fff0000
#define ATI_CCE_TYPE0_COUNT_SHIFT    16

#define ATI_CCE_TYPE1                1
#define ATI_CCE_TYPE1_REG0_MASK      0x000007ff
#define ATI_CCE_TYPE1_REG0_SHIFT     0
#define ATI_CCE_TYPE1_REG1_MASK      0x003ff800
#define ATI_CCE_TYPE1_REG1_SHIFT     11

#define ATI_CCE_TYPE2                2

#define ATI_CCE_TYPE3                3
#define ATI_CCE_TYPE3_OPCODE_MASK    0x0000ff00
#define ATI_CCE_TYPE3_OPCODE_SHIFT   8
#define ATI_CCE_TYPE3_COUNT_MASK     0x3fff0000
#define ATI_CCE_TYPE3_COUNT_SHIFT    16

typedef struct ATIPM4Type0Header {
    uint32_t base_reg;
    uint16_t count;
    bool one_reg_wr;
} ATIPM4Type0Header;

typedef struct ATIPM4Type1Header {
    uint32_t reg0;
    uint32_t reg1;
} ATIPM4Type1Header;

/* Type-2 headers are a no-op and have no state */

typedef struct ATIPM4Type3Header {
    uint8_t opcode;
    uint16_t count;
} ATIPM4Type3Header;

typedef struct ATIPM4PacketState {
    uint8_t type;
    uint16_t dwords_processed;
    union {
        ATIPM4Type0Header t0;
        ATIPM4Type1Header t1;
        ATIPM4Type3Header t3;
    };
} ATIPM4PacketState;

typedef struct ATIPM4MicrocodeState {
    uint8_t addr;
    uint8_t raddr;
    uint64_t microcode[256];
} ATIPM4MicrocodeState;

typedef struct ATICCEState {
    ATIPM4MicrocodeState microcode;
    /* MicroCntl */
    bool freerun;
    ATIPM4PacketState cur_packet;
    /* BufferCntl */
    uint32_t buffer_size_l2qw;
    bool no_update;
    uint8_t buffer_mode;
} ATICCEState;

void ati_cce_receive_data(ATIVGAState *s, uint32_t data);
#endif /* ATI_CCE_H */
