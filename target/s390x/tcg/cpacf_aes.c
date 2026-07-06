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
#include "accel/tcg/cpu-ldst-common.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "crypto/aes.h"
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

int cpacf_aes_cbc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    uint8_t key[32], iv[AES_BLOCK_SIZE];
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMC);

    switch (fc) {
    case CPACF_KMC_AES_128:
        keysize = 16;
        break;
    case CPACF_KMC_AES_192:
        keysize = 24;
        break;
    case CPACF_KMC_AES_256:
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

    /* fetch iv from param block */
    copy_from_guest_wrap(env, mmu_idx, ra, param_addr, iv, AES_BLOCK_SIZE);

    /* fetch key from param block */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + AES_BLOCK_SIZE, key, keysize);

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
            /* decrypt in => out */
            AES_cbc_decrypt(in, out, iv, &exkey);
        } else {
            /* encrypt in => out */
            AES_cbc_encrypt(in, out, iv, &exkey);
        }
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    /* update iv in param block */
    copy_to_guest_wrap(env, mmu_idx, ra, param_addr, iv, AES_BLOCK_SIZE);

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}

int cpacf_aes_ctr(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint64_t *ctr_ptr_reg, uint32_t type,
                  uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    uint8_t ctr[AES_BLOCK_SIZE], key[32];
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMCTR);

    switch (fc) {
    case CPACF_KMCTR_AES_128:
        keysize = 16;
        break;
    case CPACF_KMCTR_AES_192:
        keysize = 24;
        break;
    case CPACF_KMCTR_AES_256:
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
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* read in nonce/ctr => ctr */
        aes_read_block(env, mmu_idx, ra, *ctr_ptr_reg + done, ctr);
        /* read in one block of input data => in */
        aes_read_block(env, mmu_idx, ra, *src_ptr_reg + done, in);
        /* encrypt ctr and xor with in => out */
        AES_ctr_encrypt(in, out, ctr, &exkey);
        /* write out the processed block */
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *ctr_ptr_reg = deposit64(*ctr_ptr_reg, 0, addr_reg_size,
                             *ctr_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}

int cpacf_aes_pcc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint8_t fc)
{
    uint8_t key[32], tweak[AES_BLOCK_SIZE], buf[AES_BLOCK_SIZE];
    int keysize, i;
    AES_KEY exkey;

    switch (fc) {
    case CPACF_PCC_XTS_AES_128:
        keysize = 16;
        break;
    case CPACF_PCC_XTS_AES_256:
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* fetch block sequence nr from param block into buf */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize + AES_BLOCK_SIZE,
                         buf, AES_BLOCK_SIZE);

    /* is the block sequence nr 0 ? */
    for (i = 0; i < AES_BLOCK_SIZE && !buf[i]; i++) {
            ;
    }
    if (i < AES_BLOCK_SIZE) {
        /* no, sorry handling of non zero block sequence is not implemented */
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return 1;
    }

    /* fetch key from param block */
    copy_from_guest_wrap(env, mmu_idx, ra, param_addr, key, keysize);

    /* fetch tweak from param block into tweak */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize, tweak, AES_BLOCK_SIZE);

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* encrypt tweak */
    AES_encrypt(tweak, buf, &exkey);

    /* store encrypted tweak into xts parameter field of the param block */
    copy_to_guest_wrap(env, mmu_idx, ra,
                       param_addr + keysize + 3 * AES_BLOCK_SIZE,
                       buf, AES_BLOCK_SIZE);

    return 0;
}

int cpacf_aes_xts(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    uint8_t key[32], tweak[AES_BLOCK_SIZE];
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KM);

    switch (fc) {
    case CPACF_KM_XTS_128:
        keysize = 16;
        break;
    case CPACF_KM_XTS_256:
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

    /* fetch tweak from param block */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize, tweak, AES_BLOCK_SIZE);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* fetch one AES block into in  */
        aes_read_block(env, mmu_idx, ra, *src_ptr_reg + done, in);
        if (mod) {
            /* decrypt in => out */
            AES_xts_decrypt(in, out, tweak, &exkey);
        } else {
            /* encrypt in => out */
            AES_xts_encrypt(in, out, tweak, &exkey);
        }
        /* prep tweak for next round */
        AES_xts_prep_next_tweak(tweak);
        /* write out this processed block from out */
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    /* update tweak in param block */
    copy_to_guest_wrap(env, mmu_idx, ra,
                       param_addr + keysize, tweak, AES_BLOCK_SIZE);

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}

/*
 * Support for protected key cpacf functions. Note that this is
 * a fake implementation intended for debugging and development.
 * Do not use for production load !
 */

/*
 * Hard coded pattern xored with the AES clear key
 * to 'produce' the protected key.
 */
static const uint8_t protkey_xor_pattern[32] = PROTKEY_XOR_PATTERN;

/*
 * Hard coded wkvp ("Wrapping Key Verification Pattern")
 */
static const uint8_t protkey_wkvp[32] = PROTKEY_WKVP;

/*
 * 'encrypt' the clear key value into a protected key
 * by xor-ing the protkey_xor_pattern onto it.
 */
static void encrypt_clrkey(uint8_t *key, int keysize)
{
    for (int i = 0; i < keysize; i++) {
        key[i] ^= protkey_xor_pattern[i];
    }
}

/*
 * 'decrypt' the protected key by reverting the xor
 * of the protkey_xor_pattern onto the clear key value.
 */
static void decrypt_protkey(uint8_t *key, int keysize)
{
    for (int i = 0; i < keysize; i++) {
        key[i] ^= protkey_xor_pattern[i];
    }
}

int cpacf_aes_pckmo(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                    uint64_t param_addr, uint8_t fc)
{
    uint8_t key[32];
    int keysize;

    switch (fc) {
    case CPACF_PCKMO_ENC_AES_128_KEY:
        keysize = 16;
        break;
    case CPACF_PCKMO_ENC_AES_192_KEY:
        keysize = 24;
        break;
    case CPACF_PCKMO_ENC_AES_256_KEY:
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* fetch key from param block */
    copy_from_guest_wrap(env, mmu_idx, ra, param_addr, key, keysize);

    /* 'derive' the protected key from the clear key */
    encrypt_clrkey(key, keysize);

    /* store the protected key into param block */
    copy_to_guest_wrap(env, mmu_idx, ra, param_addr, key, keysize);
    /* followed by the fake wkvp */
    copy_to_guest_wrap(env, mmu_idx, ra,
                       param_addr + keysize,
                       protkey_wkvp, sizeof(protkey_wkvp));

    return 0;
}

int cpacf_paes_ecb(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint64_t *dst_ptr_reg,
                   uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                   uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    uint8_t key[32], wkvp[32];
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KM);

    switch (fc) {
    case CPACF_KM_PAES_128:
        keysize = 16;
        break;
    case CPACF_KM_PAES_192:
        keysize = 24;
        break;
    case CPACF_KM_PAES_256:
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

    /* fetch and check wkvp from param block */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize, wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch protected key from param block */
    copy_from_guest_wrap(env, mmu_idx, ra, param_addr, key, keysize);
    /* decrypt the protected key */
    decrypt_protkey(key, keysize);

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

int cpacf_paes_cbc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint64_t *dst_ptr_reg,
                   uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                   uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint8_t key[32], wkvp[32], iv[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMC);

    switch (fc) {
    case CPACF_KMC_PAES_128:
        keysize = 16;
        break;
    case CPACF_KMC_PAES_192:
        keysize = 24;
        break;
    case CPACF_KMC_PAES_256:
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

    /* fetch and check wkvp from param block */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + AES_BLOCK_SIZE + keysize,
                         wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch iv from param block */
    copy_from_guest_wrap(env, mmu_idx, ra, param_addr, iv, AES_BLOCK_SIZE);

    /* fetch protected key from param block */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + AES_BLOCK_SIZE, key, keysize);
    /* decrypt the protected key */
    decrypt_protkey(key, keysize);

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
            /* decrypt in => out */
            AES_cbc_decrypt(in, out, iv, &exkey);
        } else {
            /* encrypt in => out */
            AES_cbc_encrypt(in, out, iv, &exkey);
        }
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    /* update iv in param block */
    copy_to_guest_wrap(env, mmu_idx, ra, param_addr, iv, AES_BLOCK_SIZE);

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}

int cpacf_paes_ctr(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint64_t *dst_ptr_reg,
                   uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                   uint64_t *ctr_ptr_reg, uint32_t type,
                   uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint8_t ctr[AES_BLOCK_SIZE], key[32], wkvp[32];
    uint64_t len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMCTR);

    switch (fc) {
    case CPACF_KMCTR_PAES_128:
        keysize = 16;
        break;
    case CPACF_KMCTR_PAES_192:
        keysize = 24;
        break;
    case CPACF_KMCTR_PAES_256:
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

    /* fetch and check wkvp from param block */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize, wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch protected key from param block */
    copy_from_guest_wrap(env, mmu_idx, ra, param_addr, key, keysize);
    /* decrypt the protected key */
    decrypt_protkey(key, keysize);

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* read in nonce/ctr => ctr */
        aes_read_block(env, mmu_idx, ra, *ctr_ptr_reg + done, ctr);
        /* read in one block of input data => in */
        aes_read_block(env, mmu_idx, ra, *src_ptr_reg + done, in);
        /* encrypt ctr and xor with in => out */
        AES_ctr_encrypt(in, out, ctr, &exkey);
        /* write out the processed block */
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *ctr_ptr_reg = deposit64(*ctr_ptr_reg, 0, addr_reg_size,
                             *ctr_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}

int cpacf_paes_pcc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint8_t fc)
{
    uint8_t key[32], wkvp[32], tweak[AES_BLOCK_SIZE], buf[AES_BLOCK_SIZE];
    int keysize, i;
    AES_KEY exkey;

    switch (fc) {
    case CPACF_PCC_XTS_PAES_128:
        keysize = 16;
        break;
    case CPACF_PCC_XTS_PAES_256:
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* fetch and check wkvp from param block */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize, wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch block sequence nr from param block into buf */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize + sizeof(wkvp) + AES_BLOCK_SIZE,
                         buf, AES_BLOCK_SIZE);

    /* is the block sequence nr 0 ? */
    for (i = 0; i < AES_BLOCK_SIZE && !buf[i]; i++) {
            ;
    }
    if (i < AES_BLOCK_SIZE) {
        /* no, sorry handling of non zero block sequence is not implemented */
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
        return 1;
    }

    /* fetch protected key from param block */
    copy_from_guest_wrap(env, mmu_idx, ra, param_addr, key, keysize);
    /* decrypt the protected key */
    decrypt_protkey(key, keysize);

    /* fetch tweak from param block into tweak */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize + sizeof(wkvp),
                         tweak, AES_BLOCK_SIZE);

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* encrypt tweak */
    AES_encrypt(tweak, buf, &exkey);

    /* store encrypted tweak into xts parameter field of the param block */
    copy_to_guest_wrap(env, mmu_idx, ra,
                       param_addr + keysize + sizeof(wkvp) + 3 * AES_BLOCK_SIZE,
                       buf, AES_BLOCK_SIZE);

    return 0;
}

int cpacf_paes_xts(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint64_t *dst_ptr_reg,
                   uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                   uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint8_t key[32], wkvp[32], tweak[AES_BLOCK_SIZE];
    uint64_t len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KM);

    switch (fc) {
    case CPACF_KM_PXTS_128:
        keysize = 16;
        break;
    case CPACF_KM_PXTS_256:
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

    /* fetch and check wkvp from param block */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize, wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch protected key from param block */
    copy_from_guest_wrap(env, mmu_idx, ra, param_addr, key, keysize);
    /* decrypt the protected key */
    decrypt_protkey(key, keysize);

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* fetch tweak from param block */
    copy_from_guest_wrap(env, mmu_idx, ra,
                         param_addr + keysize + sizeof(wkvp),
                         tweak, AES_BLOCK_SIZE);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* fetch one AES block into in */
        aes_read_block(env, mmu_idx, ra, *src_ptr_reg + done, in);
        if (mod) {
            /* decrypt in => out */
            AES_xts_decrypt(in, out, tweak, &exkey);
        } else {
            /* encrypt in => out */
            AES_xts_encrypt(in, out, tweak, &exkey);
        }
        /* prep tweak for next round */
        AES_xts_prep_next_tweak(tweak);
        /* write out this processed block from out */
        aes_write_block(env, mmu_idx, ra, *dst_ptr_reg + done, out);
        len -= AES_BLOCK_SIZE;
        done += AES_BLOCK_SIZE;
    }

    /* update tweak in param block */
    copy_to_guest_wrap(env, mmu_idx, ra,
                       param_addr + keysize + sizeof(wkvp),
                       tweak, AES_BLOCK_SIZE);

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}
