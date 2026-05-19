/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * MIPS Octeon crypto emulation helpers.
 *
 * Copyright (c) 2026 James Hilliard
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internal.h"
#include "exec/helper-proto.h"
#include "crypto/aes.h"
#include "crypto/clmul.h"
#include "crypto/sm4.h"
#include "qemu/bitops.h"
#include "qemu/host-utils.h"

/*
 * The shared HSH/SHA3/SNOW3G/ZUC register window needs selector-position
 * arithmetic.  Instruction dispatch itself is still fully decoded by
 * decodetree and calls per-operation helpers.
 */
#define OCTEON_HSH_DATW(N)          (0x0240u + (N))
#define OCTEON_HSH_IVW(N)           (0x0250u + (N))
#define OCTEON_SHA3_DAT24_SEL       0x0050u
#define OCTEON_SHA3_DAT15_MT_SEL    0x0051u
#define OCTEON_SHA3_DAT15_MF_SEL    OCTEON_HSH_DATW(15)
#define OCTEON_SNOW3G_LFSR(N)       OCTEON_HSH_DATW(N)
#define OCTEON_SNOW3G_RESULT_SEL    OCTEON_HSH_IVW(0)
#define OCTEON_SNOW3G_FSM(N)        OCTEON_HSH_IVW(1 + (N))

static inline void octeon_set_shared_mode(MIPSOcteonCryptoState *crypto,
                                          MIPSOcteonSharedMode mode)
{
    crypto->shared_mode = mode;
}
