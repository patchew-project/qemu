/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * s390 cpacf aes
 *
 * Authors:
 *   Harald Freudenberger <freude@linux.ibm.com>
 */

#include "qemu/osdep.h"
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "accel/tcg/cpu-ldst-common.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "crypto/aes.h"
#include "crypto/aes-helpers.h"
#include "target/s390x/tcg/cpacf.h"

/*
 * helper function to copy some memory from guest to a local buffer
 */
static inline void copy_from_guest_wrap(CPUS390XState *env, const int mmu_idx,
                                        const uintptr_t ra, uint64_t guest_addr,
                                        uint8_t *dest, size_t len)
{
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);

    for (size_t i = 0; i < len; i++, guest_addr++) {
        uint64_t waddr = wrap_address(env, guest_addr);
        dest[i] = cpu_ldb_mmu(env, waddr, oi, ra);
    }
}

/*
 * helper function to copy from a local buffer to guest memory
 */
static inline void copy_to_guest_wrap(CPUS390XState *env, const int mmu_idx,
                                      const uintptr_t ra, uint64_t guest_addr,
                                      const uint8_t *src, size_t len)
{
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);

    for (size_t i = 0; i < len; i++, guest_addr++) {
        uint64_t waddr = wrap_address(env, guest_addr);
        cpu_stb_mmu(env, waddr, src[i], oi, ra);
    }
}

/*
 * read exactly one AES block from guest memory into a local buffer
 */
static inline void aes_read_block(CPUS390XState *env, const int mmu_idx,
                                  const uintptr_t ra, uint64_t guest_addr,
                                  uint8_t *buf)
{
    copy_from_guest_wrap(env, mmu_idx, ra, guest_addr, buf, AES_BLOCK_SIZE);
}

/*
 * write exactly one AES block from local buffer to guest memory
 */
static void aes_write_block(CPUS390XState *env, const int mmu_idx,
                            const uintptr_t ra, uint64_t guest_addr,
                            uint8_t *buf)
{
    copy_to_guest_wrap(env, mmu_idx, ra, guest_addr, buf, AES_BLOCK_SIZE);
}

int cpacf_aes_ecb(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    uint8_t key[32];
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KM);
    switch (fc) {
    case CPACF_KM_AES_128:
        keysize = 16;
        break;
    case CPACF_KM_AES_192:
        keysize = 24;
        break;
    case CPACF_KM_AES_256:
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    if (!(env->psw.mask & PSW_MASK_64)) {
        len = (uint32_t)len;
        addr_reg_size = (env->psw.mask & PSW_MASK_32) ? 32 : 24;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    copy_from_guest_wrap(env, mmu_idx, ra, param_addr, key, keysize);

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        aes_read_block(env, mmu_idx, ra, *src_ptr_reg + done, in);
        if (mod) {
            AES_decrypt(in, out, &exkey);
        } else {
            AES_encrypt(in, out, &exkey);
        }
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}
