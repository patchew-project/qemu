/*
 *  s390 cpacf aes
 *
 *  Authors:
 *   Harald Freudenberger <freude@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "accel/tcg/cpu-ldst.h"
#include "crypto/aes.h"
#include "cpacf.h"

static void aes_read_block(CPUS390XState *env, uint64_t addr,
                           uint8_t *a, uintptr_t ra)
{
    uint64_t _addr;
    int i;

    for (i = 0; i < AES_BLOCK_SIZE; i++, addr += 1) {
        _addr = wrap_address(env, addr);
        a[i] = cpu_ldub_data_ra(env, _addr, ra);
    }
}

static void aes_write_block(CPUS390XState *env, uint64_t addr,
                            uint8_t *a, uintptr_t ra)
{
    uint64_t _addr;
    int i;

    for (i = 0; i < AES_BLOCK_SIZE; i++, addr += 1) {
        _addr = wrap_address(env, addr);
        cpu_stb_data_ra(env, _addr, a[i], ra);
    }
}

int cpacf_aes_ecb(CPUS390XState *env, uintptr_t ra, uint64_t param_addr,
                  uint64_t *dst_ptr, uint64_t *src_ptr, uint64_t *src_len,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t addr, len = *src_len, processed = 0;
    int i, keysize, data_reg_len = 64;
    uint8_t key[32];
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KM);
    switch (fc) {
    case 0x12: /* CPACF_KM_AES_128 */
        keysize = 16;
        break;
    case 0x13: /* CPACF_KM_AES_192 */
        keysize = 24;
        break;
    case 0x14: /* CPACF_KM_AES_256 */
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    if (!(env->psw.mask & PSW_MASK_64)) {
        len = (uint32_t)len;
        data_reg_len = (env->psw.mask & PSW_MASK_32) ? 32 : 24;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldub_data_ra(env, addr, ra);
    }

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        aes_read_block(env, *src_ptr + processed, in, ra);
        if (mod) {
            AES_decrypt(in, out, &exkey);
        } else {
            AES_encrypt(in, out, &exkey);
        }
        aes_write_block(env, *dst_ptr + processed, out, ra);
        len -= AES_BLOCK_SIZE, processed += AES_BLOCK_SIZE;
    }

    *src_ptr = deposit64(*src_ptr, 0, data_reg_len, *src_ptr + processed);
    *dst_ptr = deposit64(*dst_ptr, 0, data_reg_len, *dst_ptr + processed);
    *src_len -= processed;

    return !len ? 0 : 3;
}
