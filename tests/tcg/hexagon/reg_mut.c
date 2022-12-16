
/*
 *  Copyright(c) 2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>

#include "hex_regs.h"

static int err;

enum {
    HEX_REG_PAIR_C9_8,
    HEX_REG_PAIR_C11_10,
    HEX_REG_PAIR_C15_14,
    HEX_REG_PAIR_C31_30,
};

#define check(N, EXPECT) \
    do { \
        uint64_t value = N; \
        uint64_t expect = EXPECT; \
        if (value != EXPECT) { \
            printf("ERROR: \"%s\" 0x%04llx != 0x%04llx at %s:%d\n", #N, value, \
                   expect, __FILE__, __LINE__); \
            err++; \
        } \
    } while (0)

#define check_ne(N, EXPECT) \
    do { \
        uint64_t value = N; \
        uint64_t expect = EXPECT; \
        if (value == EXPECT) { \
            printf("ERROR: \"%s\" 0x%04llx == 0x%04llx at %s:%d\n", #N, value, \
                   expect, __FILE__, __LINE__); \
            err++; \
        } \
    } while (0)

#define WRITE_REG(reg_name, output, input) \
    asm volatile(reg_name " = %1\n\t" \
                 "%0 = " reg_name "\n\t" \
                 : "=r"(output) \
                 : "r"(input) \
                 : reg_name);

#define WRITE_REG_IN_PACKET(reg_name, output, input) \
    asm volatile("{ " reg_name " = %1 }\n\t" \
                 "%0 = " reg_name "\n\t" \
                 : "=r"(output) \
                 : "r"(input) \
                 : reg_name);

#define WRITE_REG_ENCODED(reg_name, encoding, output, input) \
    asm volatile("r0 = %1\n\t" \
                 encoding \
                 "%0 = " reg_name "\n\t" \
                 : "=r"(output) \
                 : "r"(input) \
                 : "r0");

#define WRITE_REG_ENCODED_IN_PACKET(reg_name, encoding, output, input) \
    asm volatile("{ r0 = %1 }\n\t" \
                 encoding \
                 "%0 = " reg_name "\n\t" \
                 : "=r"(output) \
                 : "r"(input) \
                 : "r0");

#define WRITE_REG_PAIR_ENCODED(reg_name, encoding, output, input) \
    asm volatile("r1:0 = %1\n\t" \
                 encoding \
                 "%0 = " reg_name "\n\t" \
                 : "=r"(output) \
                 : "r"(input) \
                 : "r1:0");

#define WRITE_REG_PAIR_ENCODED_IN_PACKET(reg_name, encoding, output, input) \
    asm volatile("{ r1:0 = %1 }\n\t" \
                 encoding \
                 "%0 = " reg_name "\n\t" \
                 : "=r"(output) \
                 : "r"(input) \
                 : "r1:0");

/*
 * Instruction word: { pc = r0 }
 *
 * This instruction is barred by the assembler.
 *
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    Opc[A2_tfrrcr]   | Src[R0] |P P|                 |  C9/PC  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define PC_EQ_R0        ".word 0x6220c009\n\t"
#define GP_EQ_R0        ".word 0x6220c00b\n\t"
#define UPCYCLELO_EQ_R0 ".word 0x6220c00e\n\t"
#define UPCYCLEHI_EQ_R0 ".word 0x6220c00f\n\t"
#define UTIMERLO_EQ_R0  ".word 0x6220c01e\n\t"
#define UTIMERHI_EQ_R0  ".word 0x6220c01f\n\t"

#define C9_8_EQ_R1_0    ".word 0x6320c008\n\t"
#define C11_10_EQ_R1_0  ".word 0x6320c00a\n\t"
#define C15_14_EQ_R1_0  ".word 0x6320c00e\n\t"
#define C31_30_EQ_R1_0  ".word 0x6320c01e\n\t"

static inline uint32_t write_reg(int rnum, uint32_t val)
{
    uint32_t result;
    switch (rnum) {
    case HEX_REG_USR:
        WRITE_REG("usr", result, val);
        break;
    case HEX_REG_PC:
        WRITE_REG_ENCODED("pc", PC_EQ_R0, result, val);
        break;
    case HEX_REG_GP:
        WRITE_REG_ENCODED("gp", GP_EQ_R0, result, val);
        break;
    case HEX_REG_UPCYCLELO:
        WRITE_REG_ENCODED("upcyclelo", UPCYCLELO_EQ_R0, result, val);
        break;
    case HEX_REG_UPCYCLEHI:
        WRITE_REG_ENCODED("upcyclehi", UPCYCLEHI_EQ_R0, result, val);
        break;
    case HEX_REG_UTIMERLO:
        WRITE_REG_ENCODED("utimerlo", UTIMERLO_EQ_R0, result, val);
        break;
    case HEX_REG_UTIMERHI:
        WRITE_REG_ENCODED("utimerhi", UTIMERHI_EQ_R0, result, val);
        break;
    }
    return result;
}

static inline uint32_t write_reg_in_packet(int rnum, uint32_t val)
{
    uint32_t result;
    switch (rnum) {
    case HEX_REG_USR:
        WRITE_REG_IN_PACKET("usr", result, val);
        break;
    case HEX_REG_PC:
        WRITE_REG_ENCODED_IN_PACKET("pc", PC_EQ_R0, result, val);
        break;
    case HEX_REG_GP:
        WRITE_REG_ENCODED_IN_PACKET("gp", GP_EQ_R0, result, val);
        break;
    case HEX_REG_UPCYCLELO:
        WRITE_REG_ENCODED_IN_PACKET("upcyclelo", UPCYCLELO_EQ_R0, result, val);
        break;
    case HEX_REG_UPCYCLEHI:
        WRITE_REG_ENCODED_IN_PACKET("upcyclehi", UPCYCLEHI_EQ_R0, result, val);
        break;
    case HEX_REG_UTIMERLO:
        WRITE_REG_ENCODED_IN_PACKET("utimerlo", UTIMERLO_EQ_R0, result, val);
        break;
    case HEX_REG_UTIMERHI:
        WRITE_REG_ENCODED_IN_PACKET("utimerhi", UTIMERHI_EQ_R0, result, val);
        break;
    }
    return result;
}

static inline uint64_t write_reg_pair(int rnum, uint32_t val_hi,
                                      uint32_t val_lo)
{
    uint64_t val = (uint64_t) val_hi << 32 | val_lo;
    uint64_t result;
    switch (rnum) {
    case HEX_REG_PAIR_C9_8:
        WRITE_REG_PAIR_ENCODED("c9:8", C9_8_EQ_R1_0, result, val);
        break;
    case HEX_REG_PAIR_C11_10:
        WRITE_REG_PAIR_ENCODED("c11:10", C11_10_EQ_R1_0, result, val);
        break;
    case HEX_REG_PAIR_C15_14:
        WRITE_REG_PAIR_ENCODED("c15:14", C15_14_EQ_R1_0, result, val);
        break;
    case HEX_REG_PAIR_C31_30:
        WRITE_REG_PAIR_ENCODED("c31:30", C31_30_EQ_R1_0, result, val);
        break;
    }
    return result;
}

static inline uint64_t write_reg_pair_in_packet(int rnum, uint32_t val_hi,
                                                uint32_t val_lo)
{
    uint64_t val = (uint64_t) val_hi << 32 | val_lo;
    uint64_t result;
    switch (rnum) {
    case HEX_REG_PAIR_C9_8:
        WRITE_REG_PAIR_ENCODED_IN_PACKET("c9:8", C9_8_EQ_R1_0, result, val);
        break;
    case HEX_REG_PAIR_C11_10:
        WRITE_REG_PAIR_ENCODED_IN_PACKET("c11:10", C11_10_EQ_R1_0, result, val);
        break;
    case HEX_REG_PAIR_C15_14:
        WRITE_REG_PAIR_ENCODED_IN_PACKET("c15:14", C15_14_EQ_R1_0, result, val);
        break;
    case HEX_REG_PAIR_C31_30:
        WRITE_REG_PAIR_ENCODED_IN_PACKET("c31:30", C31_30_EQ_R1_0, result, val);
        break;
    }
    return result;
}

static inline void write_control_registers(void)
{
    check(write_reg(HEX_REG_USR,        0xffffffff), 0x3ecfff3f);
    check(write_reg(HEX_REG_GP,         0xffffffff), 0xffffffc0);
    check(write_reg(HEX_REG_UPCYCLELO,  0xffffffff),        0x0);
    check(write_reg(HEX_REG_UPCYCLEHI,  0xffffffff),        0x0);
    check(write_reg(HEX_REG_UTIMERLO,   0xffffffff),        0x0);
    check(write_reg(HEX_REG_UTIMERHI,   0xffffffff),        0x0);

    /*
     * PC is special.  Setting it to these values
     * should cause a catastrophic failure.
     */
    check_ne(write_reg(HEX_REG_PC, 0x00000000), 0x00000000);
    check_ne(write_reg(HEX_REG_PC, 0x00000000), 0x00000001);
    check_ne(write_reg(HEX_REG_PC, 0xffffffff), 0xffffffff);
    check_ne(write_reg(HEX_REG_PC, 0x00000000), 0x00000000);
}

static inline void write_control_registers_in_packets(void)
{
    check(write_reg_in_packet(HEX_REG_USR,        0xffffffff), 0x3ecfff3f);
    check(write_reg_in_packet(HEX_REG_GP,         0xffffffff), 0xffffffc0);
    check(write_reg_in_packet(HEX_REG_UPCYCLELO,  0xffffffff),        0x0);
    check(write_reg_in_packet(HEX_REG_UPCYCLEHI,  0xffffffff),        0x0);
    check(write_reg_in_packet(HEX_REG_UTIMERLO,   0xffffffff),        0x0);
    check(write_reg_in_packet(HEX_REG_UTIMERHI,   0xffffffff),        0x0);

    check_ne(write_reg_in_packet(HEX_REG_PC, 0x00000000), 0x00000000);
    check_ne(write_reg_in_packet(HEX_REG_PC, 0x00000001), 0x00000001);
    check_ne(write_reg_in_packet(HEX_REG_PC, 0xffffffff), 0xffffffff);
    check_ne(write_reg_in_packet(HEX_REG_PC, 0x00000000), 0x00000000);
}

static inline void write_control_register_pairs(void)
{
    check(write_reg_pair(HEX_REG_PAIR_C11_10, 0xffffffff, 0xffffffff),
          0xffffffc0ffffffff);
    check(write_reg_pair(HEX_REG_PAIR_C15_14, 0xffffffff, 0xffffffff), 0x0);
    check(write_reg_pair(HEX_REG_PAIR_C31_30, 0xffffffff, 0xffffffff), 0x0);

    check_ne(write_reg_pair(HEX_REG_PAIR_C9_8, 0x00000000, 0x00000000),
             0x0000000000000000);
    check_ne(write_reg_pair(HEX_REG_PAIR_C9_8, 0x00000001, 0x00000000),
             0x0000000100000000);
    check_ne(write_reg_pair(HEX_REG_PAIR_C9_8, 0xffffffff, 0xffffffff),
             0xffffffffffffffff);
    check_ne(write_reg_pair(HEX_REG_PAIR_C9_8, 0x00000000, 0x00000000),
             0x0000000000000000);
}

static inline void write_control_register_pairs_in_packets(void)
{
    check(write_reg_pair_in_packet(HEX_REG_PAIR_C11_10, 0xffffffff, 0xffffffff),
          0xffffffc0ffffffff);
    check(write_reg_pair_in_packet(HEX_REG_PAIR_C15_14, 0xffffffff, 0xffffffff),
          0x0);
    check(write_reg_pair_in_packet(HEX_REG_PAIR_C31_30, 0xffffffff, 0xffffffff),
          0x0);

    check_ne(write_reg_pair_in_packet(HEX_REG_PAIR_C9_8, 0x00000000,
             0x00000000), 0x0000000000000000);
    check_ne(write_reg_pair_in_packet(HEX_REG_PAIR_C9_8, 0x00000001,
             0x00000000), 0x0000000100000000);
    check_ne(write_reg_pair_in_packet(HEX_REG_PAIR_C9_8, 0xffffffff,
             0xffffffff), 0xffffffffffffffff);
    check_ne(write_reg_pair_in_packet(HEX_REG_PAIR_C9_8, 0x00000000,
             0x00000000), 0x0000000000000000);
}

int main()
{
    err = 0;

    write_control_registers();
    write_control_registers_in_packets();
    write_control_register_pairs();
    write_control_register_pairs_in_packets();

    puts(err ? "FAIL" : "PASS");
    return err;
}
