/*
 * windbgstub.c
 *
 * Copyright (c) 2010-2017 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "exec/windbgstub.h"
#include "exec/windbgstub-utils.h"

typedef enum ParsingState {
    STATE_LEADER,
    STATE_PACKET_TYPE,
    STATE_PACKET_BYTE_COUNT,
    STATE_PACKET_ID,
    STATE_PACKET_CHECKSUM,
    STATE_PACKET_DATA,
    STATE_TRAILING_BYTE,
} ParsingState;

typedef enum ParsingResult {
    RESULT_NONE,
    RESULT_BREAKIN_BYTE,
    RESULT_UNKNOWN_PACKET,
    RESULT_CONTROL_PACKET,
    RESULT_DATA_PACKET,
    RESULT_ERROR,
} ParsingResult;

typedef struct ParsingContext {
    /* index in the current buffer,
       which depends on the current state */
    int index;
    ParsingState state;
    ParsingResult result;
    KD_PACKET packet;
    PacketData data;
    const char *name;
} ParsingContext;

typedef struct WindbgState {
    bool is_loaded;

    CharBackend chr;

    uint32_t ctrl_packet_id;
    uint32_t data_packet_id;
} WindbgState;

static WindbgState *windbg_state;

static uint32_t compute_checksum(uint8_t *data, uint16_t len)
{
    uint32_t checksum = 0;
    while (len) {
        --len;
        checksum += *data++;
    }
    return checksum;
}

static void windbg_send_data_packet(uint8_t *data, uint16_t byte_count,
                                    uint16_t type)
{
    uint8_t trailing_byte = PACKET_TRAILING_BYTE;

    KD_PACKET packet = {
        .PacketLeader = PACKET_LEADER,
        .PacketType = type,
        .ByteCount = byte_count,
        .PacketId = windbg_state->data_packet_id,
        .Checksum = compute_checksum(data, byte_count)
    };

    packet.PacketType = lduw_p(&packet.PacketType);
    packet.ByteCount = lduw_p(&packet.ByteCount);
    packet.PacketId = ldl_p(&packet.PacketId);
    packet.Checksum = ldl_p(&packet.Checksum);

    qemu_chr_fe_write(&windbg_state->chr, PTR(packet), sizeof(packet));
    qemu_chr_fe_write(&windbg_state->chr, data, byte_count);
    qemu_chr_fe_write(&windbg_state->chr, &trailing_byte,
                      sizeof(trailing_byte));

    windbg_state->data_packet_id ^= 1;
}

static void windbg_send_control_packet(uint16_t type)
{
    KD_PACKET packet = {
        .PacketLeader = CONTROL_PACKET_LEADER,
        .PacketType = type,
        .ByteCount = 0,
        .PacketId = windbg_state->ctrl_packet_id,
        .Checksum = 0
    };

    packet.PacketType = lduw_p(&packet.PacketType);
    packet.PacketId = ldl_p(&packet.PacketId);

    qemu_chr_fe_write(&windbg_state->chr, PTR(packet), sizeof(packet));

    windbg_state->ctrl_packet_id ^= 1;
}

static void windbg_bp_handler(CPUState *cpu)
{
    SizedBuf buf = kd_gen_exception_sc(cpu);
    windbg_send_data_packet(buf.data, buf.size, PACKET_TYPE_KD_STATE_CHANGE64);
    SBUF_FREE(buf);
}

static void windbg_vm_stop(void)
{
    CPUState *cpu = qemu_get_cpu(0);
    vm_stop(RUN_STATE_PAUSED);
    windbg_bp_handler(cpu);
}

static void windbg_process_manipulate_packet(ParsingContext *ctx)
{
    CPUState *cpu;

    ctx->data.extra_size = ctx->packet.ByteCount - M64_SIZE;
    ctx->data.m64.ReturnStatus = STATUS_SUCCESS;

    cpu = qemu_get_cpu(ctx->data.m64.Processor);

    switch (ctx->data.m64.ApiNumber) {

    case DbgKdReadVirtualMemoryApi:
        kd_api_read_virtual_memory(cpu, &ctx->data);
        break;

    case DbgKdWriteVirtualMemoryApi:
        kd_api_write_virtual_memory(cpu, &ctx->data);
        break;

    case DbgKdGetContextApi:
        kd_api_get_context(cpu, &ctx->data);
        break;

    case DbgKdSetContextApi:
        kd_api_set_context(cpu, &ctx->data);
        break;

    case DbgKdWriteBreakPointApi:
        kd_api_write_breakpoint(cpu, &ctx->data);
        break;

    case DbgKdRestoreBreakPointApi:
        kd_api_restore_breakpoint(cpu, &ctx->data);
        break;

    case DbgKdReadIoSpaceApi:
        kd_api_read_io_space(cpu, &ctx->data);
        break;

    case DbgKdWriteIoSpaceApi:
        kd_api_write_io_space(cpu, &ctx->data);
        break;

    case DbgKdContinueApi:
    case DbgKdContinueApi2:
        kd_api_continue(cpu, &ctx->data);
        return;

    case DbgKdReadControlSpaceApi:
        kd_api_read_control_space(cpu, &ctx->data);
        break;

    case DbgKdWriteControlSpaceApi:
        kd_api_write_control_space(cpu, &ctx->data);
        break;

    case DbgKdReadPhysicalMemoryApi:
        kd_api_read_physical_memory(cpu, &ctx->data);
        break;

    case DbgKdWritePhysicalMemoryApi:
        kd_api_write_physical_memory(cpu, &ctx->data);
        break;

    case DbgKdReadMachineSpecificRegister:
        kd_api_read_msr(cpu, &ctx->data);
        break;

    case DbgKdWriteMachineSpecificRegister:
        kd_api_write_msr(cpu, &ctx->data);
        break;

    case DbgKdGetVersionApi:
        kd_api_get_version(cpu, &ctx->data);
        break;

    case DbgKdClearAllInternalBreakpointsApi:
        return;

    case DbgKdSearchMemoryApi:
        kd_api_search_memory(cpu, &ctx->data);
        break;

    case DbgKdFillMemoryApi:
        kd_api_fill_memory(cpu, &ctx->data);
        break;

    case DbgKdQueryMemoryApi:
        kd_api_query_memory(cpu, &ctx->data);
        break;

    default:
        kd_api_unsupported(cpu, &ctx->data);
        break;
    }

    ctx->data.m64.ReturnStatus = ldl_p(&ctx->data.m64.ReturnStatus);

    windbg_send_data_packet(ctx->data.buf, ctx->data.extra_size + M64_SIZE,
                            ctx->packet.PacketType);
}

static void windbg_process_data_packet(ParsingContext *ctx)
{
    switch (ctx->packet.PacketType) {
    case PACKET_TYPE_KD_STATE_MANIPULATE:
        windbg_send_control_packet(PACKET_TYPE_KD_ACKNOWLEDGE);
        windbg_process_manipulate_packet(ctx);
        break;

    default:
        WINDBG_ERROR("Catched unsupported data packet 0x%x",
                     ctx->packet.PacketType);

        windbg_state->ctrl_packet_id = 0;
        windbg_send_control_packet(PACKET_TYPE_KD_RESEND);
        break;
    }
}

static void windbg_process_control_packet(ParsingContext *ctx)
{
    switch (ctx->packet.PacketType) {
    case PACKET_TYPE_KD_ACKNOWLEDGE:
        break;

    case PACKET_TYPE_KD_RESET:
    {
        SizedBuf buf = kd_gen_load_symbols_sc(qemu_get_cpu(0));

        windbg_send_data_packet(buf.data, buf.size,
                                PACKET_TYPE_KD_STATE_CHANGE64);
        windbg_send_control_packet(ctx->packet.PacketType);
        windbg_state->ctrl_packet_id = INITIAL_PACKET_ID;
        SBUF_FREE(buf);
        break;
    }
    default:
        WINDBG_ERROR("Catched unsupported control packet 0x%x",
                     ctx->packet.PacketType);

        windbg_state->ctrl_packet_id = 0;
        windbg_send_control_packet(PACKET_TYPE_KD_RESEND);
        break;
    }
}

static void windbg_ctx_handler(ParsingContext *ctx)
{
    switch (ctx->result) {
    case RESULT_NONE:
        break;

    case RESULT_BREAKIN_BYTE:
        windbg_vm_stop();
        break;

    case RESULT_CONTROL_PACKET:
        windbg_process_control_packet(ctx);
        break;

    case RESULT_DATA_PACKET:
        windbg_process_data_packet(ctx);
        break;

    case RESULT_UNKNOWN_PACKET:
    case RESULT_ERROR:
        windbg_state->ctrl_packet_id = 0;
        windbg_send_control_packet(PACKET_TYPE_KD_RESEND);
        break;

    default:
        break;
    }
}

static void windbg_read_byte(ParsingContext *ctx, uint8_t byte)
{
    switch (ctx->state) {
    case STATE_LEADER:
        ctx->result = RESULT_NONE;
        if (byte == PACKET_LEADER_BYTE || byte == CONTROL_PACKET_LEADER_BYTE) {
            if (ctx->index > 0 && byte != PTR(ctx->packet.PacketLeader)[0]) {
                ctx->index = 0;
            }
            PTR(ctx->packet.PacketLeader)[ctx->index] = byte;
            ++ctx->index;
            if (ctx->index == sizeof(ctx->packet.PacketLeader)) {
                ctx->state = STATE_PACKET_TYPE;
                ctx->index = 0;
            }
        } else if (byte == BREAKIN_PACKET_BYTE) {
            ctx->result = RESULT_BREAKIN_BYTE;
            ctx->index = 0;
        } else {
            ctx->index = 0;
        }
        break;

    case STATE_PACKET_TYPE:
        PTR(ctx->packet.PacketType)[ctx->index] = byte;
        ++ctx->index;
        if (ctx->index == sizeof(ctx->packet.PacketType)) {
            ctx->packet.PacketType = lduw_p(&ctx->packet.PacketType);
            if (ctx->packet.PacketType >= PACKET_TYPE_MAX) {
                ctx->state = STATE_LEADER;
                ctx->result = RESULT_UNKNOWN_PACKET;
            } else {
                ctx->state = STATE_PACKET_BYTE_COUNT;
            }
            ctx->index = 0;
        }
        break;

    case STATE_PACKET_BYTE_COUNT:
        PTR(ctx->packet.ByteCount)[ctx->index] = byte;
        ++ctx->index;
        if (ctx->index == sizeof(ctx->packet.ByteCount)) {
            ctx->packet.ByteCount = lduw_p(&ctx->packet.ByteCount);
            ctx->state = STATE_PACKET_ID;
            ctx->index = 0;
        }
        break;

    case STATE_PACKET_ID:
        PTR(ctx->packet.PacketId)[ctx->index] = byte;
        ++ctx->index;
        if (ctx->index == sizeof(ctx->packet.PacketId)) {
            ctx->packet.PacketId = ldl_p(&ctx->packet.PacketId);
            ctx->state = STATE_PACKET_CHECKSUM;
            ctx->index = 0;
        }
        break;

    case STATE_PACKET_CHECKSUM:
        PTR(ctx->packet.Checksum)[ctx->index] = byte;
        ++ctx->index;
        if (ctx->index == sizeof(ctx->packet.Checksum)) {
            ctx->packet.Checksum = ldl_p(&ctx->packet.Checksum);
            if (ctx->packet.PacketLeader == CONTROL_PACKET_LEADER) {
                ctx->state = STATE_LEADER;
                ctx->result = RESULT_CONTROL_PACKET;
            } else if (ctx->packet.ByteCount > PACKET_MAX_SIZE) {
                ctx->state = STATE_LEADER;
                ctx->result = RESULT_ERROR;
            } else {
                ctx->state = STATE_PACKET_DATA;
            }
            ctx->index = 0;
        }
        break;

    case STATE_PACKET_DATA:
        ctx->data.buf[ctx->index] = byte;
        ++ctx->index;
        if (ctx->index == ctx->packet.ByteCount) {
            ctx->state = STATE_TRAILING_BYTE;
            ctx->index = 0;
        }
        break;

    case STATE_TRAILING_BYTE:
        if (byte == PACKET_TRAILING_BYTE) {
            ctx->result = RESULT_DATA_PACKET;
        } else {
            ctx->result = RESULT_ERROR;
        }
        ctx->state = STATE_LEADER;
        break;
    }
}

static int windbg_chr_can_receive(void *opaque)
{
    return PACKET_MAX_SIZE;
}

static void windbg_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    static ParsingContext ctx = {
        .state = STATE_LEADER,
        .result = RESULT_NONE,
        .name = ""
    };

    if (windbg_state->is_loaded) {
        int i;
        for (i = 0; i < size; i++) {
            windbg_read_byte(&ctx, buf[i]);
            windbg_ctx_handler(&ctx);
        }
    }
}

void windbg_try_load(void)
{
    if (windbg_state && !windbg_state->is_loaded) {
        windbg_state->is_loaded = windbg_on_load();
    }
}

static void windbg_exit(void)
{
    windbg_on_exit();
    g_free(windbg_state);
}

int windbg_server_start(const char *device)
{
    Chardev *chr = NULL;

    if (windbg_state) {
        WINDBG_ERROR("Multiple instances are not supported");
        exit(1);
    }

    windbg_state = g_new0(WindbgState, 1);
    windbg_state->ctrl_packet_id = RESET_PACKET_ID;
    windbg_state->data_packet_id = INITIAL_PACKET_ID;

    chr = qemu_chr_new_noreplay(WINDBG, device);
    if (!chr) {
        return -1;
    }

    qemu_chr_fe_init(&windbg_state->chr, chr, &error_abort);
    qemu_chr_fe_set_handlers(&windbg_state->chr, windbg_chr_can_receive,
                             windbg_chr_receive, NULL, NULL, NULL, NULL, true);

    if (!register_excp_debug_handler(windbg_bp_handler)) {
        exit(1);
    }

    atexit(windbg_exit);
    return 0;
}
