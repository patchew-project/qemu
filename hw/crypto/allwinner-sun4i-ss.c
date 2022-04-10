/*
 * Allwinner sun4i-ss cryptographic offloader emulation
 *
 * Copyright (C) 2022 Corentin Labbe <clabbe@baylibre.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "trace.h"
#include "qemu/module.h"
#include "exec/cpu-common.h"
#include "hw/crypto/allwinner-sun4i-ss.h"

#include <nettle/aes.h>
#include <nettle/cbc.h>
#include <nettle/des.h>
#include <nettle/md5.h>
#include <nettle/sha1.h>

#define SS_IV_ARBITRARY (1 << 14)

/* SS operation mode - bits 12-13 */
#define SS_ECB (0 << 12)
#define SS_CBC (1 << 12)

/* Key size for AES - bits 8-9 */
#define SS_AES_128BITS (0 << 8)
#define SS_AES_192BITS (1 << 8)
#define SS_AES_256BITS (2 << 8)

/* Operation direction - bit 7 */
#define SS_ENCRYPTION  (0 << 7)
#define SS_DECRYPTION  (1 << 7)

/* SS Method - bits 4-6 */
#define SS_OP_AES      (0 << 4)
#define SS_OP_DES      (1 << 4)
#define SS_OP_3DES     (2 << 4)
#define SS_OP_SHA1     (3 << 4)
#define SS_OP_MD5      (4 << 4)
#define SS_OP_PRNG     (5 << 4)

/* Data end bit - bit 2 */
#define SS_DATA_END (1 << 2)

/* SS Enable bit - bit 0 */
#define SS_ENABLED (1 << 0)

enum {
    REG_CTL        = 0x0000,
    REG_KEY_0      = 0x0004,
    REG_KEY_1      = 0x0008,
    REG_KEY_2      = 0x000c,
    REG_KEY_3      = 0x0010,
    REG_KEY_4      = 0x0014,
    REG_KEY_5      = 0x0018,
    REG_KEY_6      = 0x001c,
    REG_KEY_7      = 0x0020,
    REG_IV_0       = 0x0024,
    REG_IV_1       = 0x0028,
    REG_IV_2       = 0x002c,
    REG_IV_3       = 0x0030,
    REG_IV_4       = 0x0034,
    REG_FCSR       = 0x0044,
    REG_ICSR       = 0x0048,
    REG_MD0        = 0x004c,
    REG_MD1        = 0x0050,
    REG_MD2        = 0x0054,
    REG_MD3        = 0x0058,
    REG_MD4        = 0x005c,
    REG_RXFIFO     = 0x0200,
    REG_TXFIFO     = 0x0204,
};

static void allwinner_sun4i_ss_try_work(AwSun4iSSState *s);

/* return number of possible operation wih block size=bs */
static unsigned int can_work(AwSun4iSSState *s, unsigned int bs)
{
    unsigned int avail_rx = s->rxc / (bs / 4);
    unsigned int free_space_tx = (SS_TX_MAX - s->txc) / (bs / 4);

    if (avail_rx > free_space_tx) {
        return free_space_tx;
    }
    return avail_rx;
}

/*
 * Without any knowledge on the PRNG, the only solution is
 * to emulate it via g_random_int()
 */
static void do_prng(AwSun4iSSState *s)
{
    unsigned int size = 20;
    int i;

    for (i = 0; i < size / 4; i++) {
        s->tx[i] = g_random_int();
    }
    s->txc += size / 4;
}

/* remove pop u32 words from RX */
static void rx_pop(AwSun4iSSState *s, unsigned int pop)
{
    uint32_t *rx = (uint32_t *)s->rx;
    int i;

    for (i = 0; i < s->rxc; i++) {
        rx[i] = rx[i + pop];
    }
}

static void do_md5(AwSun4iSSState *s)
{
    unsigned int size = MD5_BLOCK_SIZE;
    unsigned char *src = s->rx;

    nettle_md5_compress(s->md, src);

    s->rxc -= size / 4;
    if (s->rxc > 0) {
        rx_pop(s, size / 4);
        allwinner_sun4i_ss_try_work(s);
    }
}

static void do_sha1(AwSun4iSSState *s)
{
    unsigned int size = SHA1_BLOCK_SIZE;
    unsigned char *src = s->rx;

    nettle_sha1_compress(s->md, src);

    s->rxc -= size / 4;
    if (s->rxc > 0) {
        rx_pop(s, size / 4);
        allwinner_sun4i_ss_try_work(s);
    }
}

static void do_des(AwSun4iSSState *s)
{
    struct CBC_CTX(struct des_ctx, DES_BLOCK_SIZE) cdes;
    struct des_ctx des;
    unsigned char *src = s->rx;
    unsigned char *dst = s->tx + s->txc * 4;
    unsigned char *key = (unsigned char *)s->key;
    unsigned int size = DES_BLOCK_SIZE;
    unsigned char biv[DES_BLOCK_SIZE];

    if (s->ctl & SS_DECRYPTION) {
        memcpy(biv, src, DES_BLOCK_SIZE);
    }

    if (s->ctl & SS_CBC) {
        CBC_SET_IV(&cdes, s->iv);
        if (s->ctl & SS_DECRYPTION) {
            des_set_key(&cdes.ctx, key);
            CBC_DECRYPT(&cdes, des_decrypt, size, dst, src);
        } else {
            des_set_key(&cdes.ctx, key);
            CBC_ENCRYPT(&cdes, des_encrypt, size, dst, src);
        }
        /* Copy next IV in registers */
        if (s->ctl & SS_DECRYPTION) {
            memcpy(s->iv, biv, DES_BLOCK_SIZE);
        } else {
            memcpy(s->iv, dst, DES_BLOCK_SIZE);
        }
    } else {
        if (s->ctl & SS_DECRYPTION) {
            des_set_key(&des, key);
            des_decrypt(&des, size, dst, src);
        } else {
                des_set_key(&des, key);
                des_encrypt(&des, size, dst, src);
            }
    }
    s->txc += size / 4;
    s->rxc -= size / 4;

    if (s->rxc > 0) {
        rx_pop(s, size / 4);
        allwinner_sun4i_ss_try_work(s);
    }
}

static void do_des3(AwSun4iSSState *s)
{
    struct CBC_CTX(struct des3_ctx, DES3_BLOCK_SIZE) cdes3;
    struct des3_ctx des3;
    unsigned char *src = s->rx;
    unsigned char *dst = s->tx + s->txc * 4;
    unsigned char *key = (unsigned char *)s->key;
    unsigned int size = DES3_BLOCK_SIZE;
    unsigned char biv[DES3_BLOCK_SIZE];

    if (s->ctl & SS_DECRYPTION) {
        memcpy(biv, src, DES3_BLOCK_SIZE);
    }

    if (s->ctl & SS_CBC) {
        CBC_SET_IV(&cdes3, s->iv);
        if (s->ctl & SS_DECRYPTION) {
            des3_set_key(&cdes3.ctx, key);
            CBC_DECRYPT(&cdes3, des3_decrypt, size, dst, src);
        } else {
            des3_set_key(&cdes3.ctx, key);
            CBC_ENCRYPT(&cdes3, des3_encrypt, size, dst, src);
        }
        /* Copy next IV in registers */
        if (s->ctl & SS_DECRYPTION) {
            memcpy(s->iv, biv, DES3_BLOCK_SIZE);
        } else {
            memcpy(s->iv, dst, DES3_BLOCK_SIZE);
        }
    } else {
        if (s->ctl & SS_DECRYPTION) {
            des3_set_key(&des3, key);
            des3_decrypt(&des3, size, dst, src);
        } else {
            des3_set_key(&des3, key);
            des3_encrypt(&des3, size, dst, src);
        }
    }
    s->txc += size / 4;
    s->rxc -= size / 4;

    if (s->rxc > 0) {
        rx_pop(s, size / 4);
        allwinner_sun4i_ss_try_work(s);
    }
}

static void do_aes(AwSun4iSSState *s)
{
    struct CBC_CTX(struct aes128_ctx, AES_BLOCK_SIZE) aes128;
    struct CBC_CTX(struct aes192_ctx, AES_BLOCK_SIZE) aes192;
    struct CBC_CTX(struct aes256_ctx, AES_BLOCK_SIZE) aes256;
    struct aes128_ctx ecb128;
    struct aes192_ctx ecb192;
    struct aes256_ctx ecb256;
    unsigned char *src = s->rx;
    unsigned char *dst = s->tx + s->txc * 4;
    unsigned char *key = (unsigned char *)s->key;
    unsigned int size = AES_BLOCK_SIZE;
    unsigned char biv[AES_BLOCK_SIZE];

    if (s->ctl & SS_DECRYPTION) {
        memcpy(biv, src, AES_BLOCK_SIZE);
    }

    if (s->ctl & SS_CBC) {
        switch (s->ctl & 0x300) {
        case SS_AES_128BITS:
            CBC_SET_IV(&aes128, s->iv);

            if (s->ctl & SS_DECRYPTION) {
                aes128_set_decrypt_key(&aes128.ctx, key);
                CBC_DECRYPT(&aes128, aes128_decrypt, size, dst, src);
            } else {
                aes128_set_encrypt_key(&aes128.ctx, key);
                CBC_ENCRYPT(&aes128, aes128_encrypt, size, dst, src);
            }
            break;
        case SS_AES_192BITS:
            CBC_SET_IV(&aes192, s->iv);

            if (s->ctl & SS_DECRYPTION) {
                aes192_set_decrypt_key(&aes192.ctx, key);
                CBC_DECRYPT(&aes192, aes192_decrypt, size, dst, src);
            } else {
                aes192_set_encrypt_key(&aes192.ctx, key);
                CBC_ENCRYPT(&aes192, aes192_encrypt, size, dst, src);
            }
            break;
        case SS_AES_256BITS:
            CBC_SET_IV(&aes256, s->iv);

            if (s->ctl & SS_DECRYPTION) {
                aes256_set_decrypt_key(&aes256.ctx, key);
                CBC_DECRYPT(&aes256, aes256_decrypt, size, dst, src);
            } else {
                aes256_set_encrypt_key(&aes256.ctx, key);
                CBC_ENCRYPT(&aes256, aes256_encrypt, size, dst, src);
            }
            break;
        }
        /* Copy next IV in registers */
        if (s->ctl & SS_DECRYPTION) {
            memcpy(s->iv, biv, AES_BLOCK_SIZE);
        } else {
            memcpy(s->iv, dst, AES_BLOCK_SIZE);
        }
    } else {
        switch (s->ctl & 0x300) {
        case SS_AES_128BITS:
            if (s->ctl & SS_DECRYPTION) {
                aes128_set_decrypt_key(&ecb128, key);
                aes128_decrypt(&ecb128, size, dst, src);
            } else {
                aes128_set_encrypt_key(&ecb128, key);
                aes128_encrypt(&ecb128, size, dst, src);
            }
            break;
        case SS_AES_192BITS:
            if (s->ctl & SS_DECRYPTION) {
                aes192_set_decrypt_key(&ecb192, key);
                aes192_decrypt(&ecb192, size, dst, src);
            } else {
                aes192_set_encrypt_key(&ecb192, key);
                aes192_encrypt(&ecb192, size, dst, src);
            }
            break;
        case SS_AES_256BITS:
            if (s->ctl & SS_DECRYPTION) {
                aes256_set_decrypt_key(&ecb256, (const unsigned char *) s->key);
                aes256_decrypt(&ecb256, size, dst, src);
            } else {
                aes256_set_encrypt_key(&ecb256, (const unsigned char *) s->key);
                aes256_encrypt(&ecb256, size, dst, src);
            }
            break;
        }
    }
    s->txc += size / 4;
    s->rxc -= size / 4;

    if (s->rxc > 0) {
        rx_pop(s, size / 4);
        allwinner_sun4i_ss_try_work(s);
    }
}

static void allwinner_sun4i_ss_update_fcsr(AwSun4iSSState *s)
{
    s->fcsr = (s->txc << 16) | ((32 - s->rxc) << 24);
}

static void allwinner_sun4i_ss_try_work(AwSun4iSSState *s)
{
    if (!(s->ctl & SS_ENABLED)) {
        return;
    }
    if ((s->ctl & 0x70) == SS_OP_AES && can_work(s, AES_BLOCK_SIZE)) {
        do_aes(s);
        allwinner_sun4i_ss_update_fcsr(s);
        return;
    }
    if ((s->ctl & 0x70) == SS_OP_DES && can_work(s, DES_BLOCK_SIZE)) {
        do_des(s);
        allwinner_sun4i_ss_update_fcsr(s);
        return;
    }
    if ((s->ctl & 0x70) == SS_OP_3DES && can_work(s, DES3_BLOCK_SIZE)) {
        do_des3(s);
        allwinner_sun4i_ss_update_fcsr(s);
        return;
    }
    if ((s->ctl & 0x70) == SS_OP_MD5 && s->rxc >= MD5_BLOCK_SIZE / 4) {
        do_md5(s);
        allwinner_sun4i_ss_update_fcsr(s);
        return;
    }
    if ((s->ctl & 0x70) == SS_OP_SHA1 && s->rxc >= SHA1_BLOCK_SIZE / 4) {
        do_sha1(s);
        allwinner_sun4i_ss_update_fcsr(s);
        return;
    }
    if ((s->ctl & 0x70) == SS_OP_PRNG) {
        do_prng(s);
        allwinner_sun4i_ss_update_fcsr(s);
        return;
    }
}

static uint32_t tx_pop(AwSun4iSSState *s)
{
    uint32_t *tx = (uint32_t *)s->tx;
    uint32_t v = 0;
    int i;

    if (s->txc > 0) {
        v = tx[0];
        s->txc--;
        for (i = 0; i < s->txc; i++) {
            tx[i] = tx[i + 1];
        }
        allwinner_sun4i_ss_update_fcsr(s);
        allwinner_sun4i_ss_try_work(s);
    }
    return v;
}

static void allwinner_sun4i_ss_reset_common(AwSun4iSSState *s)
{
    s->ctl = 0;
    s->txc = 0;
    s->rxc = 0;
    allwinner_sun4i_ss_update_fcsr(s);
}

static void allwinner_sun4i_ss_reset(DeviceState *dev)
{
    AwSun4iSSState *s = AW_SUN4I_SS(dev);

    trace_allwinner_sun4i_ss_reset();

    allwinner_sun4i_ss_reset_common(s);
}

static uint64_t allwinner_sun4i_ss_read(void *opaque, hwaddr offset,
                                          unsigned size)
{
    AwSun4iSSState *s = AW_SUN4I_SS(opaque);
    uint64_t value = 0;

    switch (offset) {
    case REG_CTL:
        value = s->ctl;
        break;
    case REG_IV_0:
        value = s->iv[0];
        break;
    case REG_IV_1:
        value = s->iv[1];
        break;
    case REG_IV_2:
        value = s->iv[2];
        break;
    case REG_IV_3:
        value = s->iv[3];
        break;
    case REG_IV_4:
        value = s->iv[4];
        break;
    case REG_FCSR:
        value = s->fcsr;
        break;
    case REG_KEY_0:
        value = s->key[0];
        break;
    case REG_KEY_1:
        value = s->key[1];
        break;
    case REG_KEY_2:
        value = s->key[2];
        break;
    case REG_KEY_3:
        value = s->key[3];
        break;
    case REG_KEY_4:
        value = s->key[4];
        break;
    case REG_KEY_5:
        value = s->key[5];
        break;
    case REG_KEY_6:
        value = s->key[6];
        break;
    case REG_KEY_7:
        value = s->key[7];
        break;
    case REG_MD0:
        value = s->md[0];
        break;
    case REG_MD1:
        value = s->md[1];
        break;
    case REG_MD2:
        value = s->md[2];
        break;
    case REG_MD3:
        value = s->md[3];
        break;
    case REG_MD4:
        value = s->md[4];
        break;
    case REG_TXFIFO:
        value = tx_pop(s);
        break;
    case REG_RXFIFO:
        value = s->rx[0];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "allwinner_sun4i_ss: read access to unknown "
                                 "CRYPTO register 0x" TARGET_FMT_plx "\n",
                                  offset);
    }

    trace_allwinner_sun4i_ss_read(offset, value);
    return value;
}

static void rx_push(AwSun4iSSState *s, uint32_t value)
{
    uint32_t *rx = (uint32_t *)s->rx;

    if (!(s->ctl & SS_ENABLED)) {
        return;
    }
    if (s->rxc > SS_RX_MAX) {
        return;
    }
    rx[s->rxc] = value;
    s->rxc++;
    allwinner_sun4i_ss_update_fcsr(s);
    allwinner_sun4i_ss_try_work(s);

    return;
}

static void allwinner_sun4i_ss_write(void *opaque, hwaddr offset,
                                       uint64_t value, unsigned size)
{
    AwSun4iSSState *s = AW_SUN4I_SS(opaque);
    bool was_disabled = !(s->ctl & SS_ENABLED);

    trace_allwinner_sun4i_ss_write(offset, value);

    switch (offset) {
    case REG_CTL:
        s->ctl = value;
        if (!(s->ctl & SS_ENABLED)) {
            allwinner_sun4i_ss_reset_common(s);
            break;
        }
        if (was_disabled) {
            if (s->ctl & SS_IV_ARBITRARY) {
                s->md[0] = s->iv[0];
                s->md[1] = s->iv[1];
                s->md[2] = s->iv[2];
                s->md[3] = s->iv[3];
                s->md[4] = s->iv[4];
            } else {
                if ((s->ctl & 0x70) == SS_OP_MD5) {
                    s->md[0] = 0x67452301;
                    s->md[1] = 0xefcdab89;
                    s->md[2] = 0x98badcfe;
                    s->md[3] = 0x10325476;
                } else {
                    s->md[0] = 0x67452301;
                    s->md[1] = 0xefcdab89;
                    s->md[2] = 0x98badcfe;
                    s->md[3] = 0x10325476;
                    s->md[4] = 0xC3D2E1F0;
                }
            }
        }
        if ((s->ctl & 0x70) == SS_OP_PRNG) {
            do_prng(s);
            allwinner_sun4i_ss_update_fcsr(s);
        }
        if ((s->ctl & 0x70) == SS_OP_MD5 && s->ctl & SS_DATA_END) {
            s->ctl &= ~SS_DATA_END;
            return;
        }
        if ((s->ctl & 0x70) == SS_OP_SHA1 && s->ctl & SS_DATA_END) {
            s->ctl &= ~SS_DATA_END;
            return;
        }
        break;
    case REG_IV_0:
        s->iv[0] = value;
        break;
    case REG_IV_1:
        s->iv[1] = value;
        break;
    case REG_IV_2:
        s->iv[2] = value;
        break;
    case REG_IV_3:
        s->iv[3] = value;
        break;
    case REG_IV_4:
        s->iv[4] = value;
        break;
    case REG_KEY_0:
        s->key[0] = value;
        break;
    case REG_KEY_1:
        s->key[1] = value;
        break;
    case REG_KEY_2:
        s->key[2] = value;
        break;
    case REG_KEY_3:
        s->key[3] = value;
        break;
    case REG_KEY_4:
        s->key[4] = value;
        break;
    case REG_KEY_5:
        s->key[5] = value;
        break;
    case REG_KEY_6:
        s->key[6] = value;
        break;
    case REG_KEY_7:
        s->key[7] = value;
        break;
    case REG_RXFIFO:
        rx_push(s, value);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "allwinner_sun4i_ss: write access to unknown "
                                 "CRYPTO register 0x" TARGET_FMT_plx "\n",
                                  offset);
    }
}

static const MemoryRegionOps allwinner_sun4i_ss_mem_ops = {
    .read = allwinner_sun4i_ss_read,
    .write = allwinner_sun4i_ss_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_sun4i_ss_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwSun4iSSState *s = AW_SUN4I_SS(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_sun4i_ss_mem_ops,
                           s, TYPE_AW_SUN4I_SS, 4 * KiB);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_allwinner_sun4i_ss = {
    .name = "allwinner-sun4i-ss",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctl, AwSun4iSSState),
        VMSTATE_UINT32(fcsr, AwSun4iSSState),
        VMSTATE_UINT32_ARRAY(iv, AwSun4iSSState, 5),
        VMSTATE_UINT32_ARRAY(key, AwSun4iSSState, 8),
        VMSTATE_UINT32_ARRAY(md, AwSun4iSSState, 5),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_sun4i_ss_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = allwinner_sun4i_ss_reset;
    dc->vmsd = &vmstate_allwinner_sun4i_ss;
}

static const TypeInfo allwinner_sun4i_ss_info = {
    .name           = TYPE_AW_SUN4I_SS,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(AwSun4iSSState),
    .instance_init  = allwinner_sun4i_ss_init,
    .class_init     = allwinner_sun4i_ss_class_init,
};

static void allwinner_sun4i_ss_register_types(void)
{
    type_register_static(&allwinner_sun4i_ss_info);
}

type_init(allwinner_sun4i_ss_register_types)
