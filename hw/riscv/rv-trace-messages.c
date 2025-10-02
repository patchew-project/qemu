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

/*
 * Format 3 subformat 1 without 'time' and 'context' fields
 */
typedef struct RVTraceTrapPayload {
    uint8_t format:2;
    uint8_t subformat:2;
    uint8_t branch:1;
    uint8_t privilege:3;
    uint8_t ecause:6;
    uint8_t interrupt:1;
    uint8_t thaddr:1;
    uint32_t addressLow;
    uint32_t addressHigh;
    uint32_t tvalLow;
    uint32_t tvalHigh;
} RVTraceTrapPayload;
#define TRAP_PAYLOAD_SIZE_64BITS 18

typedef struct RVTraceFormat2Payload {
    uint8_t format:2;
    uint32_t addressLow;
    uint32_t addressHigh;
    uint8_t notify:1;
    uint8_t updiscon:1;
    uint8_t irreport:1;
    uint8_t irdepth:3;
} RVTraceFormat2Payload;
#define FORMAT2_PAYLOAD_SIZE_64BITS 9

typedef struct RVTraceFormat1BasePayload {
    uint8_t format:2;
    uint8_t branches:5;
    uint32_t branch_map:31;
} RVTraceFormat1BasePayload;
#define FORMAT1_BASE_PAYLOAD_SIZE_64BITS 5

typedef struct RVTraceFormat1Payload {
    uint8_t format:2;
    uint8_t branches:5;
    uint32_t branch_map;
    uint32_t addressLow;
    uint32_t addressHigh;
    uint8_t notify:1;
    uint8_t updiscon:1;
    uint8_t irreport:1;
    uint8_t irdepth:3;
} RVTraceFormat1Payload;

/*
 * FORMAT2_PAYLOAD_SIZE_64BITS = 9 plus 5 bits of 'branches',
 * plus minimal 3 bits of 'branch_map' = 10 bytes.
 */
#define FORMAT1_PAYLOAD_MIN_SIZE_64BITS 10

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
                                      TracePrivLevel priv_level,
                                      bool pc_is_branch)
{
    RVTraceSyncPayload payload = {.format = 0b11,
                                  .subformat = 0b00,
                                  .branch = pc_is_branch,
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

/*
 * Note: this function assumes thaddr = 1.
 */
size_t rv_etrace_gen_encoded_trap_msg(uint8_t *buf, uint64_t trap_addr,
                                      TracePrivLevel priv_level,
                                      uint8_t ecause,
                                      bool is_interrupt,
                                      uint64_t tval)
{
    RVTraceTrapPayload payload = {.format = 0b11,
                                  .subformat = 0b01,
                                  .privilege = priv_level,
                                  .ecause = ecause};
    RVTraceMessageHeader header = {.flow = 0, .extend = 0,
                                   .length = TRAP_PAYLOAD_SIZE_64BITS};
    uint8_t bit_pos;

    payload.addressLow = extract64(trap_addr, 0, 32);
    payload.addressHigh = extract64(trap_addr, 32, 32);

    /*
     * When interrupt = 1 'tval' is ommited. Take 8 bytes
     * from the final size.
     */
    if (is_interrupt) {
        header.length = TRAP_PAYLOAD_SIZE_64BITS - 8;
    }

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

    rv_etrace_write_bits(buf, bit_pos, 6, payload.ecause);
    bit_pos += 6;

    if (is_interrupt) {
        rv_etrace_write_bits(buf, bit_pos, 1, 1);
    } else {
        rv_etrace_write_bits(buf, bit_pos, 1, 0);
    }
    bit_pos += 1;

    /* thaddr is hardcoded to 1 for now */
    rv_etrace_write_bits(buf, bit_pos, 1, 1);
    bit_pos += 1;

    rv_etrace_write_bits(buf, bit_pos, 32, payload.addressLow);
    bit_pos += 32;
    rv_etrace_write_bits(buf, bit_pos, 32, payload.addressHigh);
    bit_pos += 32;

    /* Skip trap_addr if is_interrupt  */
    if (is_interrupt) {
        goto out;
    }

    payload.tvalLow = extract64(trap_addr, 0, 32);
    payload.tvalHigh = extract64(trap_addr, 32, 32);

    rv_etrace_write_bits(buf, bit_pos, 32, payload.tvalLow);
    bit_pos += 32;
    rv_etrace_write_bits(buf, bit_pos, 32, payload.tvalHigh);

out:
    return HEADER_SIZE + header.length;
}

/*
 * Note: irreport and irdepth is always == updiscon.
 *
 * return_stack_size_p + call_counter_size_p is hardcoded
 * to 3 since we don't implement neither ATM.
 */
size_t rv_etrace_gen_encoded_format2_msg(uint8_t *buf, uint64_t addr,
                                         bool notify, bool updiscon)
{
    RVTraceFormat2Payload payload = {.format = 0b11,
                                     .notify = notify,
                                     .updiscon = updiscon};
    RVTraceMessageHeader header = {.flow = 0, .extend = 0,
                                   .length = FORMAT2_PAYLOAD_SIZE_64BITS};
    uint8_t bit_pos;

    payload.addressLow = extract64(addr, 0, 32);
    payload.addressHigh = extract64(addr, 32, 32);

    payload.irreport = updiscon;
    if (updiscon) {
        payload.irdepth = 0b111;
    } else {
        payload.irdepth = 0;
    }

    rv_etrace_write_header(buf, header);
    bit_pos = 8;

    rv_etrace_write_bits(buf, bit_pos, 2, payload.format);
    bit_pos += 2;

    rv_etrace_write_bits(buf, bit_pos, 32, payload.addressLow);
    bit_pos += 32;
    rv_etrace_write_bits(buf, bit_pos, 32, payload.addressHigh);
    bit_pos += 32;

    rv_etrace_write_bits(buf, bit_pos, 1, payload.notify);
    bit_pos += 1;
    rv_etrace_write_bits(buf, bit_pos, 1, payload.updiscon);
    bit_pos += 1;
    rv_etrace_write_bits(buf, bit_pos, 1, payload.irreport);
    bit_pos += 1;
    rv_etrace_write_bits(buf, bit_pos, 3, payload.irdepth);

    return HEADER_SIZE + header.length;
}

size_t rv_etrace_gen_encoded_format1_noaddr(uint8_t *buf,
                                            uint8_t branches,
                                            uint32_t branch_map)
{
    RVTraceMessageHeader header = {.flow = 0, .extend = 0,
        .length = FORMAT1_BASE_PAYLOAD_SIZE_64BITS};
    RVTraceFormat1BasePayload payload = {.format = 0b01,
        .branches = branches, .branch_map = branch_map};
    uint8_t bit_pos;

    rv_etrace_write_header(buf, header);
    bit_pos = 8;

    rv_etrace_write_bits(buf, bit_pos, 2, payload.format);
    bit_pos += 2;

    rv_etrace_write_bits(buf, bit_pos, 5, payload.branches);
    bit_pos += 5;

    rv_etrace_write_bits(buf, bit_pos, 31, payload.branch_map);

    return HEADER_SIZE + header.length;
}

/*
 * Same reservations made in the format 2 helper:
 *
 * - irreport and irdepth is always == updiscon;
 *
 * - return_stack_size_p + call_counter_size_p is hardcoded
 * to 3 since we don't implement neither ATM.
 */
size_t rv_etrace_gen_encoded_format1(uint8_t *buf,
                                     uint8_t branches, uint32_t branch_map,
                                     uint64_t addr,
                                     bool notify, bool updiscon)
{
    RVTraceMessageHeader header = {.flow = 0, .extend = 0};
    RVTraceFormat1Payload payload = {.format = 0b01,
                                     .branches = branches,
                                     .notify = notify,
                                     .updiscon = updiscon};
    uint8_t payload_size = FORMAT1_PAYLOAD_MIN_SIZE_64BITS;
    uint8_t branch_map_size = 0;
    uint8_t bit_pos;

    g_assert(branches < 32);

    if (branches <= 3) {
        branch_map_size = 3;
    } else if (branches <= 7) {
        branch_map_size = 7;
        payload_size++;
    } else if (branches <= 15) {
        branch_map_size = 15;
        payload_size += 2;
    } else {
        branch_map_size = 31;
        payload_size += 4;
    }

    header.length = payload_size;

    rv_etrace_write_header(buf, header);
    bit_pos = 8;

    payload.addressLow = extract64(addr, 0, 32);
    payload.addressHigh = extract64(addr, 32, 32);

    payload.irreport = updiscon;
    if (updiscon) {
        payload.irdepth = 0b111;
    } else {
        payload.irdepth = 0;
    }

    rv_etrace_write_bits(buf, bit_pos, 2, payload.format);
    bit_pos += 2;

    rv_etrace_write_bits(buf, bit_pos, 5, payload.branches);
    bit_pos += 5;

    rv_etrace_write_bits(buf, bit_pos, branch_map_size, payload.branch_map);
    bit_pos += branch_map_size;

    rv_etrace_write_bits(buf, bit_pos, 32, payload.addressLow);
    bit_pos += 32;
    rv_etrace_write_bits(buf, bit_pos, 32, payload.addressHigh);
    bit_pos += 32;

    rv_etrace_write_bits(buf, bit_pos, 1, payload.notify);
    bit_pos += 1;
    rv_etrace_write_bits(buf, bit_pos, 1, payload.updiscon);
    bit_pos += 1;
    rv_etrace_write_bits(buf, bit_pos, 1, payload.irreport);
    bit_pos += 1;
    rv_etrace_write_bits(buf, bit_pos, 3, payload.irdepth);

    return HEADER_SIZE + header.length;
}
