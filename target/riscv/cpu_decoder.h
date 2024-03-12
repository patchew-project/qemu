/*
 * QEMU RISC-V CPU Decoder
 *
 * Copyright (c) 2023-2024 Alibaba Group
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_CPU_DECODER_H
#define RISCV_CPU_DECODER_H

struct DisasContext;
struct RISCVCPUConfig;
typedef struct RISCVDecoder {
    bool (*guard_func)(const struct RISCVCPUConfig *);
    bool (*decode_fn)(struct DisasContext *, uint32_t);
} RISCVDecoder;

typedef bool (*decode_fn)(struct DisasContext *, uint32_t);

extern const size_t decoder_table_size;

extern const RISCVDecoder decoder_table[];
#endif
