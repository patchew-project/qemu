/*
 * QEMU Apple AES device emulation
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "crypto/hash.h"
#include "crypto/aes.h"
#include "crypto/cipher.h"

#define TYPE_AES          "apple-aes"
#define MAX_FIFO_SIZE     9

#define CMD_KEY           0x1
#define CMD_KEY_CONTEXT_SHIFT    27
#define CMD_KEY_CONTEXT_MASK     (0x1 << CMD_KEY_CONTEXT_SHIFT)
#define CMD_KEY_SELECT_SHIFT     24
#define CMD_KEY_SELECT_MASK      (0x7 << CMD_KEY_SELECT_SHIFT)
#define CMD_KEY_KEY_LEN_SHIFT    22
#define CMD_KEY_KEY_LEN_MASK     (0x3 << CMD_KEY_KEY_LEN_SHIFT)
#define CMD_KEY_ENCRYPT_SHIFT    20
#define CMD_KEY_ENCRYPT_MASK     (0x1 << CMD_KEY_ENCRYPT_SHIFT)
#define CMD_KEY_BLOCK_MODE_SHIFT 16
#define CMD_KEY_BLOCK_MODE_MASK  (0x3 << CMD_KEY_BLOCK_MODE_SHIFT)
#define CMD_IV            0x2
#define CMD_IV_CONTEXT_SHIFT     26
#define CMD_IV_CONTEXT_MASK      (0x3 << CMD_KEY_CONTEXT_SHIFT)
#define CMD_DSB           0x3
#define CMD_SKG           0x4
#define CMD_DATA          0x5
#define CMD_DATA_KEY_CTX_SHIFT   27
#define CMD_DATA_KEY_CTX_MASK    (0x1 << CMD_DATA_KEY_CTX_SHIFT)
#define CMD_DATA_IV_CTX_SHIFT    25
#define CMD_DATA_IV_CTX_MASK     (0x3 << CMD_DATA_IV_CTX_SHIFT)
#define CMD_DATA_LEN_MASK        0xffffff
#define CMD_STORE_IV      0x6
#define CMD_STORE_IV_ADDR_MASK   0xffffff
#define CMD_WRITE_REG     0x7
#define CMD_FLAG          0x8
#define CMD_FLAG_STOP_MASK       BIT(26)
#define CMD_FLAG_RAISE_IRQ_MASK  BIT(27)
#define CMD_FLAG_INFO_MASK       0xff
#define CMD_MAX           0x10

#define CMD_SHIFT         28

#define REG_STATUS            0xc
#define REG_STATUS_DMA_READ_RUNNING     BIT(0)
#define REG_STATUS_DMA_READ_PENDING     BIT(1)
#define REG_STATUS_DMA_WRITE_RUNNING    BIT(2)
#define REG_STATUS_DMA_WRITE_PENDING    BIT(3)
#define REG_STATUS_BUSY                 BIT(4)
#define REG_STATUS_EXECUTING            BIT(5)
#define REG_STATUS_READY                BIT(6)
#define REG_STATUS_TEXT_DPA_SEEDED      BIT(7)
#define REG_STATUS_UNWRAP_DPA_SEEDED    BIT(8)

#define REG_IRQ_STATUS        0x18
#define REG_IRQ_STATUS_INVALID_CMD      BIT(2)
#define REG_IRQ_STATUS_FLAG             BIT(5)
#define REG_IRQ_ENABLE        0x1c
#define REG_WATERMARK         0x20
#define REG_Q_STATUS          0x24
#define REG_FLAG_INFO         0x30
#define REG_FIFO              0x200

static const uint32_t key_lens[4] = {
    [0] = 16,
    [1] = 24,
    [2] = 32,
    [3] = 64,
};

struct key {
    uint32_t key_len;
    uint32_t key[8];
};

struct iv {
    uint32_t iv[4];
};

struct context {
    struct key key;
    struct iv iv;
};

static struct key builtin_keys[7] = {
    [1] = {
        .key_len = 32,
        .key = { 0x1 },
    },
    [2] = {
        .key_len = 32,
        .key = { 0x2 },
    },
    [3] = {
        .key_len = 32,
        .key = { 0x3 },
    }
};

typedef struct AESState {
    /* Private */
    SysBusDevice parent_obj;

    /* Public */
    qemu_irq irq;
    MemoryRegion iomem1;
    MemoryRegion iomem2;

    uint32_t status;
    uint32_t q_status;
    uint32_t irq_status;
    uint32_t irq_enable;
    uint32_t watermark;
    uint32_t flag_info;
    uint32_t fifo[MAX_FIFO_SIZE];
    uint32_t fifo_idx;
    struct key key[2];
    struct iv iv[4];
    bool is_encrypt;
    QCryptoCipherMode block_mode;
} AESState;

OBJECT_DECLARE_SIMPLE_TYPE(AESState, AES)

static void aes_update_irq(AESState *s)
{
    qemu_set_irq(s->irq, !!(s->irq_status & s->irq_enable));
}

static uint64_t aes1_read(void *opaque, hwaddr offset, unsigned size)
{
    AESState *s = opaque;
    uint64_t res = 0;

    switch (offset) {
    case REG_STATUS:
        res = s->status;
        break;
    case REG_IRQ_STATUS:
        res = s->irq_status;
        break;
    case REG_IRQ_ENABLE:
        res = s->irq_enable;
        break;
    case REG_WATERMARK:
        res = s->watermark;
        break;
    case REG_Q_STATUS:
        res = s->q_status;
        break;
    case REG_FLAG_INFO:
        res = s->flag_info;
        break;

    default:
        trace_aes_read_unknown(offset);
        break;
    }

    trace_aes_read(offset, res);

    return res;
}

static void fifo_append(AESState *s, uint64_t val)
{
    if (s->fifo_idx == MAX_FIFO_SIZE) {
        /* Exceeded the FIFO. Bail out */
        return;
    }

    s->fifo[s->fifo_idx++] = val;
}

static bool has_payload(AESState *s, uint32_t elems)
{
    return s->fifo_idx >= (elems + 1);
}

static bool cmd_key(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t key_select = (cmd & CMD_KEY_SELECT_MASK) >> CMD_KEY_SELECT_SHIFT;
    uint32_t ctxt = (cmd & CMD_KEY_CONTEXT_MASK) >> CMD_KEY_CONTEXT_SHIFT;
    uint32_t key_len;

    switch ((cmd & CMD_KEY_BLOCK_MODE_MASK) >> CMD_KEY_BLOCK_MODE_SHIFT) {
    case 0:
        s->block_mode = QCRYPTO_CIPHER_MODE_ECB;
        break;
    case 1:
        s->block_mode = QCRYPTO_CIPHER_MODE_CBC;
        break;
    default:
        return false;
    }

    s->is_encrypt = !!((cmd & CMD_KEY_ENCRYPT_MASK) >> CMD_KEY_ENCRYPT_SHIFT);
    key_len = key_lens[((cmd & CMD_KEY_KEY_LEN_MASK) >> CMD_KEY_KEY_LEN_SHIFT)];

    if (key_select) {
        trace_aes_cmd_key_select_builtin(ctxt, key_select,
                                         s->is_encrypt ? "en" : "de",
                                         QCryptoCipherMode_str(s->block_mode));
        s->key[ctxt] = builtin_keys[key_select];
    } else {
        trace_aes_cmd_key_select_new(ctxt, key_len,
                                     s->is_encrypt ? "en" : "de",
                                     QCryptoCipherMode_str(s->block_mode));
        if (key_len > sizeof(s->key[ctxt].key)) {
            return false;
        }
        if (!has_payload(s, key_len / sizeof(uint32_t))) {
            /* wait for payload */
            return false;
        }
        memcpy(&s->key[ctxt].key, &s->fifo[1], key_len);
        s->key[ctxt].key_len = key_len;
    }

    return true;
}

static bool cmd_iv(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t ctxt = (cmd & CMD_IV_CONTEXT_MASK) >> CMD_IV_CONTEXT_SHIFT;

    if (!has_payload(s, 4)) {
        /* wait for payload */
        return false;
    }
    memcpy(&s->iv[ctxt].iv, &s->fifo[1], sizeof(s->iv[ctxt].iv));
    trace_aes_cmd_iv(ctxt, s->fifo[1], s->fifo[2], s->fifo[3], s->fifo[4]);

    return true;
}

static char hexdigit2str(uint8_t val)
{
    g_assert(val < 0x10);
    if (val >= 0xa) {
        return 'a' + (val - 0xa);
    } else {
        return '0' + val;
    }
}

static void dump_data(const char *desc, const void *p, size_t len)
{
    char hex[(len * 2) + 1];
    const uint8_t *data = p;
    char *hexp = hex;
    size_t i;

    if (len > 0x1000) {
        /* Too large buffer, let's bail out */
        return;
    }

    for (i = 0; i < len; i++) {
        uint8_t val = data[i];
        *(hexp++) = hexdigit2str(val >> 4);
        *(hexp++) = hexdigit2str(val & 0xf);
    }
    *hexp = '\0';

    trace_aes_dump_data(desc, hex);
}

static bool cmd_data(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t ctxt_iv = 0;
    uint32_t ctxt_key = (cmd & CMD_DATA_KEY_CTX_MASK) >> CMD_DATA_KEY_CTX_SHIFT;
    uint32_t len = cmd & CMD_DATA_LEN_MASK;
    uint64_t src_addr = s->fifo[2];
    uint64_t dst_addr = s->fifo[3];
    QCryptoCipherAlgorithm alg;
    QCryptoCipher *cipher;
    char *src;
    char *dst;

    src_addr |= ((uint64_t)s->fifo[1] << 16) & 0xffff00000000ULL;
    dst_addr |= ((uint64_t)s->fifo[1] << 32) & 0xffff00000000ULL;

    trace_aes_cmd_data(ctxt_key, ctxt_iv, src_addr, dst_addr, len);

    if (!has_payload(s, 3)) {
        /* wait for payload */
        trace_aes_cmd_data_error("No payload");
        return false;
    }

    if (ctxt_key >= ARRAY_SIZE(s->key) ||
        ctxt_iv >= ARRAY_SIZE(s->iv)) {
        /* Invalid input */
        trace_aes_cmd_data_error("Invalid key or iv");
        return false;
    }

    src = g_malloc0(len);
    dst = g_malloc0(len);

    cpu_physical_memory_read(src_addr, src, len);

    dump_data("cmd_data(): src_data=", src, len);

    switch (s->key[ctxt_key].key_len) {
    case 128 / 8:
        alg = QCRYPTO_CIPHER_ALG_AES_128;
        break;
    case 192 / 8:
        alg = QCRYPTO_CIPHER_ALG_AES_192;
        break;
    case 256 / 8:
        alg = QCRYPTO_CIPHER_ALG_AES_256;
        break;
    default:
        trace_aes_cmd_data_error("Invalid key len");
        goto err_free;
    }
    cipher = qcrypto_cipher_new(alg, s->block_mode,
                                (void *)s->key[ctxt_key].key,
                                s->key[ctxt_key].key_len, NULL);
    g_assert(cipher != NULL);
    if (s->block_mode != QCRYPTO_CIPHER_MODE_ECB) {
        if (qcrypto_cipher_setiv(cipher, (void *)s->iv[ctxt_iv].iv,
                                 sizeof(s->iv[ctxt_iv].iv), NULL) != 0) {
            trace_aes_cmd_data_error("Failed to set IV");
            goto err_free_cipher;
        }
    }
    if (s->is_encrypt) {
        if (qcrypto_cipher_encrypt(cipher, src, dst, len, NULL) != 0) {
            trace_aes_cmd_data_error("Encrypt failed");
            goto err_free_cipher;
        }
    } else {
        if (qcrypto_cipher_decrypt(cipher, src, dst, len, NULL) != 0) {
            trace_aes_cmd_data_error("Decrypt failed");
            goto err_free_cipher;
        }
    }
    qcrypto_cipher_free(cipher);

    dump_data("cmd_data(): dst_data=", dst, len);
    cpu_physical_memory_write(dst_addr, dst, len);
    g_free(src);
    g_free(dst);

    return true;

err_free_cipher:
    qcrypto_cipher_free(cipher);
err_free:
    g_free(src);
    g_free(dst);
    return false;
}

static bool cmd_store_iv(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t ctxt = (cmd & CMD_IV_CONTEXT_MASK) >> CMD_IV_CONTEXT_SHIFT;
    uint64_t addr = s->fifo[1];

    if (!has_payload(s, 1)) {
        /* wait for payload */
        return false;
    }

    if (ctxt >= ARRAY_SIZE(s->iv)) {
        /* Invalid context selected */
        return false;
    }

    addr |= ((uint64_t)cmd << 32) & 0xff00000000ULL;
    cpu_physical_memory_write(addr, &s->iv[ctxt].iv, sizeof(s->iv[ctxt].iv));

    trace_aes_cmd_store_iv(ctxt, addr, s->iv[ctxt].iv[0], s->iv[ctxt].iv[1],
                           s->iv[ctxt].iv[2], s->iv[ctxt].iv[3]);

    return true;
}

static bool cmd_flag(AESState *s)
{
    uint32_t cmd = s->fifo[0];
    uint32_t raise_irq = cmd & CMD_FLAG_RAISE_IRQ_MASK;

    /* We always process data when it's coming in, so fire an IRQ immediately */
    if (raise_irq) {
        s->irq_status |= REG_IRQ_STATUS_FLAG;
    }

    s->flag_info = cmd & CMD_FLAG_INFO_MASK;

    trace_aes_cmd_flag(!!raise_irq, s->flag_info);

    return true;
}

static void fifo_process(AESState *s)
{
    uint32_t cmd = s->fifo[0] >> CMD_SHIFT;
    bool success = false;

    if (!s->fifo_idx) {
        return;
    }

    switch (cmd) {
    case CMD_KEY:
        success = cmd_key(s);
        break;
    case CMD_IV:
        success = cmd_iv(s);
        break;
    case CMD_DATA:
        success = cmd_data(s);
        break;
    case CMD_STORE_IV:
        success = cmd_store_iv(s);
        break;
    case CMD_FLAG:
        success = cmd_flag(s);
        break;
    default:
        s->irq_status |= REG_IRQ_STATUS_INVALID_CMD;
        break;
    }

    if (success) {
        s->fifo_idx = 0;
    }

    trace_aes_fifo_process(cmd, success ? 1 : 0);
}

static void aes1_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    AESState *s = opaque;

    trace_aes_write(offset, val);

    switch (offset) {
    case REG_IRQ_STATUS:
        s->irq_status &= ~val;
        break;
    case REG_IRQ_ENABLE:
        s->irq_enable = val;
        break;
    case REG_FIFO:
        fifo_append(s, val);
        fifo_process(s);
        break;
    default:
        trace_aes_write_unknown(offset);
        return;
    }

    aes_update_irq(s);
}

static const MemoryRegionOps aes1_ops = {
    .read = aes1_read,
    .write = aes1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t aes2_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t res = 0;

    switch (offset) {
    case 0:
        res = 0;
        break;
    default:
        trace_aes_2_read_unknown(offset);
        break;
    }

    trace_aes_2_read(offset, res);

    return res;
}

static void aes2_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    trace_aes_2_write(offset, val);

    switch (offset) {
    default:
        trace_aes_2_write_unknown(offset);
        return;
    }
}

static const MemoryRegionOps aes2_ops = {
    .read = aes2_read,
    .write = aes2_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void aes_reset(DeviceState *d)
{
    AESState *s = AES(d);

    s->status = 0x3f80;
    s->q_status = 2;
    s->irq_status = 0;
    s->irq_enable = 0;
    s->watermark = 0;
}

static void aes_init(Object *obj)
{
    AESState *s = AES(obj);

    memory_region_init_io(&s->iomem1, obj, &aes1_ops, s, TYPE_AES, 0x4000);
    memory_region_init_io(&s->iomem2, obj, &aes2_ops, s, TYPE_AES, 0x4000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem1);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem2);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq);
}

static void aes_realize(DeviceState *dev, Error **errp)
{
}

static void aes_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = aes_reset;
    dc->realize = aes_realize;
}

static const TypeInfo aes_info = {
    .name          = TYPE_AES,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AESState),
    .class_init    = aes_class_init,
    .instance_init = aes_init,
};

static void aes_register_types(void)
{
    type_register_static(&aes_info);
}

type_init(aes_register_types)
