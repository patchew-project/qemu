/*
 * Copyright (c) 2024-2025 Michael Clark
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "disas/x86.h"

static size_t format_hex(char *buf, size_t buflen, uchar *data, size_t nbytes)
{
    size_t len = 0;
    size_t indent = 1;

    const size_t hexcols = 10;

    for (size_t i = 0; i < nbytes; i++) {
        len += snprintf(buf + len, buflen - len, " %02x" + (i == 0), data[i]);
    }
    if (hexcols - nbytes < hexcols) {
        indent = (hexcols - nbytes) * 3 + 8 - (hexcols * 3) % 8;
    }
    for (size_t i = 0; i < indent && len < (buflen - 1); i++) {
        buf[len++] = ' ';
    }
    buf[len] = '\0';

    return len;
}

static size_t format_symbol(char *buf, size_t buflen, x86_codec *c,
    size_t pc_offset)
{
    ullong addr = pc_offset + c->imm32;
    return snprintf(buf, buflen, " # 0x%llx", addr);
}

int print_insn_x86(bfd_vma memaddr, struct disassemble_info *info)
{
    x86_buffer buf;
    x86_codec codec;
    x86_ctx *ctx;
    bfd_byte *packet;
    size_t nfetch, ndecode, len;
    char str[128];
    int ret;

    static const size_t max_fetch_len = 16;

    /* read instruction */
    nfetch = info->buffer_vma + info->buffer_length - memaddr;
    if (nfetch > max_fetch_len) {
        nfetch = max_fetch_len;
    }
    packet = alloca(nfetch);
    ret = (*info->read_memory_func)(memaddr, packet, nfetch, info);
    if (ret != 0) {
        (*info->memory_error_func)(ret, memaddr, info);
        return ret;
    }

    /* decode instruction */
    ctx = (x86_ctx *)info->private_data;
    x86_buffer_init_ex(&buf, packet, 0, nfetch);
    ret = x86_codec_read(ctx, &buf, &codec, &ndecode);
    if (ret != 0) {
        return -1;
    }

    /* format instruction */
    len = format_hex(str, sizeof(str), packet, ndecode);
    x86_format_op_symbol(str + len, sizeof(str) - len, ctx, &codec,
        memaddr + ndecode, format_symbol);
    (*info->fprintf_func)(info->stream, "%s", str);

    return ndecode;
}
