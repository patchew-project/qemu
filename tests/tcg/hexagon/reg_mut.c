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

int err;

#define check_ne(N, EXPECT) \
    { uint32_t value = N; \
        if (value == EXPECT) { \
            printf("ERROR: \"%s\" 0x%04x == 0x%04x at %s:%d\n", #N, value, \
                EXPECT, __FILE__, __LINE__); \
            err++; \
        } \
    }

#define check(N, EXPECT) \
    { uint32_t value = N; \
        if (value != EXPECT) { \
            printf("ERROR: \"%s\" 0x%04x != 0x%04x at %s:%d\n", #N, value, \
                  EXPECT, __FILE__, __LINE__); \
            err++; \
        } \
    }

#define READ_REG(reg_name, out_reg) \
  asm volatile ("%0 = " reg_name "\n\t" \
                : "=r"(out_reg) \
                : \
                : \
                ); \

#define WRITE_REG(reg_name, out_reg, in_reg) \
  asm volatile (reg_name " = %1\n\t" \
                "%0 = " reg_name "\n\t" \
                : "=r"(out_reg) \
                : "r"(in_reg) \
                : reg_name \
                ); \

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
#define PC_EQ_R0 ".word 0x6220c009\n\t"

static inline uint32_t set_pc(uint32_t in_reg)
{
    uint32_t out_reg;
    asm volatile("r0 = %1\n\t"
                 PC_EQ_R0
                 "%0 = pc\n\t"
                : "=r"(out_reg)
                : "r"(in_reg)
                : "r0");
    return out_reg;
}

static inline uint32_t set_usr(uint32_t in_reg)
{
    uint32_t out_reg;
    WRITE_REG("usr", out_reg, in_reg);
    return out_reg;
}

int main()
{
    check(set_usr(0x00),       0x00);
    check(set_usr(0xffffffff), 0x3ecfff3f);
    check(set_usr(0x00),       0x00);
    check(set_usr(0x01),       0x01);
    check(set_usr(0xff),       0x3f);

    /*
     * PC is special.  Setting it to these values
     * should cause an instruction fetch miss.
     */
    check_ne(set_pc(0x00000000), 0x00000000);
    check_ne(set_pc(0xffffffff), 0xffffffff);
    check_ne(set_pc(0x00000001), 0x00000001);
    check_ne(set_pc(0x000000ff), 0x000000ff);

    puts(err ? "FAIL" : "PASS");
    return err;
}
