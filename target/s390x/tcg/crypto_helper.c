/*
 *  s390x crypto helpers
 *
 *  Copyright (C) 2022 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *  Copyright (c) 2017 Red Hat Inc
 *
 *  Authors:
 *   David Hildenbrand <david@redhat.com>
 *   Jason A. Donenfeld <Jason@zx2c4.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/guest-random.h"
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "cpacf.h"

static void fill_buf_random(CPUS390XState *env, uintptr_t ra,
                            uint64_t *buf_reg, uint64_t *len_reg)
{
    uint8_t tmp[256];
    uint64_t len = *len_reg;
    int buf_reg_len = 64;

    if (!(env->psw.mask & PSW_MASK_64)) {
        len = (uint32_t)len;
        buf_reg_len = (env->psw.mask & PSW_MASK_32) ? 32 : 24;
    }

    while (len) {
        size_t block = MIN(len, sizeof(tmp));

        qemu_guest_getrandom_nofail(tmp, block);
        for (size_t i = 0; i < block; ++i) {
            cpu_stb_data_ra(env, wrap_address(env, *buf_reg), tmp[i], ra);
            *buf_reg = deposit64(*buf_reg, 0, buf_reg_len, *buf_reg + 1);
            --*len_reg;
        }
        len -= block;
    }
}

static int cpacf_kimd(CPUS390XState *env, const uintptr_t ra,
                      uint32_t r1, uint32_t r2, uint32_t r3, uint8_t fc)
{
    int rc = 0;

    switch (fc) {
    case 0x03: /* CPACF_KIMD_SHA_512 */
        rc = cpacf_sha512(env, ra, env->regs[1], &env->regs[r2],
                          &env->regs[r2 + 1], S390_FEAT_TYPE_KIMD);
        break;
    default:
        g_assert_not_reached();
    }

    return rc;
}

static int cpacf_klmd(CPUS390XState *env, const uintptr_t ra,
                      uint32_t r1, uint32_t r2, uint32_t r3, uint8_t fc)
{
    int rc = 0;

    switch (fc) {
    case 0x03: /* CPACF_KLMD_SHA_512 */
        rc = cpacf_sha512(env, ra, env->regs[1], &env->regs[r2],
                          &env->regs[r2 + 1], S390_FEAT_TYPE_KLMD);
        break;
    default:
        g_assert_not_reached();
    }

    return rc;
}

static int cpacf_ppno(CPUS390XState *env, uintptr_t ra,
                      uint32_t r1, uint32_t r2, uint32_t r3, uint8_t fc)
{
    int rc = 0;

    switch (fc) {
    case 0x72: /* CPACF_PRNO_TRNG */
        fill_buf_random(env, ra, &env->regs[r1], &env->regs[r1 + 1]);
        fill_buf_random(env, ra, &env->regs[r2], &env->regs[r2 + 1]);
        break;
    default:
        g_assert_not_reached();
    }

    return rc;
}

uint32_t HELPER(msa)(CPUS390XState *env, uint32_t r1, uint32_t r2, uint32_t r3,
                     uint32_t type)
{
    const uintptr_t ra = GETPC();
    const uint8_t mod = env->regs[0] & 0x80ULL;
    const uint8_t fc = env->regs[0] & 0x7fULL;
    uint8_t subfunc[16] = { 0 };
    uint64_t param_addr;
    int i, rc = 0;

    switch (type) {
    case S390_FEAT_TYPE_KDSA:
    case S390_FEAT_TYPE_KIMD:
    case S390_FEAT_TYPE_KLMD:
    case S390_FEAT_TYPE_KMAC:
    case S390_FEAT_TYPE_PCC:
    case S390_FEAT_TYPE_PCKMO:
        if (mod) {
            tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        }
        break;
    }

    s390_get_feat_block(type, subfunc);
    if (!test_be_bit(fc, subfunc)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* handle query subfunction */
    if (fc == 0) {
        for (i = 0; i < sizeof(subfunc); i++) {
            param_addr = wrap_address(env, env->regs[1] + i);
            cpu_stb_data_ra(env, param_addr, subfunc[i], ra);
        }
        goto out;
    }

    switch (type) {
    case S390_FEAT_TYPE_KIMD:
        rc = cpacf_kimd(env, ra, r1, r2, r3, fc);
        break;
    case S390_FEAT_TYPE_KLMD:
        rc = cpacf_klmd(env, ra, r1, r2, r3, fc);
        break;
    case S390_FEAT_TYPE_PPNO:
        rc = cpacf_ppno(env, ra, r1, r2, r3, fc);
        break;
    default:
        g_assert_not_reached();
    }

out:
    return rc;
}
