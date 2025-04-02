/*
 * ASPEED Secure Boot Controller
 *
 * Copyright (C) 2021-2022 IBM Corp.
 *
 * Joel Stanley <joel@jms.id.au>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/misc/aspeed_sbc.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "system/block-backend.h"
#include "qobject/qdict.h"

#define R_PROT          (0x000 / 4)
#define R_CMD           (0x004 / 4)
#define R_ADDR          (0x010 / 4)
#define R_STATUS        (0x014 / 4)
#define R_CAMP1         (0x020 / 4)
#define R_CAMP2         (0x024 / 4)
#define R_QSR           (0x040 / 4)

/* R_STATUS */
#define ABR_EN                  BIT(14) /* Mirrors SCU510[11] */
#define ABR_IMAGE_SOURCE        BIT(13)
#define SPI_ABR_IMAGE_SOURCE    BIT(12)
#define SB_CRYPTO_KEY_EXP_DONE  BIT(11)
#define SB_CRYPTO_BUSY          BIT(10)
#define OTP_WP_EN               BIT(9)
#define OTP_ADDR_WP_EN          BIT(8)
#define LOW_SEC_KEY_EN          BIT(7)
#define SECURE_BOOT_EN          BIT(6)
#define UART_BOOT_EN            BIT(5)
/* bit 4 reserved*/
#define OTP_CHARGE_PUMP_READY   BIT(3)
#define OTP_IDLE                BIT(2)
#define OTP_MEM_IDLE            BIT(1)
#define OTP_COMPARE_STATUS      BIT(0)

/* QSR */
#define QSR_RSA_MASK           (0x3 << 12)
#define QSR_HASH_MASK          (0x3 << 10)

#define OTP_FILE_PATH "otpmem"

#define BLK_VALID(s) \
    do { \
        if (s->blk == NULL) { \
            qemu_log_mask(LOG_GUEST_ERROR, \
                          "%s: blk is not initialized\n", \
                          __func__); \
            return; \
        } \
    } while (0)

static uint64_t aspeed_sbc_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);

    addr >>= 2;

    if (addr >= ASPEED_SBC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return 0;
    }

    return s->regs[addr];
}

static void aspeed_sbc_otpmem_read(void *opaque)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);
    uint32_t otp_addr, data, otp_offset;
    bool is_data = false;

    BLK_VALID(s);
    otp_addr = s->regs[R_ADDR];
    if (otp_addr < OTP_DATA_DWORD_COUNT) {
        is_data = true;
    } else if (otp_addr >= OTP_TOTAL_DWORD_COUNT) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid OTP addr 0x%x\n",
                      __func__, otp_addr);
        return;
    }
    otp_offset = otp_addr << 2;

    if (blk_pread(s->blk, (int64_t)otp_offset, sizeof(data), &data, 0) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to read data 0x%x\n",
                      __func__, otp_offset);
        return;
    }
    s->regs[R_CAMP1] = data;

    if (is_data) {
        if (blk_pread(s->blk, (int64_t)otp_offset + 4,
                      sizeof(data), &data, 0) < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Failed to read data 0x%x\n",
                          __func__, otp_offset);
            return;
        }
        s->regs[R_CAMP2] = data;
    }
}

static bool check_bit_conditions(uint32_t otp_addr,
                                 uint32_t value, uint32_t prog_bit)
{
    uint32_t programed_bits, pass;
    bool is_odd = otp_addr & 1;

    if (is_odd) {
        programed_bits = ~value & prog_bit;
    } else {
        programed_bits = value & (~prog_bit);
    }

    pass = value ^ (~prog_bit);

    if (programed_bits) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Found programed bits in addr %x\n",
                      __func__, otp_addr);
        for (int i = 0; i < 32; ++i) {
            if (programed_bits & (1U << i)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "  Programed bit %d\n",
                              i);
            }
        }
    }

    return pass != 0;
}

static bool program_otp_data(void *opaque, uint32_t otp_addr,
                             uint32_t prog_bit, uint32_t *value)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);
    bool is_odd = otp_addr & 1;
    uint32_t otp_offset = otp_addr << 2;

    if (blk_pread(s->blk, (int64_t)otp_offset,
                  sizeof(uint32_t), value, 0) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to read data 0x%x\n",
                      __func__, otp_offset);
        return false;
    }

    if (check_bit_conditions(otp_addr, *value, prog_bit) == false) {
        return false;
    }

    if (is_odd) {
        *value &= ~prog_bit;
    } else {
        *value |= ~prog_bit;
    }

    return true;
}

static void mr_handler(uint32_t otp_addr, uint32_t data)
{
    switch (otp_addr) {
    case MODE_REGISTER:
    case MODE_REGISTER_A:
    case MODE_REGISTER_B:
        /* HW behavior, do nothing here */
        break;
    default:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Unsupported address 0x%x\n",
                  __func__, otp_addr);
        return;
    }
}

static void aspeed_sbc_otpmem_write(void *opaque)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);
    uint32_t otp_addr, data;

    otp_addr = s->regs[R_ADDR];
    data = s->regs[R_CAMP1];

    if (otp_addr == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: ignore write program bit request\n",
                      __func__);
    } else if (otp_addr >= MODE_REGISTER) {
        mr_handler(otp_addr, data);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unhandled OTP write address 0x%x\n",
                      __func__, otp_addr);
    }
}

static void aspeed_sbc_otpmem_prog(void *opaque)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);
    uint32_t otp_addr, value, otp_offset;

    BLK_VALID(s);
    otp_addr = s->regs[R_ADDR];
    if (otp_addr >= OTP_TOTAL_DWORD_COUNT) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid OTP addr 0x%x\n",
                      __func__, otp_addr);
        return;
    }

    otp_offset = otp_addr << 2;
    if (program_otp_data(opaque, otp_addr,
                         s->regs[R_CAMP1], &value) == false) {
        qemu_log_mask(LOG_GUEST_ERROR,
                       "%s: Failed to program data 0x%x to 0x%x\n",
                      __func__, s->regs[R_CAMP1], otp_offset);
        return;
    }

    if (blk_pwrite(s->blk, (int64_t)otp_offset,
                   sizeof(value), &value, 0) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to write data 0x%x to 0x%x\n",
                      __func__, value, otp_offset);
    }
}

static void aspeed_sbc_handle_command(void *opaque, uint32_t cmd)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);

    s->regs[R_STATUS] &= ~(OTP_MEM_IDLE | OTP_IDLE);

    switch (cmd) {
    case READ_CMD:
        aspeed_sbc_otpmem_read(s);
        break;
    case WRITE_CMD:
        aspeed_sbc_otpmem_write(s);
        break;
    case PROG_CMD:
        aspeed_sbc_otpmem_prog(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Unknown command 0x%x\n",
                      __func__, cmd);
        break;
    }

    s->regs[R_STATUS] |= (OTP_MEM_IDLE | OTP_IDLE);
}

static void aspeed_sbc_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedSBCState *s = ASPEED_SBC(opaque);

    addr >>= 2;

    if (addr >= ASPEED_SBC_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    }

    switch (addr) {
    case R_STATUS:
    case R_QSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read only register 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    case R_CMD:
        aspeed_sbc_handle_command(opaque, data);
        return;
    default:
        break;
    }

    s->regs[addr] = data;
}

static const MemoryRegionOps aspeed_sbc_ops = {
    .read = aspeed_sbc_read,
    .write = aspeed_sbc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_sbc_reset(DeviceState *dev)
{
    struct AspeedSBCState *s = ASPEED_SBC(dev);

    memset(s->regs, 0, sizeof(s->regs));

    /* Set secure boot enabled with RSA4096_SHA256 and enable eMMC ABR */
    s->regs[R_STATUS] = OTP_IDLE | OTP_MEM_IDLE;

    if (s->emmc_abr) {
        s->regs[R_STATUS] &= ABR_EN;
    }

    if (s->signing_settings) {
        s->regs[R_STATUS] &= SECURE_BOOT_EN;
    }

    s->regs[R_QSR] = s->signing_settings;

    if (!s->blk) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: blk not initialized!\n",
                      __func__);
        return;
    }
}

static BlockBackend *init_otpmem(int64_t size_bytes)
{
    Error *local_err = NULL;
    BlockDriverState *bs = NULL;
    BlockBackend *blk = NULL;
    bool image_created = false;
    QDict *options;
    uint32_t i, odd_def = 0xffffffff, even_def = 0, *def;

    if (!g_file_test(OTP_FILE_PATH, G_FILE_TEST_EXISTS)) {
        bdrv_img_create(OTP_FILE_PATH, "raw", NULL, NULL,
                        NULL, size_bytes, 0, true, &local_err);
        if (local_err) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Failed to create image %s: %s\n",
                          __func__, OTP_FILE_PATH,
                          error_get_pretty(local_err));
            error_free(local_err);
            return NULL;
        }
        image_created = true;
    }

    blk = blk_new(qemu_get_aio_context(),
                  BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                  0);
    if (!blk) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to create BlockBackend\n",
                      __func__);
        return NULL;
    }

    options =  qdict_new();
    qdict_put_str(options, "driver", "raw");
    bs = bdrv_open(OTP_FILE_PATH, NULL, options, BDRV_O_RDWR, &local_err);
    if (local_err) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to create OTP memory, err = %s\n",
                      __func__, error_get_pretty(local_err));
        blk_unref(blk);
        error_free(local_err);
        return NULL;
    }

    blk_insert_bs(blk, bs, &local_err);
    if (local_err) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to insert OTP memory to SBC, err = %s\n",
                      __func__, error_get_pretty(local_err));
        bdrv_unref(bs);
        blk_unref(blk);
        error_free(local_err);
        return NULL;
    }
    bdrv_unref(bs);

    if (image_created) {
        /* init otp memory data */
        for (i = 0; i < OTP_TOTAL_DWORD_COUNT; i++) {
            if (i & 1) {
                def = &odd_def;
            } else {
                def = &even_def;
            }

            if (blk_pwrite(blk, i << 2, sizeof(uint32_t), def, 0) < 0) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Failed to init OTP memory file\n",
                              __func__);
                blk_unref(blk);
                return NULL;
            }
        }
    }

    return blk;
}

static void aspeed_sbc_realize(DeviceState *dev, Error **errp)
{
    AspeedSBCState *s = ASPEED_SBC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_sbc_ops, s,
            TYPE_ASPEED_SBC, 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);

    s->blk = init_otpmem(OTP_FILE_SIZE);
    if (s->blk == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to attach otpmem\n",
                      __func__);
    }
}

static const VMStateDescription vmstate_aspeed_sbc = {
    .name = TYPE_ASPEED_SBC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedSBCState, ASPEED_SBC_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property aspeed_sbc_properties[] = {
    DEFINE_PROP_BOOL("emmc-abr", AspeedSBCState, emmc_abr, 0),
    DEFINE_PROP_UINT32("signing-settings", AspeedSBCState, signing_settings, 0),
};

static void aspeed_sbc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_sbc_realize;
    device_class_set_legacy_reset(dc, aspeed_sbc_reset);
    dc->vmsd = &vmstate_aspeed_sbc;
    device_class_set_props(dc, aspeed_sbc_properties);
}

static const TypeInfo aspeed_sbc_info = {
    .name = TYPE_ASPEED_SBC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedSBCState),
    .class_init = aspeed_sbc_class_init,
    .class_size = sizeof(AspeedSBCClass)
};

static void aspeed_ast2600_sbc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "AST2600 Secure Boot Controller";
}

static const TypeInfo aspeed_ast2600_sbc_info = {
    .name = TYPE_ASPEED_AST2600_SBC,
    .parent = TYPE_ASPEED_SBC,
    .class_init = aspeed_ast2600_sbc_class_init,
};

static void aspeed_sbc_register_types(void)
{
    type_register_static(&aspeed_ast2600_sbc_info);
    type_register_static(&aspeed_sbc_info);
}

type_init(aspeed_sbc_register_types);
