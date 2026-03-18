/*
 * QEMU RISC-V SPMP (S-mode Physical Memory Protection)
 *
 * Author:
 *   Luís Cunha <luisccunha8@gmail.com>
 *
 * Based on an earlier SPMP prototype by:
 *   Bicheng Yang <SuperYbc@outlook.com>
 *   Dong Du      <Ddnirvana1@gmail.com>
 *
 * This provides a RISC-V S-mode Physical Memory Protection interface.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef RISCV_SPMP_H
#define RISCV_SPMP_H


typedef enum {
    SPMP_READ   = (1 << 0),
    SPMP_WRITE  = (1 << 1),
    SPMP_EXEC   = (1 << 2),
    SPMP_AMATCH = (3 << 3),
    SPMP_LOCK   = (1 << 7),
    SPMP_UMODE  = (1 << 8),
    SPMP_SHARED = (1 << 9)
} spmp_priv_t;

typedef enum {
    SPMP_AMATCH_OFF,  /* Null (off)                            */
    SPMP_AMATCH_TOR,  /* Top of Range                          */
    SPMP_AMATCH_NA4,  /* Naturally aligned four-byte region    */
    SPMP_AMATCH_NAPOT /* Naturally aligned power-of-two region */
} spmp_am_t;

typedef struct {
    target_ulong addr_reg;
    uint16_t  cfg_reg;
} spmp_entry_t;

typedef struct {
    target_ulong sa;
    target_ulong ea;
} spmp_addr_t;

typedef struct {
    spmp_entry_t spmp[MAX_RISCV_SPMPS];
    spmp_addr_t  addr[MAX_RISCV_SPMPS];

    uint8_t     num_active_rules;
    uint8_t     num_deleg_rules;
    uint64_t    spmpen;
    uint64_t    locked_rules;
} spmp_table_t;

void spmpcfg_csr_write(CPURISCVState *env, uint32_t reg_index,
    target_ulong val, bool m_mode_access);
target_ulong spmpcfg_csr_read(CPURISCVState *env, uint32_t reg_index);

target_ulong spmpaddr_csr_read(CPURISCVState *env, uint32_t addr_index);
void spmpaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
    target_ulong val, bool m_mode_access);

void sspmpen_csr_write(CPURISCVState *env, uint64_t new_val);

bool spmp_hart_has_privs(CPURISCVState *env, target_ulong addr,
    target_ulong size, spmp_priv_t privs, spmp_priv_t *allowed_privs,
    target_ulong mode);
int spmp_priv_to_page_prot(spmp_priv_t spmp_priv);
void spmp_unlock_entries(CPURISCVState *env);

void spmp_decode_napot(target_ulong a, target_ulong *sa, target_ulong *ea);
uint8_t spmp_get_a_field(uint8_t cfg);

#endif
