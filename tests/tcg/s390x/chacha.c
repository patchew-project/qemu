/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Derived from linux kernel sources:
 *   ./include/crypto/chacha.h
 *   ./crypto/chacha_generic.c
 *   ./arch/s390/crypto/chacha-glue.c
 *   ./tools/testing/crypto/chacha20-s390/test-cipher.c
 *   ./tools/testing/crypto/chacha20-s390/run-tests.sh
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/random.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

static unsigned data_size;
static bool debug;

#define CHACHA_IV_SIZE          16
#define CHACHA_KEY_SIZE         32
#define CHACHA_BLOCK_SIZE       64
#define CHACHAPOLY_IV_SIZE      12
#define CHACHA_STATE_WORDS      (CHACHA_BLOCK_SIZE / sizeof(u32))

static u32 rol32(u32 val, u32 sh)
{
    return (val << (sh & 31)) | (val >> (-sh & 31));
}

static u32 get_unaligned_le32(const void *ptr)
{
    u32 val;
    memcpy(&val, ptr, 4);
    return __builtin_bswap32(val);
}

static void put_unaligned_le32(u32 val, void *ptr)
{
    val = __builtin_bswap32(val);
    memcpy(ptr, &val, 4);
}

static void chacha_permute(u32 *x, int nrounds)
{
    for (int i = 0; i < nrounds; i += 2) {
        x[0]  += x[4];    x[12] = rol32(x[12] ^ x[0],  16);
        x[1]  += x[5];    x[13] = rol32(x[13] ^ x[1],  16);
        x[2]  += x[6];    x[14] = rol32(x[14] ^ x[2],  16);
        x[3]  += x[7];    x[15] = rol32(x[15] ^ x[3],  16);

        x[8]  += x[12];   x[4]  = rol32(x[4]  ^ x[8],  12);
        x[9]  += x[13];   x[5]  = rol32(x[5]  ^ x[9],  12);
        x[10] += x[14];   x[6]  = rol32(x[6]  ^ x[10], 12);
        x[11] += x[15];   x[7]  = rol32(x[7]  ^ x[11], 12);

        x[0]  += x[4];    x[12] = rol32(x[12] ^ x[0],   8);
        x[1]  += x[5];    x[13] = rol32(x[13] ^ x[1],   8);
        x[2]  += x[6];    x[14] = rol32(x[14] ^ x[2],   8);
        x[3]  += x[7];    x[15] = rol32(x[15] ^ x[3],   8);

        x[8]  += x[12];   x[4]  = rol32(x[4]  ^ x[8],   7);
        x[9]  += x[13];   x[5]  = rol32(x[5]  ^ x[9],   7);
        x[10] += x[14];   x[6]  = rol32(x[6]  ^ x[10],  7);
        x[11] += x[15];   x[7]  = rol32(x[7]  ^ x[11],  7);

        x[0]  += x[5];    x[15] = rol32(x[15] ^ x[0],  16);
        x[1]  += x[6];    x[12] = rol32(x[12] ^ x[1],  16);
        x[2]  += x[7];    x[13] = rol32(x[13] ^ x[2],  16);
        x[3]  += x[4];    x[14] = rol32(x[14] ^ x[3],  16);

        x[10] += x[15];   x[5]  = rol32(x[5]  ^ x[10], 12);
        x[11] += x[12];   x[6]  = rol32(x[6]  ^ x[11], 12);
        x[8]  += x[13];   x[7]  = rol32(x[7]  ^ x[8],  12);
        x[9]  += x[14];   x[4]  = rol32(x[4]  ^ x[9],  12);

        x[0]  += x[5];    x[15] = rol32(x[15] ^ x[0],   8);
        x[1]  += x[6];    x[12] = rol32(x[12] ^ x[1],   8);
        x[2]  += x[7];    x[13] = rol32(x[13] ^ x[2],   8);
        x[3]  += x[4];    x[14] = rol32(x[14] ^ x[3],   8);

        x[10] += x[15];   x[5]  = rol32(x[5]  ^ x[10],  7);
        x[11] += x[12];   x[6]  = rol32(x[6]  ^ x[11],  7);
        x[8]  += x[13];   x[7]  = rol32(x[7]  ^ x[8],   7);
        x[9]  += x[14];   x[4]  = rol32(x[4]  ^ x[9],   7);
    }
}

static void chacha_block_generic(u32 *state, u8 *stream, int nrounds)
{
    u32 x[16];

    memcpy(x, state, 64);
    chacha_permute(x, nrounds);

    for (int i = 0; i < 16; i++) {
        put_unaligned_le32(x[i] + state[i], &stream[i * sizeof(u32)]);
    }
    state[12]++;
}

static void crypto_xor_cpy(u8 *dst, const u8 *src1,
                           const u8 *src2, unsigned len)
{
    while (len--) {
        *dst++ = *src1++ ^ *src2++;
    }
}

static void chacha_crypt_generic(u32 *state, u8 *dst, const u8 *src,
                                 unsigned int bytes, int nrounds)
{
    u8 stream[CHACHA_BLOCK_SIZE];

    while (bytes >= CHACHA_BLOCK_SIZE) {
        chacha_block_generic(state, stream, nrounds);
        crypto_xor_cpy(dst, src, stream, CHACHA_BLOCK_SIZE);
        bytes -= CHACHA_BLOCK_SIZE;
        dst += CHACHA_BLOCK_SIZE;
        src += CHACHA_BLOCK_SIZE;
    }
    if (bytes) {
        chacha_block_generic(state, stream, nrounds);
        crypto_xor_cpy(dst, src, stream, bytes);
    }
}

enum chacha_constants { /* expand 32-byte k */
    CHACHA_CONSTANT_EXPA = 0x61707865U,
    CHACHA_CONSTANT_ND_3 = 0x3320646eU,
    CHACHA_CONSTANT_2_BY = 0x79622d32U,
    CHACHA_CONSTANT_TE_K = 0x6b206574U
};

static void chacha_init_generic(u32 *state, const u32 *key, const u8 *iv)
{
    state[0]  = CHACHA_CONSTANT_EXPA;
    state[1]  = CHACHA_CONSTANT_ND_3;
    state[2]  = CHACHA_CONSTANT_2_BY;
    state[3]  = CHACHA_CONSTANT_TE_K;
    state[4]  = key[0];
    state[5]  = key[1];
    state[6]  = key[2];
    state[7]  = key[3];
    state[8]  = key[4];
    state[9]  = key[5];
    state[10] = key[6];
    state[11] = key[7];
    state[12] = get_unaligned_le32(iv +  0);
    state[13] = get_unaligned_le32(iv +  4);
    state[14] = get_unaligned_le32(iv +  8);
    state[15] = get_unaligned_le32(iv + 12);
}

void chacha20_vx(u8 *out, const u8 *inp, size_t len, const u32 *key,
                 const u32 *counter);

static void chacha20_crypt_s390(u32 *state, u8 *dst, const u8 *src,
                                unsigned int nbytes, const u32 *key,
                                u32 *counter)
{
    chacha20_vx(dst, src, nbytes, key, counter);
    *counter += (nbytes + CHACHA_BLOCK_SIZE - 1) / CHACHA_BLOCK_SIZE;
}

static void chacha_crypt_arch(u32 *state, u8 *dst, const u8 *src,
                              unsigned int bytes, int nrounds)
{
    /*
     * s390 chacha20 implementation has 20 rounds hard-coded,
     * it cannot handle a block of data or less, but otherwise
     * it can handle data of arbitrary size
     */
    if (bytes <= CHACHA_BLOCK_SIZE || nrounds != 20) {
        chacha_crypt_generic(state, dst, src, bytes, nrounds);
    } else {
        chacha20_crypt_s390(state, dst, src, bytes, &state[4], &state[12]);
    }
}

static void print_hex_dump(const char *prefix_str, const void *buf, int len)
{
    for (int i = 0; i < len; i += 16) {
        printf("%s%.8x: ", prefix_str, i);
        for (int j = 0; j < 16; ++j) {
            printf("%02x%c", *(u8 *)(buf + i + j), j == 15 ? '\n' : ' ');
        }
    }
}

/* Perform cipher operations with the chacha lib */
static int test_lib_chacha(u8 *revert, u8 *cipher, u8 *plain, bool generic)
{
    u32 chacha_state[CHACHA_STATE_WORDS];
    u8 iv[16], key[32];

    memset(key, 'X', sizeof(key));
    memset(iv, 'I', sizeof(iv));

    if (debug) {
        print_hex_dump("key: ", key, 32);
        print_hex_dump("iv:  ", iv, 16);
    }

    /* Encrypt */
    chacha_init_generic(chacha_state, (u32*)key, iv);

    if (generic) {
        chacha_crypt_generic(chacha_state, cipher, plain, data_size, 20);
    } else {
        chacha_crypt_arch(chacha_state, cipher, plain, data_size, 20);
    }

    if (debug) {
        print_hex_dump("encr:", cipher,
                       (data_size > 64 ? 64 : data_size));
    }

    /* Decrypt */
    chacha_init_generic(chacha_state, (u32 *)key, iv);

    if (generic) {
        chacha_crypt_generic(chacha_state, revert, cipher, data_size, 20);
    } else {
        chacha_crypt_arch(chacha_state, revert, cipher, data_size, 20);
    }

    if (debug) {
        print_hex_dump("decr:", revert,
                       (data_size > 64 ? 64 : data_size));
    }
    return 0;
}

static int chacha_s390_test_init(void)
{
    u8 *plain = NULL, *revert = NULL;
    u8 *cipher_generic = NULL, *cipher_s390 = NULL;
    int ret = -1;

    printf("s390 ChaCha20 test module: size=%d debug=%d\n",
           data_size, debug);

    /* Allocate and fill buffers */
    plain = malloc(data_size);
    if (!plain) {
        printf("could not allocate plain buffer\n");
        ret = -2;
        goto out;
    }

    memset(plain, 'a', data_size);
    for (unsigned i = 0, n = data_size > 256 ? 256 : data_size; i < n; ) {
        ssize_t t = getrandom(plain + i, n - i, 0);
        if (t < 0) {
            break;
        }
        i -= t;
    }

    cipher_generic = calloc(1, data_size);
    if (!cipher_generic) {
        printf("could not allocate cipher_generic buffer\n");
        ret = -2;
        goto out;
    }

    cipher_s390 = calloc(1, data_size);
    if (!cipher_s390) {
        printf("could not allocate cipher_s390 buffer\n");
        ret = -2;
        goto out;
    }

    revert = calloc(1, data_size);
    if (!revert) {
        printf("could not allocate revert buffer\n");
        ret = -2;
        goto out;
    }

    if (debug) {
        print_hex_dump("src: ", plain,
                       (data_size > 64 ? 64 : data_size));
    }

    /* Use chacha20 lib */
    test_lib_chacha(revert, cipher_generic, plain, true);
    if (memcmp(plain, revert, data_size)) {
        printf("generic en/decryption check FAILED\n");
        ret = -2;
        goto out;
    }
    printf("generic en/decryption check OK\n");

    test_lib_chacha(revert, cipher_s390, plain, false);
    if (memcmp(plain, revert, data_size)) {
        printf("lib en/decryption check FAILED\n");
        ret = -2;
        goto out;
    }
    printf("lib en/decryption check OK\n");

    if (memcmp(cipher_generic, cipher_s390, data_size)) {
        printf("lib vs generic check FAILED\n");
        ret = -2;
        goto out;
    }
    printf("lib vs generic check OK\n");

    printf("--- chacha20 s390 test end ---\n");

out:
    free(plain);
    free(cipher_generic);
    free(cipher_s390);
    free(revert);
    return ret;
}

int main(int ac, char **av)
{
    static const unsigned sizes[] = {
        63, 64, 65, 127, 128, 129, 511, 512, 513, 4096, 65611,
        /* too slow for tcg: 6291456, 62914560 */
    };

    debug = ac >= 2;
    for (int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        data_size = sizes[i];
        if (chacha_s390_test_init() != -1) {
            return 1;
        }
    }
    return 0;
}
