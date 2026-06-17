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
#include "cpacf.h"

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

static void aes_read_block(CPUS390XState *env, const int mmu_idx,
                           uint64_t addr, uint8_t *a, uintptr_t ra)
{
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint64_t _addr;

    for (int i = 0; i < AES_BLOCK_SIZE; i++, addr += 1) {
        _addr = wrap_address(env, addr);
        a[i] = cpu_ldb_mmu(env, _addr, oi, ra);
    }
}

static void aes_write_block(CPUS390XState *env, const int mmu_idx,
                            uint64_t addr, uint8_t *a, uintptr_t ra)
{
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint64_t _addr;

    for (int i = 0; i < AES_BLOCK_SIZE; i++, addr += 1) {
        _addr = wrap_address(env, addr);
        cpu_stb_mmu(env, _addr, a[i], oi, ra);
    }
}

int cpacf_aes_ecb(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t addr, len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
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
        addr_reg_size = (env->psw.mask & PSW_MASK_32) ? 32 : 24;
    }

    /* length has to be properly aligned. */
    if (!QEMU_IS_ALIGNED(len, AES_BLOCK_SIZE)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* fetch key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        aes_read_block(env, mmu_idx, *src_ptr_reg + done, in, ra);
        if (mod) {
            AES_decrypt(in, out, &exkey);
        } else {
            AES_encrypt(in, out, &exkey);
        }
        aes_write_block(env, mmu_idx, *dst_ptr_reg + done, out, ra);
        len -= AES_BLOCK_SIZE, done += AES_BLOCK_SIZE;
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
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint64_t addr, len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    uint8_t key[32], iv[AES_BLOCK_SIZE];
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMC);

    switch (fc) {
    case 0x12: /* CPACF_KMC_AES_128 */
        keysize = 16;
        break;
    case 0x13: /* CPACF_KMC_AES_192 */
        keysize = 24;
        break;
    case 0x14: /* CPACF_KMC_AES_256 */
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
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + i);
        iv[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* fetch key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + AES_BLOCK_SIZE + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        aes_read_block(env, mmu_idx, *src_ptr_reg + done, in, ra);
        if (mod) {
            /* decrypt in => out */
            AES_cbc_decrypt(in, out, iv, &exkey);
        } else {
            /* encrypt in => out */
            AES_cbc_encrypt(in, out, iv, &exkey);
        }
        aes_write_block(env, mmu_idx, *dst_ptr_reg + done, out, ra);
        len -= AES_BLOCK_SIZE, done += AES_BLOCK_SIZE;
    }

    /* update iv in param block */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + i);
        cpu_stb_mmu(env, addr, iv[i], oi, ra);
    }

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
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t addr, len = *src_len_reg, done = 0;
    uint8_t ctr[AES_BLOCK_SIZE], key[32];
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMCTR);

    switch (fc) {
    case 0x12: /* CPACF_KMCTR_AES_128 */
        keysize = 16;
        break;
    case 0x13: /* CPACF_KMCTR_AES_192 */
        keysize = 24;
        break;
    case 0x14: /* CPACF_KMCTR_AES_256 */
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
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* read in nonce/ctr => ctr */
        aes_read_block(env, mmu_idx, *ctr_ptr_reg + done, ctr, ra);
        /* read in one block of input data => in */
        aes_read_block(env, mmu_idx, *src_ptr_reg + done, in, ra);
        /* encrypt ctr and xor with in => out */
        AES_ctr_encrypt(in, out, ctr, &exkey);
        /* write out the processed block */
        aes_write_block(env, mmu_idx, *dst_ptr_reg + done, out, ra);
        len -= AES_BLOCK_SIZE, done += AES_BLOCK_SIZE;
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
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    int keysize, i;
    uint64_t addr;
    AES_KEY exkey;

    switch (fc) {
    case 0x32: /* CPACF_PCC compute XTS param AES-128 */
        keysize = 16;
        break;
    case 0x34: /* CPACF PCC compute XTS param AES-256 */
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* fetch block sequence nr from param block into buf */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize + AES_BLOCK_SIZE + i);
        buf[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* is the block sequence nr 0 ? */
    for (i = 0; i < AES_BLOCK_SIZE && !buf[i]; i++) {
            ;
    }
    if (i < AES_BLOCK_SIZE) {
        /* no, sorry handling of non zero block sequence is not implemented */
        cpu_abort(env_cpu(env),
                  "PCC-compute-XTS-param with non zero block sequence is not implemented\n");
        return 1;
    }

    /* fetch key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* fetch tweak from param block into tweak */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize + i);
        tweak[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* encrypt tweak */
    AES_encrypt(tweak, buf, &exkey);

    /* store encrypted tweak into xts parameter field of the param block */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize + 3 * AES_BLOCK_SIZE + i);
        cpu_stb_mmu(env, addr, buf[i], oi, ra);
    }

    return 0;
}

int cpacf_aes_xts(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint64_t addr, len = *src_len_reg, done = 0;
    uint8_t key[32], tweak[AES_BLOCK_SIZE];
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KM);

    switch (fc) {
    case 0x32: /* CPACF_KM_XTS_128 */
        keysize = 16;
        break;
    case 0x34: /* CPACF_KM_XTS_256 */
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
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* fetch tweak from param block */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize + i);
        tweak[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* fetch one AES block into in  */
        aes_read_block(env, mmu_idx, *src_ptr_reg + done, in, ra);
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
        aes_write_block(env, mmu_idx, *dst_ptr_reg + done, out, ra);
        len -= AES_BLOCK_SIZE, done += AES_BLOCK_SIZE;
    }

    /* update tweak in param block */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize + i);
        cpu_stb_mmu(env, addr, tweak[i], oi, ra);
    }

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}

/*
 * Hard coded pattern xored with the AES clear key
 * to 'produce' the protected key.
 */
static const uint8_t protkey_xor_pattern[32] = {
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };

/*
 * Hard coded wkvp ("Wrapping Key Verification Pattern")
 */
static const uint8_t protkey_wkvp[32] = {
    0x0F, 0x0A, 0x0C, 0x0E, 0x0F, 0x0A, 0x0C, 0x0E,
    0x0F, 0x0A, 0x0C, 0x0E, 0x0F, 0x0A, 0x0C, 0x0E,
    0x0F, 0x0A, 0x0C, 0x0E, 0x0F, 0x0A, 0x0C, 0x0E,
    0x0F, 0x0A, 0x0C, 0x0E, 0x0F, 0x0A, 0x0C, 0x0E };

int cpacf_aes_pckmo(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                    uint64_t param_addr, uint8_t fc)
{
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint8_t key[32];
    int keysize, i;
    uint64_t addr;

    switch (fc) {
    case 0x12: /* CPACF_PCKMO_ENC_AES_128_KEY */
        keysize = 16;
        break;
    case 0x13: /* CPACF_PCKMO_ENC_AES_192_KEY */
        keysize = 24;
        break;
    case 0x14: /* CPACF_PCKMO_ENC_AES_256_KEY */
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* fetch key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* 'derive' the protected key */
    for (i = 0; i < keysize; i++) {
        key[i] ^= protkey_xor_pattern[i];
    }

    /* store the protected key into param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        cpu_stb_mmu(env, addr, key[i], oi, ra);
    }
    /* followed by the fake wkvp */
    for (i = 0; i < sizeof(protkey_wkvp); i++) {
        addr = wrap_address(env, param_addr + keysize + i);
        cpu_stb_mmu(env, addr, protkey_wkvp[i], oi, ra);
    }

    return 0;
}

int cpacf_paes_ecb(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint64_t *dst_ptr_reg,
                   uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                   uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint64_t addr, len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    uint8_t key[32], wkvp[32];
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KM);

    switch (fc) {
    case 0x1a: /* CPACF_KM_PAES_128 */
        keysize = 16;
        break;
    case 0x1b: /* CPACF_KM_PAES_192 */
        keysize = 24;
        break;
    case 0x1c: /* CPACF_KM_PAES_256 */
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
    for (i = 0; i < sizeof(wkvp); i++) {
        addr = wrap_address(env, param_addr + keysize + i);
        wkvp[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch protected key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    /* 'decrypt' the protected key */
    for (i = 0; i < keysize; i++) {
        key[i] ^= protkey_xor_pattern[i];
    }

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        aes_read_block(env, mmu_idx, *src_ptr_reg + done, in, ra);
        if (mod) {
            AES_decrypt(in, out, &exkey);
        } else {
            AES_encrypt(in, out, &exkey);
        }
        aes_write_block(env, mmu_idx, *dst_ptr_reg + done, out, ra);
        len -= AES_BLOCK_SIZE, done += AES_BLOCK_SIZE;
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
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint8_t key[32], wkvp[32], iv[AES_BLOCK_SIZE];
    uint64_t addr, len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMC);

    switch (fc) {
    case 0x1a: /* CPACF_KMC_PAES_128 */
        keysize = 16;
        break;
    case 0x1b: /* CPACF_KMC_PAES_192 */
        keysize = 24;
        break;
    case 0x1c: /* CPACF_KMC_PAES_256 */
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
    for (i = 0; i < sizeof(wkvp); i++) {
        addr = wrap_address(env, param_addr + AES_BLOCK_SIZE + keysize + i);
        wkvp[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch iv from param block */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + i);
        iv[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* fetch protected key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + AES_BLOCK_SIZE + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    /* 'decrypt' the protected key */
    for (i = 0; i < keysize; i++) {
        key[i] ^= protkey_xor_pattern[i];
    }

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        aes_read_block(env, mmu_idx, *src_ptr_reg + done, in, ra);
        if (mod) {
            /* decrypt in => out */
            AES_cbc_decrypt(in, out, iv, &exkey);
        } else {
            /* encrypt in => out */
            AES_cbc_encrypt(in, out, iv, &exkey);
        }
        aes_write_block(env, mmu_idx, *dst_ptr_reg + done, out, ra);
        len -= AES_BLOCK_SIZE, done += AES_BLOCK_SIZE;
    }

    /* update iv in param block */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + i);
        cpu_stb_mmu(env, addr, iv[i], oi, ra);
    }

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
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    uint8_t ctr[AES_BLOCK_SIZE], key[32], wkvp[32];
    uint64_t addr, len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KMCTR);

    switch (fc) {
    case 0x1a: /* CPACF_KMCTR_PAES_128 */
        keysize = 16;
        break;
    case 0x1b: /* CPACF_KMCTR_PAES_192 */
        keysize = 24;
        break;
    case 0x1c: /* CPACF_KMCTR_PAES_256 */
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
    for (i = 0; i < sizeof(wkvp); i++) {
        addr = wrap_address(env, param_addr + keysize + i);
        wkvp[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch protected key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    /* 'decrypt' the protected key */
    for (i = 0; i < keysize; i++) {
        key[i] ^= protkey_xor_pattern[i];
    }

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* read in nonce/ctr => ctr */
        aes_read_block(env, mmu_idx, *ctr_ptr_reg + done, ctr, ra);
        /* read in one block of input data => in */
        aes_read_block(env, mmu_idx, *src_ptr_reg + done, in, ra);
        /* encrypt ctr and xor with in => out */
        AES_ctr_encrypt(in, out, ctr, &exkey);
        /* write out the processed block */
        aes_write_block(env, mmu_idx, *dst_ptr_reg + done, out, ra);
        len -= AES_BLOCK_SIZE, done += AES_BLOCK_SIZE;
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
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    int keysize, i;
    uint64_t addr;
    AES_KEY exkey;

    switch (fc) {
    case 0x3a: /* CPACF_PCC compute XTS param Encrypted AES-128 */
        keysize = 16;
        break;
    case 0x3c: /* CPACF PCC compute XTS param Encrypted AES-256 */
        keysize = 32;
        break;
    default:
        g_assert_not_reached();
    }

    /* fetch and check wkvp from param block */
    for (i = 0; i < sizeof(wkvp); i++) {
        addr = wrap_address(env, param_addr + keysize + i);
        wkvp[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch block sequence nr from param block into buf */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize +
                            sizeof(wkvp) + AES_BLOCK_SIZE + i);
        buf[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* is the block sequence nr 0 ? */
    for (i = 0; i < AES_BLOCK_SIZE && !buf[i]; i++) {
            ;
    }
    if (i < AES_BLOCK_SIZE) {
        /* no, sorry handling of non zero block sequence is not implemented */
        cpu_abort(env_cpu(env),
                  "PCC-compute-XTS-param (encrypted) with non zero block seq nr is not implemented\n");
        return 1;
    }

    /* fetch protected key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    /* 'decrypt' the protected key */
    for (i = 0; i < keysize; i++) {
        key[i] ^= protkey_xor_pattern[i];
    }

    /* fetch tweak from param block into tweak */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize + sizeof(wkvp) + i);
        tweak[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* expand key */
    AES_set_encrypt_key(key, keysize * 8, &exkey);

    /* encrypt tweak */
    AES_encrypt(tweak, buf, &exkey);

    /* store encrypted tweak into xts parameter field of the param block */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize +
                            sizeof(wkvp) + 3 * AES_BLOCK_SIZE + i);
        cpu_stb_mmu(env, addr, buf[i], oi, ra);
    }

    return 0;
}

int cpacf_paes_xts(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint64_t *dst_ptr_reg,
                   uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                   uint32_t type, uint8_t fc, uint8_t mod)
{
    enum { MAX_BLOCKS_PER_RUN = 8192 / AES_BLOCK_SIZE };
    uint8_t in[AES_BLOCK_SIZE], out[AES_BLOCK_SIZE];
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);
    uint8_t key[32], wkvp[32], tweak[AES_BLOCK_SIZE];
    uint64_t addr, len = *src_len_reg, done = 0;
    int i, keysize, addr_reg_size = 64;
    AES_KEY exkey;

    g_assert(type == S390_FEAT_TYPE_KM);

    switch (fc) {
    case 0x3a: /* CPACF_KM_PXTS_128 */
        keysize = 16;
        break;
    case 0x3c: /* CPACF_KM_PXTS_256 */
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
    for (i = 0; i < sizeof(wkvp); i++) {
        addr = wrap_address(env, param_addr + keysize + i);
        wkvp[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    if (memcmp(wkvp, protkey_wkvp, sizeof(wkvp))) {
        /* wkvp mismatch -> return with cc 1 */
        return 1;
    }

    /* fetch protected key from param block */
    for (i = 0; i < keysize; i++) {
        addr = wrap_address(env, param_addr + i);
        key[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }
    /* 'decrypt' the protected key */
    for (i = 0; i < keysize; i++) {
        key[i] ^= protkey_xor_pattern[i];
    }

    /* expand key */
    if (mod) {
        AES_set_decrypt_key(key, keysize * 8, &exkey);
    } else {
        AES_set_encrypt_key(key, keysize * 8, &exkey);
    }

    /* fetch tweak from param block */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize + sizeof(wkvp) + i);
        tweak[i] = cpu_ldb_mmu(env, addr, oi, ra);
    }

    /* process up to MAX_BLOCKS_PER_RUN aes blocks */
    for (i = 0; i < MAX_BLOCKS_PER_RUN && len >= AES_BLOCK_SIZE; i++) {
        /* fetch one AES block into in */
        aes_read_block(env, mmu_idx, *src_ptr_reg + done, in, ra);
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
        aes_write_block(env, mmu_idx, *dst_ptr_reg + done, out, ra);
        len -= AES_BLOCK_SIZE, done += AES_BLOCK_SIZE;
    }

    /* update tweak in param block */
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        addr = wrap_address(env, param_addr + keysize + sizeof(wkvp) + i);
        cpu_stb_mmu(env, addr, tweak[i], oi, ra);
    }

    *src_ptr_reg = deposit64(*src_ptr_reg, 0, addr_reg_size,
                             *src_ptr_reg + done);
    *dst_ptr_reg = deposit64(*dst_ptr_reg, 0, addr_reg_size,
                             *dst_ptr_reg + done);
    *src_len_reg -= done;

    return !len ? 0 : 3;
}
