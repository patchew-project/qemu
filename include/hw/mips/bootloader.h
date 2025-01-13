/*
 * Utility for QEMU MIPS to generate it's simple bootloader
 *
 * Copyright (C) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MIPS_BOOTLOADER_H
#define HW_MIPS_BOOTLOADER_H

#include "exec/cpu-defs.h"
#include "target/mips/cpu-qom.h"

/**
 * bl_gen_jump_to: Generate bootloader code to jump to an address
 *
 * @ptr: Pointer to buffer where to write the bootloader code
 * @jump_addr: Address to jump to
 */
void bl_gen_jump_to(void **ptr, target_ulong jump_addr);

/**
 * bl_gen_jump_kernel: Generate bootloader code to jump to a Linux kernel
 *
 * @ptr: Pointer to buffer where to write the bootloader code
 * @set_sp: Whether to set $sp register
 * @set_a0: Whether to set $a0 register
 * @set_a1: Whether to set $a1 register
 * @set_a2: Whether to set $a2 register
 * @set_a3: Whether to set $a3 register
 * @sp: Value to set $sp to if @set_sp is set
 * @a0: Value to set $a0 to if @set_a0 is set
 * @a1: Value to set $a0 to if @set_a1 is set
 * @a2: Value to set $a0 to if @set_a2 is set
 * @a3: Value to set $a0 to if @set_a3 is set
 * @kernel_addr: Start address of the kernel to jump to
 */
void bl_gen_jump_kernel(void **ptr,
                        bool set_sp, target_ulong sp,
                        bool set_a0, target_ulong a0,
                        bool set_a1, target_ulong a1,
                        bool set_a2, target_ulong a2,
                        bool set_a3, target_ulong a3,
                        target_ulong kernel_addr);

/**
 * bl_gen_write_ulong: Generate bootloader code to write an unsigned long
 *                     value at an address
 *
 * @cpu: The MIPS CPU which will run the bootloader code
 * @ptr: Pointer to buffer where to write the bootloader code
 * @addr: Address to write to
 * @val: Value to write at @addr
 */
void bl_gen_write_ulong(const MIPSCPU *cpu, void **ptr,
                        target_ulong addr, target_ulong val);

/**
 * bl_gen_write_u32: Generate bootloader code to write a 32-bit unsigned
 *                   value at an address
 *
 * @cpu: The MIPS CPU which will run the bootloader code
 * @ptr: Pointer to buffer where to write the bootloader code
 * @addr: Address to write to
 * @val: Value to write at @addr
 */
void bl_gen_write_u32(const MIPSCPU *cpu, void **ptr,
                      target_ulong addr, uint32_t val);

/**
 * bl_gen_write_u64: Generate bootloader code to write a 64-bit unsigned
 *                   value at an address
 *
 * @cpu: The MIPS CPU which will run the bootloader code
 * @ptr: Pointer to buffer where to write the bootloader code
 * @addr: Address to write to
 * @val: Value to write at @addr
 */
void bl_gen_write_u64(const MIPSCPU *cpu, void **ptr,
                      target_ulong addr, uint64_t val);

#endif
