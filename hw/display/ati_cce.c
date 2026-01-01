/*
 * QEMU ATI SVGA emulation
 * CCE engine functions
 *
 * Copyright (c) 2025 Chad Jablonski
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "ati_regs.h"
#include "ati_int.h"
#include "trace.h"

static uint32_t
ati_cce_fifo_max(uint8_t mode)
{
    switch (mode) {
    case PM4_BUFFER_CNTL_NONPM4...PM4_BUFFER_CNTL_192BM:
        return 192;
    case PM4_BUFFER_CNTL_128PIO_64INDBM...PM4_BUFFER_CNTL_128BM_64INDBM:
        return 128;
    case PM4_BUFFER_CNTL_64PIO_128INDBM...PM4_BUFFER_CNTL_64PIO_64VCBM_64INDBM:
        /* fall through */
    case PM4_BUFFER_CNTL_64PIO_64VCPIO_64INPIO:
        return 64;
    default:
        /* Undocumented but testing shows this returns 192 otherwise */
        return 192;
    }
}

static inline uint32_t
ati_cce_data_packets_remaining(const ATIPM4PacketState *p)
{
    switch (p->type) {
    case ATI_CCE_TYPE0:
        return p->t0.count - p->dwords_processed;
    case ATI_CCE_TYPE1:
        return 2 - p->dwords_processed;
    case ATI_CCE_TYPE2:
        return 0;
    case ATI_CCE_TYPE3:
        return p->t3.count - p->dwords_processed;
    default:
        /* This should never happen, type is 2-bits wide */
        return 0;
    }
}

static void
ati_cce_parse_packet_header(ATIPM4PacketState *p, uint32_t header)
{
    p->dwords_processed = 0;
    p->type = (header & ATI_CCE_TYPE_MASK) >> ATI_CCE_TYPE_SHIFT;
    switch (p->type) {
    case ATI_CCE_TYPE0: {
        ATIPM4Type0Header t0 = {
            /* Packet stores base_reg as word offset, convert to byte offset */
            .base_reg = ((header & ATI_CCE_TYPE0_BASE_REG_MASK) >>
                        ATI_CCE_TYPE0_BASE_REG_SHIFT) << 2,
            /* Packet stores count as n-1, convert to actual count */
            .count = ((header & ATI_CCE_TYPE0_COUNT_MASK) >>
                     ATI_CCE_TYPE0_COUNT_SHIFT) + 1,
            .one_reg_wr = header & ATI_CCE_TYPE0_ONE_REG_WR,
        };
        p->t0 = t0;
        trace_ati_cce_packet_type0(t0.base_reg, t0.count, t0.one_reg_wr);
        break;
    }
    case ATI_CCE_TYPE1: {
        ATIPM4Type1Header t1 = {
            /* Packet stores reg0 as word offset, convert to byte offset */
            .reg0 = ((header & ATI_CCE_TYPE1_REG0_MASK) >>
                    ATI_CCE_TYPE1_REG0_SHIFT) << 2,
            /* Packet stores reg1 as word offset, convert to byte offset */
            .reg1 = ((header & ATI_CCE_TYPE1_REG1_MASK) >>
                    ATI_CCE_TYPE1_REG1_SHIFT) << 2,
        };
        p->t1 = t1;
        trace_ati_cce_packet_type1(t1.reg0, t1.reg1);
        break;
    }
    case ATI_CCE_TYPE2: {
        /* Type-2 is a no-op, it has no header state */
        trace_ati_cce_packet_type2();
        break;
    }
    case ATI_CCE_TYPE3: {
        ATIPM4Type3Header t3 = {
            .opcode = (header & ATI_CCE_TYPE3_OPCODE_MASK) >>
                      ATI_CCE_TYPE3_OPCODE_SHIFT,
            /* Packet stores count as n-1, convert to actual count */
            .count = ((header & ATI_CCE_TYPE3_COUNT_MASK) >>
                     ATI_CCE_TYPE3_COUNT_SHIFT) + 1,
        };
        p->t3 = t3;
        trace_ati_cce_packet_type3(t3.opcode, t3.count);
        break;
    }
    default:
        /* This should never happen, type is 2-bits wide */
        break;
    }
}

static void
ati_cce_process_type0_data(ATIVGAState *s, uint32_t data)
{
    ATIPM4PacketState *p = &s->cce.cur_packet;
    uint32_t offset = p->t0.one_reg_wr ? 0 :
                      (p->dwords_processed * sizeof(uint32_t));
    uint32_t reg = p->t0.base_reg + offset;
    trace_ati_cce_packet_type0_data(p->dwords_processed, reg, data);
    ati_reg_write(s, reg, data, sizeof(uint32_t));
}

static void
ati_cce_process_type1_data(ATIVGAState *s, uint32_t data)
{
    ATIPM4PacketState *p = &s->cce.cur_packet;
    uint32_t reg = p->dwords_processed == 0 ? p->t1.reg0 : p->t1.reg1;
    trace_ati_cce_packet_type1_data(p->dwords_processed, reg, data);
    ati_reg_write(s, reg, data, sizeof(uint32_t));
}

static void
ati_cce_process_type3_data(ATIVGAState *s, uint32_t data)
{
    ATIPM4PacketState *p = &s->cce.cur_packet;
    uint32_t opcode = p->t3.opcode;
    qemu_log_mask(LOG_UNIMP, "Type-3 CCE packets not yet implemented\n");
    trace_ati_cce_packet_type3_data(p->dwords_processed, opcode, data);
}

static void
ati_cce_process_packet_data(ATIVGAState *s, uint32_t data)
{
    ATIPM4PacketState *p = &s->cce.cur_packet;
    switch (p->type) {
    case ATI_CCE_TYPE0: {
        ati_cce_process_type0_data(s, data);
        p->dwords_processed += 1;
        break;
    }
    case ATI_CCE_TYPE1: {
        ati_cce_process_type1_data(s, data);
        p->dwords_processed += 1;
        break;
    }
    case ATI_CCE_TYPE2:
        /* Type-2 packets have no data, we should never end up here */
        break;
    case ATI_CCE_TYPE3: {
        ati_cce_process_type3_data(s, data);
        p->dwords_processed += 1;
        break;
    }
    default:
        /* This should never happen, type is 2-bits wide */
        break;
    }
}

void
ati_cce_receive_data(ATIVGAState *s, uint32_t data)
{
    uint32_t remaining = ati_cce_data_packets_remaining(&s->cce.cur_packet);
    if (remaining == 0) {
        /* We're ready to start processing a new packet header */
        ati_cce_parse_packet_header(&s->cce.cur_packet, data);
        return;
    }
    ati_cce_process_packet_data(s, data);
}

bool
ati_cce_micro_busy(const ATIPM4PacketState *p)
{
    uint32_t remaining = ati_cce_data_packets_remaining(p);
    if (remaining > 0) {
        return true;
    }
    return false;
}

uint32_t
ati_cce_fifo_cnt(const ATICCEState *c)
{
    /*
     * This should return the available slots. Given that commands are
     * processed immediately this returns the fifo max for now.
     */
    return ati_cce_fifo_max(c->buffer_mode);
}
