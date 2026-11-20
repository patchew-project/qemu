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
#include "accel/tcg/helper-retaddr.h"
#include "crypto/aes.h"
#include "crypto/aes-helpers.h"
#include "target/s390x/tcg/cpacf-arch.h"
#include "target/s390x/tcg/cpacf.h"
#include "target/s390x/tcg/crypto_helper.h"

/*
 * probe one AES block at guest address, which is wrapped according to current
 * addressing mode, since access_prepare() only wraps second page.
 */
static inline void aes_prepare_block(CPUS390XState *env, const int mmu_idx,
                                     const uintptr_t ra, S390Access *acc,
                                     uint64_t addr, MMUAccessType type)
{
    uint64_t waddr;
    waddr = wrap_address(env, addr);
    access_prepare(acc, env, waddr, AES_BLOCK_SIZE, type, mmu_idx, ra);
}

/*
 * read exactly one AES block from a prepared access into a local buffer.
 */
static inline void aes_read_block(CPUS390XState *env, const uintptr_t ra,
                                  S390Access *acc, uint8_t *buf)
{
    /* gather block */
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
        buf[i] = access_get_byte(env, acc, i, ra);
    }
}

/*
 * write exactly one AES block from a local buffer to a prepared access.
 */
static inline void aes_write_block(CPUS390XState *env, const uintptr_t ra,
                                   S390Access *acc, uint8_t *buf)
{
    /* scatter block */
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
        access_set_byte(env, acc, i, buf[i], ra);
    }
}

/*
 * Helper function returning the bitsize of the address mode.
 * Within this file only 64 bit and 32 bit are supported and
 * thus this function returns either 64 or 32 or 0.
 */
static int get_address_bitsize(CPUS390XState *env)
{
    if (env->psw.mask & PSW_MASK_64) {
        return 64;
    } else if (env->psw.mask & PSW_MASK_32) {
        return 32;
    } else {
        /* unknown or unsupported here */
        return 0;
    }
}

int cpacf_aes_ecb(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    int i, keysize, addr_reg_size;
    uint64_t len = *src_len_reg;
    S390Access srca, dsta;
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

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* probe source and destination access */
        aes_prepare_block(env, mmu_idx, ra, &srca, *src_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &dsta, *dst_ptr_reg,
                          MMU_DATA_STORE);

        /*
         * linux-user: the accessors below use host pointers. Record ra so a
         * fault from a racing unmap of the probed page is unwound correctly.
         */
        set_helper_retaddr(ra);

        aes_read_block(env, ra, &srca, in);
        if (mod) {
            AES_decrypt(in, out, &exkey);
        } else {
            AES_encrypt(in, out, &exkey);
        }
        aes_write_block(env, ra, &dsta, out);

        clear_helper_retaddr();

        /* update registers at end of each iteration */
        *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                                 *src_ptr_reg + AES_BLOCK_SIZE);
        *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                                 *dst_ptr_reg + AES_BLOCK_SIZE);
        *src_len_reg -= AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }

    return !len ? 0 : 3;
}

int cpacf_aes_cbc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint8_t key[32], iv[AES_BLOCK_SIZE];
    int i, keysize, addr_reg_size;
    uint64_t len = *src_len_reg;
    S390Access srca, dsta, iva;
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

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch iv from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, iv, AES_BLOCK_SIZE);

    /* fetch key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + AES_BLOCK_SIZE, key, keysize);

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* probe source, destination, and iv access */
        aes_prepare_block(env, mmu_idx, ra, &srca, *src_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &dsta, *dst_ptr_reg,
                          MMU_DATA_STORE);
        aes_prepare_block(env, mmu_idx, ra, &iva, param_addr,
                          MMU_DATA_STORE);

        /*
         * linux-user: the accessors below use host pointers. Record ra so a
         * fault from a racing unmap of the probed page is unwound correctly.
         */
        set_helper_retaddr(ra);

        aes_read_block(env, ra, &srca, in);
        if (mod) {
            /* decrypt in => out */
            AES_cbc_decrypt(in, out, iv, &exkey);
        } else {
            /* encrypt in => out */
            AES_cbc_encrypt(in, out, iv, &exkey);
        }
        /* scatter block to destination */
        aes_write_block(env, ra, &dsta, out);
        /* write back running iv */
        aes_write_block(env, ra, &iva, iv);

        clear_helper_retaddr();

        /* update registers at end of each iteration */
        *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                                 *src_ptr_reg + AES_BLOCK_SIZE);
        *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                                 *dst_ptr_reg + AES_BLOCK_SIZE);
        *src_len_reg -= AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }

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
    uint8_t ctr[AES_BLOCK_SIZE], key[32];
    int i, keysize, addr_reg_size;
    uint64_t len = *src_len_reg;
    S390Access srca, dsta, ctra;
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

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* probe source, counter, and destination access */
        aes_prepare_block(env, mmu_idx, ra, &srca, *src_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &ctra, *ctr_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &dsta, *dst_ptr_reg,
                          MMU_DATA_STORE);

        /*
         * linux-user: the accessors below use host pointers. Record ra so a
         * fault from a racing unmap of the probed page is unwound correctly.
         */
        set_helper_retaddr(ra);

        aes_read_block(env, ra, &srca, in);
        aes_read_block(env, ra, &ctra, ctr);
        /* encrypt ctr and xor with in => out */
        AES_ctr_encrypt(in, out, ctr, &exkey);
        aes_write_block(env, ra, &dsta, out);

        clear_helper_retaddr();

        /* update registers at end of each iteration */
        *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                                 *src_ptr_reg + AES_BLOCK_SIZE);
        *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                                 *dst_ptr_reg + AES_BLOCK_SIZE);
        *ctr_ptr_reg = deposit64(*ctr_ptr_reg, 0, addr_reg_size,
                                 *ctr_ptr_reg + AES_BLOCK_SIZE);
        *src_len_reg -= AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }

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
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize + AES_BLOCK_SIZE,
                       buf, AES_BLOCK_SIZE);

    /* is the block sequence nr 0 ? */
    for (i = 0; i < AES_BLOCK_SIZE && !buf[i]; i++) {
        ;
    }
    if (i < AES_BLOCK_SIZE) {
        /* no, sorry handling of non zero block sequence is not implemented */
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);

    /* fetch tweak from param block into tweak */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize, tweak, AES_BLOCK_SIZE);

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* encrypt tweak */
    AES_encrypt(tweak, buf, &exkey);

    /* store encrypted tweak into xts parameter field of the param block */
    write_guest_wrap_u8(env, mmu_idx, ra,
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
    uint8_t key[32], tweak[AES_BLOCK_SIZE];
    S390Access srca, dsta, tweaka;
    int i, keysize, addr_reg_size;
    uint64_t len = *src_len_reg;
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

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* fetch tweak from param block */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize, tweak, AES_BLOCK_SIZE);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* probe source, destination, and tweak access */
        aes_prepare_block(env, mmu_idx, ra, &srca, *src_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &dsta, *dst_ptr_reg,
                          MMU_DATA_STORE);
        aes_prepare_block(env, mmu_idx, ra, &tweaka, param_addr + keysize,
                          MMU_DATA_STORE);

        /*
         * linux-user: the accessors below use host pointers. Record ra so a
         * fault from a racing unmap of the probed page is unwound correctly.
         */
        set_helper_retaddr(ra);

        aes_read_block(env, ra, &srca, in);
        if (mod) {
            /* decrypt in => out */
            AES_xts_decrypt(in, out, tweak, &exkey);
        } else {
            /* encrypt in => out */
            AES_xts_encrypt(in, out, tweak, &exkey);
        }
        /* prep tweak for next round */
        AES_xts_prep_next_tweak(tweak);
        /* scatter block to destination */
        aes_write_block(env, ra, &dsta, out);
        /* write back running tweak */
        aes_write_block(env, ra, &tweaka, tweak);

        clear_helper_retaddr();

        /* update registers at end of each iteration */
        *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                                 *src_ptr_reg + AES_BLOCK_SIZE);
        *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                                 *dst_ptr_reg + AES_BLOCK_SIZE);
        *src_len_reg -= AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }

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
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);

    /* 'derive' the protected key from the clear key */
    encrypt_clrkey(key, keysize);

    /* store the protected key into param block */
    write_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);
    /* followed by the fake wkvp */
    write_guest_wrap_u8(env, mmu_idx, ra,
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
    int i, keysize, addr_reg_size;
    uint64_t len = *src_len_reg;
    uint8_t key[32], wkvp[32];
    S390Access srca, dsta;
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

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch and check wkvp from param block */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize, wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch protected key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);
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
        /* probe source and destination access */
        aes_prepare_block(env, mmu_idx, ra, &srca, *src_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &dsta, *dst_ptr_reg,
                          MMU_DATA_STORE);

        /*
         * linux-user: the accessors below use host pointers. Record ra so a
         * fault from a racing unmap of the probed page is unwound correctly.
         */
        set_helper_retaddr(ra);

        aes_read_block(env, ra, &srca, in);
        if (mod) {
            AES_decrypt(in, out, &exkey);
        } else {
            AES_encrypt(in, out, &exkey);
        }
        aes_write_block(env, ra, &dsta, out);

        clear_helper_retaddr();

        /* update registers at end of each iteration */
        *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                                 *src_ptr_reg + AES_BLOCK_SIZE);
        *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                                 *dst_ptr_reg + AES_BLOCK_SIZE);
        *src_len_reg -= AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }

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
    int i, keysize, addr_reg_size;
    uint64_t len = *src_len_reg;
    S390Access srca, dsta, iva;
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

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch and check wkvp from param block */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + AES_BLOCK_SIZE + keysize,
                       wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch iv from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, iv, AES_BLOCK_SIZE);

    /* fetch protected key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra,
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
        /* probe source, destination, and iv access */
        aes_prepare_block(env, mmu_idx, ra, &srca, *src_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &dsta, *dst_ptr_reg,
                          MMU_DATA_STORE);
        aes_prepare_block(env, mmu_idx, ra, &iva, param_addr,
                          MMU_DATA_STORE);

        /*
         * linux-user: the accessors below use host pointers. Record ra so a
         * fault from a racing unmap of the probed page is unwound correctly.
         */
        set_helper_retaddr(ra);

        aes_read_block(env, ra, &srca, in);
        if (mod) {
            /* decrypt in => out */
            AES_cbc_decrypt(in, out, iv, &exkey);
        } else {
            /* encrypt in => out */
            AES_cbc_encrypt(in, out, iv, &exkey);
        }
        /* scatter block to destination */
        aes_write_block(env, ra, &dsta, out);
        /* write back running iv */
        aes_write_block(env, ra, &iva, iv);

        clear_helper_retaddr();

        /* update registers at end of each iteration */
        *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                                 *src_ptr_reg + AES_BLOCK_SIZE);
        *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                                 *dst_ptr_reg + AES_BLOCK_SIZE);
        *src_len_reg -= AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }

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
    int i, keysize, addr_reg_size;
    uint64_t len = *src_len_reg;
    S390Access srca, dsta, ctra;
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

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch and check wkvp from param block */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize, wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch protected key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);
    /* decrypt the protected key */
    decrypt_protkey(key, keysize);

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* probe source, counter, and destination access */
        aes_prepare_block(env, mmu_idx, ra, &srca, *src_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &ctra, *ctr_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &dsta, *dst_ptr_reg,
                          MMU_DATA_STORE);

        /*
         * linux-user: the accessors below use host pointers. Record ra so a
         * fault from a racing unmap of the probed page is unwound correctly.
         */
        set_helper_retaddr(ra);

        aes_read_block(env, ra, &srca, in);
        aes_read_block(env, ra, &ctra, ctr);
        /* encrypt ctr and xor with in => out */
        AES_ctr_encrypt(in, out, ctr, &exkey);
        aes_write_block(env, ra, &dsta, out);

        clear_helper_retaddr();

        /* update registers at end of each iteration */
        *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                                 *src_ptr_reg + AES_BLOCK_SIZE);
        *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                                 *dst_ptr_reg + AES_BLOCK_SIZE);
        *ctr_ptr_reg = deposit64(*ctr_ptr_reg, 0, addr_reg_size,
                                 *ctr_ptr_reg + AES_BLOCK_SIZE);
        *src_len_reg -= AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }

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
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize, wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch block sequence nr from param block into buf */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize + sizeof(wkvp) + AES_BLOCK_SIZE,
                       buf, AES_BLOCK_SIZE);

    /* is the block sequence nr 0 ? */
    for (i = 0; i < AES_BLOCK_SIZE && !buf[i]; i++) {
        ;
    }
    if (i < AES_BLOCK_SIZE) {
        /* no, sorry handling of non zero block sequence is not implemented */
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch protected key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);
    /* decrypt the protected key */
    decrypt_protkey(key, keysize);

    /* fetch tweak from param block into tweak */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize + sizeof(wkvp),
                       tweak, AES_BLOCK_SIZE);

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* encrypt tweak */
    AES_encrypt(tweak, buf, &exkey);

    /* store encrypted tweak into xts parameter field of the param block */
    write_guest_wrap_u8(env, mmu_idx, ra,
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
    S390Access srca, dsta, tweaka;
    int i, keysize, addr_reg_size;
    uint64_t len = *src_len_reg;
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

    /* check addressing mode, raise exception if not supported here */
    addr_reg_size = get_address_bitsize(env);
    switch (addr_reg_size) {
    case 64:
        break;
    case 32:
        len = (uint32_t)len;
        break;
    default:
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* early bail out if length is zero */
    if (!len) {
        return 0;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch and check wkvp from param block */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize, wkvp, sizeof(wkvp));
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch protected key from param block */
    read_guest_wrap_u8(env, mmu_idx, ra, param_addr, key, keysize);
    /* decrypt the protected key */
    decrypt_protkey(key, keysize);

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* fetch tweak from param block */
    read_guest_wrap_u8(env, mmu_idx, ra,
                       param_addr + keysize + sizeof(wkvp),
                       tweak, AES_BLOCK_SIZE);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* probe source, destination, and tweak access */
        aes_prepare_block(env, mmu_idx, ra, &srca, *src_ptr_reg,
                          MMU_DATA_LOAD);
        aes_prepare_block(env, mmu_idx, ra, &dsta, *dst_ptr_reg,
                          MMU_DATA_STORE);
        aes_prepare_block(env, mmu_idx, ra, &tweaka,
                          param_addr + keysize + sizeof(wkvp),
                          MMU_DATA_STORE);

        /*
         * linux-user: the accessors below use host pointers. Record ra so a
         * fault from a racing unmap of the probed page is unwound correctly.
         */
        set_helper_retaddr(ra);

        aes_read_block(env, ra, &srca, in);
        if (mod) {
            /* decrypt in => out */
            AES_xts_decrypt(in, out, tweak, &exkey);
        } else {
            /* encrypt in => out */
            AES_xts_encrypt(in, out, tweak, &exkey);
        }
        /* prep tweak for next round */
        AES_xts_prep_next_tweak(tweak);
        /* scatter block to destination */
        aes_write_block(env, ra, &dsta, out);
        /* write back running tweak */
        aes_write_block(env, ra, &tweaka, tweak);

        clear_helper_retaddr();

        /* update registers at end of each iteration */
        *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                                 *src_ptr_reg + AES_BLOCK_SIZE);
        *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                                 *dst_ptr_reg + AES_BLOCK_SIZE);
        *src_len_reg -= AES_BLOCK_SIZE;
        len -= AES_BLOCK_SIZE;
    }

    return !len ? 0 : 3;
}
