#ifndef HW_MIPS_BOOTLOADER_H
#define HW_MIPS_BOOTLOADER_H

#include "exec/cpu-defs.h"

void bl_gen_jump_to(uint32_t **p, target_ulong jump_addr);
void bl_gen_jump_kernel(uint32_t **p, target_ulong sp, target_ulong a0,
                        target_ulong a1, target_ulong a2, target_ulong a3,
                        target_ulong kernel_addr);
void bl_gen_write_ulong(uint32_t **p, target_ulong val, target_ulong addr);
void bl_gen_write_u32(uint32_t **p, uint32_t val, target_ulong addr);
void bl_gen_write_u64(uint32_t **p, uint64_t val, target_ulong addr);

typedef enum bl_reg {
    BL_REG_ZERO = 0,
    BL_REG_AT = 1,
    BL_REG_V0 = 2,
    BL_REG_V1 = 3,
    BL_REG_A0 = 4,
    BL_REG_A1 = 5,
    BL_REG_A2 = 6,
    BL_REG_A3 = 7,
    BL_REG_T0 = 8,
    BL_REG_T1 = 9,
    BL_REG_T2 = 10,
    BL_REG_T3 = 11,
    BL_REG_T4 = 12,
    BL_REG_T5 = 13,
    BL_REG_T6 = 14,
    BL_REG_T7 = 15,
    BL_REG_S0 = 16,
    BL_REG_S1 = 17,
    BL_REG_S2 = 18,
    BL_REG_S3 = 19,
    BL_REG_S4 = 20,
    BL_REG_S5 = 21,
    BL_REG_S6 = 22,
    BL_REG_S7 = 23,
    BL_REG_T8 = 24,
    BL_REG_T9 = 25,
    BL_REG_K0 = 26,
    BL_REG_K1 = 27,
    BL_REG_GP = 28,
    BL_REG_SP = 29,
    BL_REG_FP = 30,
    BL_REG_RA = 31,
} bl_reg;

#endif
