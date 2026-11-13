/*
 * Rockchip OTP
 *
 * Copyright (C) 2026 Process Mission
 *
 * Authors:
 *    Chao Liu <chao.liu@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The automatic read interface exposes a bank of unprogrammed OTP words.
 * Each read command fills the data registers and sets the read-done status;
 * the status is cleared by a write-one-to-clear acknowledgement.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "hw/core/register.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_ROCKCHIP_OTP "rockchip-otp"
OBJECT_DECLARE_SIMPLE_TYPE(RockchipOTPState, ROCKCHIP_OTP)

/* Size of the OTP window in the RK3588 address map. */
#define ROCKCHIP_OTP_SIZE 0x400
/* Highest byte address described by rockchip_otp_regs_info[]. */
#define ROCKCHIP_OTP_MAX_REG 0x84
#define ROCKCHIP_OTP_NUM_REGS \
    ((ROCKCHIP_OTP_MAX_REG / sizeof(uint32_t)) + 1)
/* Number of 32-bit words in the OTP array. */
#define ROCKCHIP_OTP_NUM_WORDS 1024

struct RockchipOTPState {
    SysBusDevice parent_obj;

    RegisterInfo regs_info[ROCKCHIP_OTP_NUM_REGS];
    uint32_t regs[ROCKCHIP_OTP_NUM_REGS];
};

REG32(AUTO_CTRL, 0x04)
REG32(AUTO_EN, 0x08)
REG32(DOUT0, 0x20)
REG32(DOUT1, 0x24)
REG32(DOUT2, 0x28)
REG32(DOUT3, 0x2c)
REG32(INT_STATUS, 0x84)

/* INT_STATUS: a read has finished. */
#define ROCKCHIP_OTP_INT_STATUS_READ_DONE BIT(1)

static void rockchip_otp_auto_read(RegisterInfo *reg, uint64_t value)
{
    RockchipOTPState *s = ROCKCHIP_OTP(reg->opaque);

    if (value & 1) {
        uint32_t control = s->regs[R_AUTO_CTRL];
        unsigned int address = control >> 16;
        unsigned int count = (control >> 8) & 0xff;

        s->regs[R_AUTO_EN] = 0;
        if (count == 0 || count > 4 ||
            address + count > ROCKCHIP_OTP_NUM_WORDS) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "rockchip-otp: invalid read command\n");
            return;
        }
        /* Unprogrammed fuse words read as zero. */
        for (unsigned int i = 0; i < count; i++) {
            s->regs[R_DOUT0 + i] = 0;
        }
        s->regs[R_INT_STATUS] |= ROCKCHIP_OTP_INT_STATUS_READ_DONE;
    }
}

static const RegisterAccessInfo rockchip_otp_regs_info[] = {
    { .name = "AUTO_CTRL", .addr = A_AUTO_CTRL },
    { .name = "AUTO_EN", .addr = A_AUTO_EN,
      .ro = ~1U, .post_write = rockchip_otp_auto_read },
    { .name = "DOUT0", .addr = A_DOUT0, .ro = UINT32_MAX },
    { .name = "DOUT1", .addr = A_DOUT1, .ro = UINT32_MAX },
    { .name = "DOUT2", .addr = A_DOUT2, .ro = UINT32_MAX },
    { .name = "DOUT3", .addr = A_DOUT3, .ro = UINT32_MAX },
    { .name = "INT_STATUS", .addr = A_INT_STATUS,
      .ro = ~ROCKCHIP_OTP_INT_STATUS_READ_DONE,
      .w1c = ROCKCHIP_OTP_INT_STATUS_READ_DONE },
};

static const MemoryRegionOps rockchip_otp_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rockchip_otp_reset(DeviceState *dev)
{
    RockchipOTPState *s = ROCKCHIP_OTP(dev);

    for (int i = 0; i < ARRAY_SIZE(s->regs_info); i++) {
        register_reset(&s->regs_info[i]);
    }
}

static void rockchip_otp_init(Object *obj)
{
    RockchipOTPState *s = ROCKCHIP_OTP(obj);
    RegisterInfoArray *reg_array;

    reg_array = register_init_block32(
        DEVICE(obj), rockchip_otp_regs_info, ARRAY_SIZE(rockchip_otp_regs_info),
        s->regs_info, s->regs, &rockchip_otp_ops, false, ROCKCHIP_OTP_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &reg_array->mem);
}

static void rockchip_otp_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, rockchip_otp_reset);
}

static const TypeInfo rockchip_otp_type_info = {
    .name = TYPE_ROCKCHIP_OTP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RockchipOTPState),
    .instance_init = rockchip_otp_init,
    .class_init = rockchip_otp_class_init,
};

static void rockchip_otp_register_types(void)
{
    type_register_static(&rockchip_otp_type_info);
}

type_init(rockchip_otp_register_types)
