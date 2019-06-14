/*
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * plugin-gen.h - TCG-dependent definitions for generating plugin code
 *
 * This header should be included only from plugin.c and C files that emit
 * TCG code.
 */
#ifndef QEMU_PLUGIN_GEN_H
#define QEMU_PLUGIN_GEN_H

#include "qemu/plugin.h"
#include "tcg/tcg.h"

/* used by plugin_callback_start and plugin_callback_end TCG ops */
enum plugin_gen_from {
    PLUGIN_GEN_FROM_TB,
    PLUGIN_GEN_FROM_INSN,
    PLUGIN_GEN_FROM_MEM,
    PLUGIN_GEN_AFTER_INSN,
    PLUGIN_GEN_N_FROMS,
};

struct DisasContextBase;

#ifdef CONFIG_PLUGIN

bool plugin_gen_tb_start(CPUState *cpu, const TranslationBlock *tb);
void plugin_gen_tb_end(CPUState *cpu);
void plugin_gen_insn_start(CPUState *cpu, const struct DisasContextBase *db);
void plugin_gen_insn_end(void);

void plugin_gen_disable_mem_helpers(void);
void plugin_gen_empty_mem_callback(TCGv addr, uint8_t info);

#else /* !CONFIG_PLUGIN */

static inline
bool plugin_gen_tb_start(CPUState *cpu, const TranslationBlock *tb)
{
    return false;
}

static inline
void plugin_gen_insn_start(CPUState *cpu, const struct DisasContextBase *db)
{ }

static inline void plugin_gen_insn_end(void)
{ }

static inline void plugin_gen_tb_end(CPUState *cpu)
{ }

static inline void plugin_gen_disable_mem_helpers(void)
{ }

static inline void plugin_gen_empty_mem_callback(TCGv addr, uint8_t info)
{ }

#endif /* CONFIG_PLUGIN */

#endif /* QEMU_PLUGIN_GEN_H */

