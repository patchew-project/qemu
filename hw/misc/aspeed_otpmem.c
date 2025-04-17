/*
 *  ASPEED OTP (One-Time Programmable) memory
 *
 *  Copyright (C) 2025 Aspeed
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/block/block.h"
#include "hw/block/flash.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "system/block-backend.h"
#include "qemu/log.h"
#include "qemu/option.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "hw/misc/aspeed_otpmem.h"

static const Property aspeed_otpmem_properties[] = {
    DEFINE_PROP_DRIVE("drive", AspeedOTPMemState, blk),
};

static void aspeed_otpmem_read(void *opaque, uint32_t addr,
                               uint32_t *out, Error **errp)
{
    AspeedOTPMemState *otp = ASPEED_OTPMEM(opaque);

    if (!otp->blk) {
        error_setg(errp, "OTP memory is not initialized");
        return;
    }

    if (out == NULL) {
        error_setg(errp, "out is NULL");
        return;
    }

    if (addr > (otp->max_size - 4)) {
        error_setg(errp, "OTP memory 0x%x is exceeded", addr);
        return;
    }

    if (blk_pread(otp->blk, (int64_t)addr, sizeof(uint32_t), out, 0) < 0) {
        error_setg(errp, "Failed to read data 0x%x", addr);
        return;
    }
    return;
}

static bool valid_program_data(uint32_t otp_addr,
                                 uint32_t value, uint32_t prog_bit)
{
    uint32_t programmed_bits, has_programmable_bits;
    bool is_odd = otp_addr & 1;

    /*
     * prog_bit uses 0s to indicate target bits to program:
     *   - if OTP word is even-indexed, programmed bits flip 0->1
     *   - if odd, bits flip 1->0
     * Bit programming is one-way only and irreversible.
     */
    if (is_odd) {
        programmed_bits = ~value & prog_bit;
    } else {
        programmed_bits = value & (~prog_bit);
    }

    /* If there is some bit can be programed, to accept the request */
    has_programmable_bits = value ^ (~prog_bit);

    if (programmed_bits) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Found programmed bits in addr %x\n",
                      __func__, otp_addr);
        for (int i = 0; i < 32; ++i) {
            if (programmed_bits & (1U << i)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "  Programmed bit %d\n",
                              i);
            }
        }
    }

    return has_programmable_bits != 0;
}

static bool program_otpmem_data(void *opaque, uint32_t otp_addr,
                             uint32_t prog_bit, uint32_t *value)
{
    AspeedOTPMemState *s = ASPEED_OTPMEM(opaque);
    bool is_odd = otp_addr & 1;
    uint32_t otp_offset = otp_addr << 2;

    if (blk_pread(s->blk, (int64_t)otp_offset,
                  sizeof(uint32_t), value, 0) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Failed to read data 0x%x\n",
                      __func__, otp_offset);
        return false;
    }

    if (!valid_program_data(otp_addr, *value, prog_bit)) {
        return false;
    }

    if (is_odd) {
        *value &= ~prog_bit;
    } else {
        *value |= ~prog_bit;
    }

    return true;
}

static void aspeed_otpmem_prog(void *s, uint32_t otp_addr,
                               uint32_t data, Error **errp)
{
    AspeedOTPMemState *otp = ASPEED_OTPMEM(s);
    uint32_t otp_offset, value;

    if (!otp->blk) {
        error_setg(errp, "OTP memory is not initialized");
        return;
    }

    if (otp_addr > (otp->max_size >> 2)) {
        error_setg(errp, "OTP memory 0x%x is exceeded", otp_addr);
        return;
    }

    otp_offset = otp_addr << 2;
    if (!program_otpmem_data(s, otp_addr, data, &value)) {
        error_setg(errp, "Failed to program data");
        return;
    }

    if (blk_pwrite(otp->blk, (int64_t)otp_offset,
                   sizeof(value), &value, 0) < 0) {
        error_setg(errp, "Failed to write data");
    }

    return;
}

static void aspeed_otpmem_set_default(void *s, uint32_t otp_offset,
                                      uint32_t data, Error **errp)
{
    AspeedOTPMemState *otp = ASPEED_OTPMEM(s);

    if ((otp_offset + 4) > otp->max_size) {
        error_setg(errp, "OTP memory 0x%x is exceeded", otp_offset);
        return;
    }

    if (blk_pwrite(otp->blk, (int64_t)otp_offset,
                   sizeof(data), &data, 0) < 0) {
        error_setg(errp, "Failed to write data");
    }
    return;
}

static AspeedOTPMemOps aspeed_otpmem_ops = {
    .read = aspeed_otpmem_read,
    .prog = aspeed_otpmem_prog,
    .set_default_value = aspeed_otpmem_set_default
};

static void aspeed_otpmem_realize(DeviceState *dev, Error **errp)
{
    AspeedOTPMemState *s = ASPEED_OTPMEM(dev);

    if (!s->blk) {
        error_setg(&error_fatal, "OTP memory is not initialized");
        return;
    }

    s->max_size = blk_getlength(s->blk);
    if (s->max_size < 0 || (s->max_size % 4)) {
        error_setg(&error_fatal,
                   "Unexpected OTP memory size: %" PRId64 "",
                   s->max_size);
        return;
    }

    s->ops = &aspeed_otpmem_ops;

    return;
}

static void aspeed_otpmem_system_reset(DeviceState *dev)
{
    return;
}

static void aspeed_otpmem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, aspeed_otpmem_system_reset);
    dc->realize = aspeed_otpmem_realize;
    device_class_set_props(dc, aspeed_otpmem_properties);

}

static const TypeInfo aspeed_otpmem_types[] = {
    {
        .name           = TYPE_ASPEED_OTPMEM,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(AspeedOTPMemState),
        .class_init     = aspeed_otpmem_class_init,
    },
};

DEFINE_TYPES(aspeed_otpmem_types)
