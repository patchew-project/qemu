/*
 * Helpers for RISC-V Trace Messages
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "rv-trace-messages.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"

typedef struct RVTraceMessageHeader {
    uint8_t length:5;
    uint8_t flow:2;
    uint8_t extend:1;
} RVTraceMessageHeader;
#define HEADER_SIZE 1

/*
 * Format 3 subformat 0 without 'time' and 'context' fields
 */
typedef struct RVTraceSyncPayload {
    uint8_t format:2;
    uint8_t subformat:2;
    uint8_t branch:1;
    uint8_t privilege:3;
    uint32_t addressLow;
    uint32_t addressHigh;
} RVTraceSyncPayload;
#define SYNC_PAYLOAD_SIZE_64BITS 9

static void rv_etrace_write_bits(uint8_t *bytes, uint32_t bit_pos,
                                 uint32_t num_bits, uint32_t val)
{
    uint32_t pos, byte_index, byte_pos, byte_bits = 0;

    if (!num_bits || 32 < num_bits) {
        return;
    }

    for (pos = 0; pos < num_bits; pos += byte_bits) {
        byte_index = (bit_pos + pos) >> 3;
        byte_pos = (bit_pos + pos) & 0x7;
        byte_bits = (8 - byte_pos) < (num_bits - pos) ?
                    (8 - byte_pos) : (num_bits - pos);
        bytes[byte_index] &= ~(((1U << byte_bits) - 1) << byte_pos);
        bytes[byte_index] |= ((val >> pos) & ((1U << byte_bits) - 1)) << byte_pos;
    }
}

static void rv_etrace_write_header(uint8_t *buf, RVTraceMessageHeader header)
{
    /* flow and extend are always zero, i.e just write length */
    rv_etrace_write_bits(buf, 0, 5, header.length);
}

size_t rv_etrace_gen_encoded_sync_msg(uint8_t *buf, uint64_t pc,
                                      TracePrivLevel priv_level)
{
    RVTraceSyncPayload payload = {.format = 0b11,
                                  .subformat = 0b00,
                                  .branch = 1,
                                  .privilege = priv_level};
    RVTraceMessageHeader header = {.flow = 0, .extend = 0,
                                   .length = SYNC_PAYLOAD_SIZE_64BITS};
    uint8_t bit_pos;

    payload.addressLow = extract64(pc, 0, 32);
    payload.addressHigh = extract64(pc, 32, 32);

    rv_etrace_write_header(buf, header);
    bit_pos = 8;

    rv_etrace_write_bits(buf, bit_pos, 2, payload.format);
    bit_pos += 2;
    rv_etrace_write_bits(buf, bit_pos, 2, payload.subformat);
    bit_pos += 2;
    rv_etrace_write_bits(buf, bit_pos, 1, payload.branch);
    bit_pos += 1;
    rv_etrace_write_bits(buf, bit_pos, 3, payload.privilege);
    bit_pos += 3;

    rv_etrace_write_bits(buf, bit_pos, 32, payload.addressLow);
    bit_pos += 32;
    rv_etrace_write_bits(buf, bit_pos, 32, payload.addressHigh);

    return HEADER_SIZE + SYNC_PAYLOAD_SIZE_64BITS;
}
