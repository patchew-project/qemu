/*
 *  s390x crypto helpers
 *
 *  Copyright (c) 2017 Red Hat Inc
 *
 *  Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/guest-random.h"
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

static void fill_buf_random(CPUS390XState *env, uintptr_t ra,
                            uint64_t buf, uint64_t len)
{
        uint8_t tmp[256];

        if (!(env->psw.mask & PSW_MASK_64))
                len = (uint32_t)len;

        while (len) {
                size_t block = MIN(len, sizeof(tmp));
                qemu_guest_getrandom_nofail(tmp, block);
                for (size_t i = 0; i < block; ++i)
                        cpu_stb_data_ra(env, wrap_address(env, buf++), tmp[i], ra);
                len -= block;
        }
}

uint32_t HELPER(msa)(CPUS390XState *env, uint32_t r1, uint32_t r2, uint32_t r3,
                     uint32_t type)
{
    const uintptr_t ra = GETPC();
    const uint8_t mod = env->regs[0] & 0x80ULL;
    const uint8_t fc = env->regs[0] & 0x7fULL;
    uint8_t subfunc[16] = { 0 };
    uint64_t param_addr;
    int i;

    switch (type) {
    case S390_FEAT_TYPE_KMAC:
    case S390_FEAT_TYPE_KIMD:
    case S390_FEAT_TYPE_KLMD:
    case S390_FEAT_TYPE_PCKMO:
    case S390_FEAT_TYPE_PCC:
        if (mod) {
            tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        }
        break;
    }

    s390_get_feat_block(type, subfunc);
    if (!test_be_bit(fc, subfunc)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    switch (fc) {
    case 0: /* query subfunction */
        for (i = 0; i < 16; i++) {
            param_addr = wrap_address(env, env->regs[1] + i);
            cpu_stb_data_ra(env, param_addr, subfunc[i], ra);
        }
        break;
    case 114:
        if (r1 & 1 || !r1 || r2 & 1 || !r2) {
                tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
                break;
        }

        fill_buf_random(env, ra, env->regs[r1], env->regs[r1 + 1]);
        fill_buf_random(env, ra, env->regs[r2], env->regs[r2 + 1]);

        env->regs[r1] += env->regs[r1 + 1];
        env->regs[r1 + 1] = 0;
        env->regs[r2] += env->regs[r2 + 1];
        env->regs[r2 + 1] = 0;
        break;
    default:
        /* we don't implement any other subfunction yet */
        g_assert_not_reached();
    }

    return 0;
}
