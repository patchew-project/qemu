/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#define QEMU_GENERATE
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "internal.h"
#include "tcg/tcg-op.h"
#include "insn.h"
#include "opcodes.h"
#include "translate.h"
#include "macros.h"
#include "genptr_helpers.h"

#define DEF_TCG_FUNC(TAG, GENFN) \
static void generate_##TAG(CPUHexagonState *env, DisasContext *ctx, \
                           insn_t *insn, packet_t *pkt) \
{ \
    GENFN \
}
#include "tcg_funcs_generated.h"
#undef DEF_TCG_FUNC


/* Fill in the table with NULLs because not all the opcodes have DEF_QEMU */
semantic_insn_t opcode_genptr[] = {
#define OPCODE(X)                              NULL
#include "opcodes_def_generated.h"
    NULL
#undef OPCODE
};

/* This function overwrites the NULL entries where we have a DEF_QEMU */
void init_genptr(void)
{
#define DEF_TCG_FUNC(TAG, GENFN) \
    opcode_genptr[TAG] = generate_##TAG;
#include "tcg_funcs_generated.h"
#undef DEF_TCG_FUNC
}
