/*
 *  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * QEMU Hexagon Disassembler
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "target/hexagon/cpu_bits.h"

/*
 * We will disassemble a packet with up to 4 instructions, so we need
 * a hefty size buffer.
 */
#define PACKET_BUFFER_LEN                   1028

int print_insn_hexagon(bfd_vma memaddr, struct disassemble_info *info)
{
    uint32_t words[PACKET_WORDS_MAX];
    bool found_end = false;
    GString *buf;
    int i, len;

    for (i = 0; i < PACKET_WORDS_MAX && !found_end; i++) {
        int status = (*info->read_memory_func)(memaddr + i * sizeof(uint32_t),
                                               (bfd_byte *)&words[i],
                                               sizeof(uint32_t), info);
        if (status) {
            if (i > 0) {
                break;
            }
            (*info->memory_error_func)(status, memaddr, info);
            return status;
        }
        if (is_packet_end(words[i])) {
            found_end = true;
        }
    }

    if (!found_end) {
        (*info->fprintf_func)(info->stream, "<invalid>");
        return PACKET_WORDS_MAX * sizeof(uint32_t);
    }

    buf = g_string_sized_new(PACKET_BUFFER_LEN);
    len = disassemble_hexagon(words, i, memaddr, buf);
    (*info->fprintf_func)(info->stream, "%s", buf->str);
    g_string_free(buf, true);

    return len;
}
