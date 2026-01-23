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
    case 0x02: /* CPACF_KIMD_SHA_256 */
        rc = cpacf_sha256(env, ra, env->regs[1], &env->regs[r2],
                          &env->regs[r2 + 1], S390_FEAT_TYPE_KIMD);
        break;
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
    case 0x02: /* CPACF_KLMD_SHA_256 */
        rc = cpacf_sha256(env, ra, env->regs[1], &env->regs[r2],
                          &env->regs[r2 + 1], S390_FEAT_TYPE_KLMD);
        break;
    case 0x03: /* CPACF_KLMD_SHA_512 */
        rc = cpacf_sha512(env, ra, env->regs[1], &env->regs[r2],
                          &env->regs[r2 + 1], S390_FEAT_TYPE_KLMD);
        break;
    default:
        g_assert_not_reached();
    }

    return rc;
}


static int cpacf_km(CPUS390XState *env, uintptr_t ra, uint32_t r1,
                    uint32_t r2, uint32_t r3, uint8_t fc, uint8_t mod)
{
    int rc = 0;

    switch (fc) {
    case 0x12: /* CPACF_KM_AES_128 */
    case 0x13: /* CPACF_KM_AES_192 */
    case 0x14: /* CPACF_KM_AES_256 */
        rc = cpacf_aes_ecb(env, ra, env->regs[1],
                           &env->regs[r1], &env->regs[r2], &env->regs[r2 + 1],
                           S390_FEAT_TYPE_KM, fc, mod);
        break;
    case 0x1a: /* CPACF_KM_PAES_128 */
    case 0x1b: /* CPACF_KM_PAES_192 */
    case 0x1c: /* CPACF_KM_PAES_256 */
        rc = cpacf_paes_ecb(env, ra, env->regs[1],
                            &env->regs[r1], &env->regs[r2], &env->regs[r2 + 1],
                            S390_FEAT_TYPE_KM, fc, mod);
        break;
    case 0x32: /* CPACF_KM_XTS_128 */
    case 0x34: /* CPACF_KM_XTS_256 */
        rc = cpacf_aes_xts(env, ra, env->regs[1],
                           &env->regs[r1], &env->regs[r2], &env->regs[r2 + 1],
                           S390_FEAT_TYPE_KM, fc, mod);
        break;
    case 0x3a: /* CPACF_KM_PXTS_128 */
    case 0x3c: /* CPACF_KM_PXTS_256 */
        rc = cpacf_paes_xts(env, ra, env->regs[1],
                            &env->regs[r1], &env->regs[r2], &env->regs[r2 + 1],
                            S390_FEAT_TYPE_KM, fc, mod);
        break;
    default:
        g_assert_not_reached();
    }

    return rc;
}

static int cpacf_kmc(CPUS390XState *env, uintptr_t ra, uint32_t r1,
                     uint32_t r2, uint32_t r3, uint8_t fc, uint8_t mod)
{
    int rc = 0;

    switch (fc) {
    case 0x12: /* CPACF_KMC_AES_128 */
    case 0x13: /* CPACF_KMC_AES_192 */
    case 0x14: /* CPACF_KMC_AES_256 */
        rc = cpacf_aes_cbc(env, ra, env->regs[1],
                           &env->regs[r1], &env->regs[r2], &env->regs[r2 + 1],
                           S390_FEAT_TYPE_KMC, fc, mod);
        break;
    case 0x1a: /* CPACF_KMC_PAES_128 */
    case 0x1b: /* CPACF_KMC_PAES_192 */
    case 0x1c: /* CPACF_KMC_PAES_256 */
        rc = cpacf_paes_cbc(env, ra, env->regs[1],
                            &env->regs[r1], &env->regs[r2], &env->regs[r2 + 1],
                            S390_FEAT_TYPE_KMC, fc, mod);
        break;
    default:
        g_assert_not_reached();
    }

    return rc;
}

static int cpacf_kmctr(CPUS390XState *env, uintptr_t ra, uint32_t r1,
                       uint32_t r2, uint32_t r3, uint8_t fc, uint8_t mod)
{
    int rc = 0;

    switch (fc) {
    case 0x12: /* CPACF_KMCTR_AES_128 */
    case 0x13: /* CPACF_KMCTR_AES_192 */
    case 0x14: /* CPACF_KMCTR_AES_256 */
        rc = cpacf_aes_ctr(env, ra, env->regs[1],
                           &env->regs[r1], &env->regs[r2], &env->regs[r2 + 1],
                           &env->regs[r3], S390_FEAT_TYPE_KMCTR, fc, mod);
        break;
    case 0x1a: /* CPACF_KMCTR_PAES_128 */
    case 0x1b: /* CPACF_KMCTR_PAES_192 */
    case 0x1c: /* CPACF_KMCTR_PAES_256 */
        rc = cpacf_paes_ctr(env, ra, env->regs[1],
                            &env->regs[r1], &env->regs[r2], &env->regs[r2 + 1],
                            &env->regs[r3], S390_FEAT_TYPE_KMCTR, fc, mod);
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

static int cpacf_pcc(CPUS390XState *env, uintptr_t ra, uint8_t fc)
{
    int rc = 0;

    switch (fc) {
    case 0x32: /* CPACF_PCC compute XTS param AES-128 */
    case 0x34: /* CPACF PCC compute XTS param AES-256 */
            rc = cpacf_aes_pcc(env, ra, env->regs[1], fc);
            break;
    case 0x3a: /* CPACF_PCC compute XTS param Encrypted AES-128 */
    case 0x3c: /* CPACF PCC compute XTS param Encrypted AES-256 */
            rc = cpacf_paes_pcc(env, ra, env->regs[1], fc);
            break;
    default:
        g_assert_not_reached();
    }

    return rc;
}

static int cpacf_pckmo(CPUS390XState *env, uintptr_t ra, uint8_t fc)
{
    int rc = 0;

    switch (fc) {
    case 0x12: /* CPACF_PCKMO_ENC_AES_128_KEY */
    case 0x13: /* CPACF_PCKMO_ENC_AES_192_KEY */
    case 0x14: /* CPACF_PCKMO_ENC_AES_256_KEY */
        rc = cpacf_aes_pckmo(env, ra, env->regs[1], fc);
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
    case S390_FEAT_TYPE_KM:
        rc = cpacf_km(env, ra, r1, r2, r3, fc, mod);
        break;
    case S390_FEAT_TYPE_KMC:
        rc = cpacf_kmc(env, ra, r1, r2, r3, fc, mod);
        break;
    case S390_FEAT_TYPE_KMCTR:
        rc = cpacf_kmctr(env, ra, r1, r2, r3, fc, mod);
        break;
    case S390_FEAT_TYPE_PCC:
        rc = cpacf_pcc(env, ra, fc);
        break;
    case S390_FEAT_TYPE_PCKMO:
        rc = cpacf_pckmo(env, ra, fc);
        break;
    default:
        g_assert_not_reached();
    }

out:
    return rc;
}
