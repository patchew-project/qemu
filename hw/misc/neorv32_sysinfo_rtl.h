/*
 * NEORV32 RTL specific definitions.
 *
 * Copyright (c) 2025 Michael Levit
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * NEORV32: neorv32_sysinfo.h - System Information Memory (SYSINFO) HW driver.
 *
 * BSD 3-Clause License.
 *
 * Copyright (c) 2023, Stephan Nolting.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The NEORV32 Processor: https://github.com/stnolting/neorv32
 */

#ifndef NEORV32_SYSINFO_RTL_H
#define NEORV32_SYSINFO_RTL_H

/*
 * IO Device: System Configuration Information Memory (SYSINFO).
 */

typedef struct __attribute__((packed, aligned(4))) {
    uint32_t CLK;         /* Offset 0: Clock speed in Hz. */
    const uint32_t MISC;  /*
                           * Offset 4: Misc system configuration bits.
                           * See enum NEORV32_SYSINFO_MISC_enum.
                           */
    const uint32_t SOC;   /*
                           * Offset 8: Implemented SoC features.
                           * See enum NEORV32_SYSINFO_SOC_enum.
                           */
    const uint32_t CACHE; /*
                           * Offset 12: Cache configuration.
                           * See enum NEORV32_SYSINFO_CACHE_enum.
                           */
} neorv32_sysinfo_t;

/* SYSINFO module hardware access. */
#define NEORV32_SYSINFO ((neorv32_sysinfo_t *)(NEORV32_SYSINFO_BASE))

/*
 * NEORV32_SYSINFO.MISC (r/-): Miscellaneous system configurations.
 */
enum NEORV32_SYSINFO_MISC_enum {
    /* log2(internal IMEM size in bytes) (via IMEM_SIZE generic). */
    SYSINFO_MISC_IMEM_LSB = 0,   /* LSB. */
    SYSINFO_MISC_IMEM_MSB = 7,   /* MSB. */

    /* log2(internal DMEM size in bytes) (via DMEM_SIZE generic). */
    SYSINFO_MISC_DMEM_LSB = 8,   /* LSB. */
    SYSINFO_MISC_DMEM_MSB = 15,  /* MSB. */

    /* Number of physical CPU cores ("harts"). */
    SYSINFO_MISC_HART_LSB = 16,  /* LSB. */
    SYSINFO_MISC_HART_MSB = 19,  /* MSB. */

    /* Boot mode configuration (via BOOT_MODE_SELECT generic). */
    SYSINFO_MISC_BOOT_LSB = 20,  /* LSB. */
    SYSINFO_MISC_BOOT_MSB = 21,  /* MSB. */

    /* log2(internal bus timeout cycles). */
    SYSINFO_MISC_ITMO_LSB = 22,  /* LSB. */
    SYSINFO_MISC_ITMO_MSB = 26,  /* MSB. */

    /* log2(external bus timeout cycles). */
    SYSINFO_MISC_ETMO_LSB = 27,  /* LSB. */
    SYSINFO_MISC_ETMO_MSB = 31   /* MSB. */
};

/*
 * NEORV32_SYSINFO.SOC (r/-): Implemented processor devices/features.
 */
enum NEORV32_SYSINFO_SOC_enum {
    /* Bootloader implemented when 1 (via BOOT_MODE_SELECT). */
    SYSINFO_SOC_BOOTLOADER = 0,

    /* External bus interface implemented when 1 (via XBUS_EN). */
    SYSINFO_SOC_XBUS = 1,

    /* Instruction memory implemented when 1 (via IMEM_EN). */
    SYSINFO_SOC_IMEM = 2,

    /* Data memory implemented when 1 (via DMEM_EN). */
    SYSINFO_SOC_DMEM = 3,

    /* On-chip debugger implemented when 1 (via OCD_EN). */
    SYSINFO_SOC_OCD = 4,

    /* Instruction cache implemented when 1 (via ICACHE_EN). */
    SYSINFO_SOC_ICACHE = 5,

    /* Data cache implemented when 1 (via DCACHE_EN). */
    SYSINFO_SOC_DCACHE = 6,

    /* Reserved. */
    /* SYSINFO_SOC_reserved = 7, */

    /* Reserved. */
    /* SYSINFO_SOC_reserved = 8, */

    /* Reserved. */
    /* SYSINFO_SOC_reserved = 9, */

    /* Reserved. */
    /* SYSINFO_SOC_reserved = 10, */

    /* On-chip debugger authentication when 1 (via OCD_AUTHENTICATION). */
    SYSINFO_SOC_OCD_AUTH = 11,

    /*
     * Instruction memory as pre-initialized ROM when 1
     * (via BOOT_MODE_SELECT).
     */
    SYSINFO_SOC_IMEM_ROM = 12,

    /* Two-wire device implemented when 1 (via IO_TWD_EN). */
    SYSINFO_SOC_IO_TWD = 13,

    /* Direct memory access controller when 1 (via IO_DMA_EN). */
    SYSINFO_SOC_IO_DMA = 14,

    /* General purpose I/O port when 1 (via IO_GPIO_EN). */
    SYSINFO_SOC_IO_GPIO = 15,

    /* Core local interruptor when 1 (via IO_CLINT_EN). */
    SYSINFO_SOC_IO_CLINT = 16,

    /* UART0 when 1 (via IO_UART0_EN). */
    SYSINFO_SOC_IO_UART0 = 17,

    /* SPI when 1 (via IO_SPI_EN). */
    SYSINFO_SOC_IO_SPI = 18,

    /* TWI when 1 (via IO_TWI_EN). */
    SYSINFO_SOC_IO_TWI = 19,

    /* PWM unit when 1 (via IO_PWM_EN). */
    SYSINFO_SOC_IO_PWM = 20,

    /* Watchdog timer when 1 (via IO_WDT_EN). */
    SYSINFO_SOC_IO_WDT = 21,

    /* Custom functions subsystem when 1 (via IO_CFS_EN). */
    SYSINFO_SOC_IO_CFS = 22,

    /* True random number generator when 1 (via IO_TRNG_EN). */
    SYSINFO_SOC_IO_TRNG = 23,

    /* Serial data interface when 1 (via IO_SDI_EN). */
    SYSINFO_SOC_IO_SDI = 24,

    /* UART1 when 1 (via IO_UART1_EN). */
    SYSINFO_SOC_IO_UART1 = 25,

    /* NeoPixel-compatible smart LED IF when 1 (via IO_NEOLED_EN). */
    SYSINFO_SOC_IO_NEOLED = 26,

    /* Execution tracer when 1 (via IO_TRACER_EN). */
    SYSINFO_SOC_IO_TRACER = 27,

    /* General purpose timer when 1 (via IO_GPTMR_EN). */
    SYSINFO_SOC_IO_GPTMR = 28,

    /* Stream link interface when 1 (via IO_SLINK_EN). */
    SYSINFO_SOC_IO_SLINK = 29,

    /* 1-wire interface controller when 1 (via IO_ONEWIRE_EN). */
    SYSINFO_SOC_IO_ONEWIRE = 30

    /* Reserved. */
    /* SYSINFO_SOC_reserved = 31 */
};

/*
 * NEORV32_SYSINFO.CACHE (r/-): Cache configuration.
 */
enum NEORV32_SYSINFO_CACHE_enum {
    /* I-cache: log2(block size in bytes), bit 0 (via CACHE_BLOCK_SIZE). */
    SYSINFO_CACHE_INST_BLOCK_SIZE_0 = 0,

    /* I-cache: log2(block size in bytes), bit 3 (via CACHE_BLOCK_SIZE). */
    SYSINFO_CACHE_INST_BLOCK_SIZE_3 = 3,

    /* I-cache: log2(number of cache blocks), bit 0 (via ICACHE_NUM_BLOCKS). */
    SYSINFO_CACHE_INST_NUM_BLOCKS_0 = 4,

    /* I-cache: log2(number of cache blocks), bit 3 (via ICACHE_NUM_BLOCKS). */
    SYSINFO_CACHE_INST_NUM_BLOCKS_3 = 7,

    /* D-cache: log2(block size in bytes), bit 0 (via CACHE_BLOCK_SIZE). */
    SYSINFO_CACHE_DATA_BLOCK_SIZE_0 = 8,

    /* D-cache: log2(block size in bytes), bit 3 (via CACHE_BLOCK_SIZE). */
    SYSINFO_CACHE_DATA_BLOCK_SIZE_3 = 11,

    /* D-cache: log2(number of cache blocks), bit 0 (via DCACHE_NUM_BLOCKS). */
    SYSINFO_CACHE_DATA_NUM_BLOCKS_0 = 12,

    /* D-cache: log2(number of cache blocks), bit 3 (via DCACHE_NUM_BLOCKS). */
    SYSINFO_CACHE_DATA_NUM_BLOCKS_3 = 15,

    /* I-cache: issue burst transfers on update (via CACHE_BURSTS_EN). */
    SYSINFO_CACHE_INST_BURSTS_EN = 16,

    /* D-cache: issue burst transfers on update (via CACHE_BURSTS_EN). */
    SYSINFO_CACHE_DATA_BURSTS_EN = 24
};

#endif /* NEORV32_SYSINFO_RTL_H */
